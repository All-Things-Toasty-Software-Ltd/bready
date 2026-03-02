// Copyright 2026 All Things Toasty Software Ltd
//
// BreadyBot class implementation.

#include "bready_bot.h"

#include <dpp/dpp.h>

#include <chrono>
#include <iostream>
#include <optional>
#include <string>
#include <thread>

#include "odoo_commands.h"

namespace bready {

namespace {

// Interval between Odoo-Discord bridge polls.
constexpr auto kPollInterval = std::chrono::seconds(10);

// HTML-escapes |text| so it is safe to embed in an HTML element body.
std::string HtmlEscape(const std::string& text) {
  std::string result;
  result.reserve(text.size());
  for (char c : text) {
    switch (c) {
      case '&': result += "&amp;"; break;
      case '<': result += "&lt;";  break;
      case '>': result += "&gt;";  break;
      case '"': result += "&quot;"; break;
      default:  result += c;       break;
    }
  }
  return result;
}

// Strips HTML tags from |html| and returns plain text.  Handles the basic
// subset used by Odoo Discuss (<p>, <br>, <strong>, <em>, etc.).
// <img src="…"> tags are replaced with the image URL on its own line.
// <a href="…">text</a> is rendered as "text (url)" so the link is preserved.
// Basic HTML entities (&amp; &lt; &gt; &quot; &nbsp;) are decoded.
std::string StripHtml(const std::string& html) {
  std::string result;
  result.reserve(html.size());
  std::string pending_href;  // URL saved when we enter an <a href="…"> tag.
  size_t i = 0;
  while (i < html.size()) {
    if (html[i] != '<') {
      result += html[i++];
      continue;
    }
    // Locate the closing '>'.
    const size_t gt = html.find('>', i + 1);
    if (gt == std::string::npos) {
      result += html[i++];
      continue;
    }
    const std::string tag = html.substr(i + 1, gt - i - 1);
    i = gt + 1;

    // Closing tags.
    if (!tag.empty() && tag[0] == '/') {
      // </a> → emit the saved href after the link text.
      if (tag.size() >= 2 && tag[1] == 'a' &&
          (tag.size() == 2 || tag[2] == ' ')) {
        if (!pending_href.empty()) {
          result += " (" + pending_href + ")";
          pending_href.clear();
        }
      }
      // All other closing tags are silently stripped.
      continue;
    }

    // <br> / <br/> / <br …> → newline.
    if (!tag.empty() && tag[0] == 'b' &&
        tag.size() >= 2 && tag[1] == 'r' &&
        (tag.size() == 2 || tag[2] == '/' || tag[2] == ' ')) {
      result += '\n';
      continue;
    }

    // <img src="…"> → URL on its own line.
    if (tag.size() >= 3 && tag[0] == 'i' && tag[1] == 'm' && tag[2] == 'g') {
      const auto sp = tag.find("src=\"");
      if (sp != std::string::npos) {
        const size_t url_start = sp + 5;
        const size_t url_end = tag.find('"', url_start);
        if (url_end != std::string::npos) {
          const std::string url = tag.substr(url_start, url_end - url_start);
          if (!url.empty()) {
            if (!result.empty() && result.back() != '\n') result += '\n';
            result += url;
            result += '\n';
          }
        }
      }
      continue;
    }

    // <a href="…"> → save href; it will be appended after the link text at </a>.
    if (!tag.empty() && tag[0] == 'a' &&
        (tag.size() == 1 || tag[1] == ' ')) {
      const auto href_pos = tag.find("href=\"");
      if (href_pos != std::string::npos) {
        const size_t url_start = href_pos + 6;
        const size_t url_end = tag.find('"', url_start);
        if (url_end != std::string::npos) {
          pending_href = tag.substr(url_start, url_end - url_start);
        }
      }
      continue;
    }

    // All other tags are silently stripped.
  }

  // Decode basic HTML entities.
  std::string decoded;
  decoded.reserve(result.size());
  for (size_t j = 0; j < result.size(); ++j) {
    if (result[j] != '&') {
      decoded += result[j];
      continue;
    }
    if      (result.compare(j, 5, "&amp;")  == 0) { decoded += '&';  j += 4U; }
    else if (result.compare(j, 4, "&lt;")   == 0) { decoded += '<';  j += 3U; }
    else if (result.compare(j, 4, "&gt;")   == 0) { decoded += '>';  j += 3U; }
    else if (result.compare(j, 6, "&quot;") == 0) { decoded += '"';  j += 5U; }
    else if (result.compare(j, 6, "&nbsp;") == 0) { decoded += ' ';  j += 5U; }
    else                                           { decoded += result[j]; }
  }

  // Collapse leading/trailing whitespace.
  const auto start = decoded.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) return "";
  const auto end = decoded.find_last_not_of(" \t\r\n");
  return decoded.substr(start, end - start + 1);
}

// Builds an HTML snippet for a single Discord attachment to be embedded in an
// Odoo Discuss message body.
std::string AttachmentHtml(const dpp::attachment& att) {
  const std::string& ct = att.content_type;
  if (ct.rfind("image/", 0) == 0) {
    return "<p><img src=\"" + HtmlEscape(att.url) + "\"/></p>";
  }
  if (ct.rfind("audio/", 0) == 0) {
    return "<p>\xF0\x9F\x8E\xB5 <a href=\"" + HtmlEscape(att.url) + "\">" +
           HtmlEscape(att.filename) + "</a></p>";
  }
  return "<p>\xF0\x9F\x93\x8E <a href=\"" + HtmlEscape(att.url) + "\">" +
         HtmlEscape(att.filename) + "</a></p>";
}

}  // namespace

BreadyBot::BreadyBot(const std::string& token, const OdooConfig& odoo_config,
                     const std::string& user_store_path,
                     const std::string& bridge_store_path,
                     const std::string& bridge_db_store_path,
                     dpp::snowflake admin_role_id,
                     dpp::snowflake member_role_id)
    : bot_(token,
           dpp::i_default_intents | dpp::i_guild_members |
               dpp::i_message_content),
      odoo_client_(odoo_config),
      user_store_(user_store_path),
      bridge_store_(bridge_store_path),
      bridge_db_store_(bridge_db_store_path),
      admin_role_id_(admin_role_id),
      member_role_id_(member_role_id) {}

BreadyBot::~BreadyBot() {
  running_ = false;
  if (poll_thread_.joinable()) {
    poll_thread_.join();
  }
}

void BreadyBot::Run() {
  bot_.on_log(dpp::utility::cout_logger());

  bot_.on_ready([this](const dpp::ready_t& /*event*/) {
    std::cout << "Bready is online as " << bot_.me.username << "!\n";
    if (dpp::run_once<struct RegisterBotCommands>()) {
      RegisterCommands();
    }
    // Validate the shared bot Odoo credentials at startup.
    if (odoo_client_.IsConfigured() && !odoo_client_.ValidateCredentials()) {
      std::cerr << "[odoo] Warning: shared bot credentials are invalid. "
                   "Verify ODOO_URL, ODOO_DB, ODOO_USER, and ODOO_API_KEY, "
                   "then restart the bot.\n";
    }
  });

  bot_.on_slashcommand(
      [this](const dpp::slashcommand_t& event) { OnSlashCommand(event); });

  bot_.on_message_create([this](const dpp::message_create_t& event) {
    OnMessageCreate(event);
  });

  // Start the Odoo-polling thread before blocking on the gateway.
  running_ = true;
  poll_thread_ = std::thread([this]() { PollOdooChannels(); });

  bot_.start(dpp::st_wait);
}

// Access control

BreadyBot::AccessLevel BreadyBot::GetAccessLevel(
    const dpp::slashcommand_t& event) const {
  // If neither role is configured, grant full access to everyone.
  if (admin_role_id_ == dpp::snowflake{0} &&
      member_role_id_ == dpp::snowflake{0}) {
    return AccessLevel::kAdmin;
  }

  const auto& roles = event.command.member.get_roles();
  bool has_admin = false;
  bool has_member = false;
  for (const auto& r : roles) {
    if (admin_role_id_ != dpp::snowflake{0} && r == admin_role_id_)
      has_admin = true;
    if (member_role_id_ != dpp::snowflake{0} && r == member_role_id_)
      has_member = true;
  }

  if (has_admin) return AccessLevel::kAdmin;
  if (has_member) return AccessLevel::kMember;
  return AccessLevel::kNone;
}

bool BreadyBot::RequireMemberAccess(const dpp::slashcommand_t& event) const {
  if (GetAccessLevel(event) != AccessLevel::kNone) return true;
  event.reply(
      dpp::message(
          "🚫 You need the **Member** or **Admin** role to use this bot.  "
          "Please ask a server admin to assign you the required role.")
          .set_flags(dpp::m_ephemeral));
  return false;
}

bool BreadyBot::RequireAdminAccess(const dpp::slashcommand_t& event) const {
  if (GetAccessLevel(event) == AccessLevel::kAdmin) return true;
  event.reply(
      dpp::message("🚫 This command requires the **Admin** role.")
          .set_flags(dpp::m_ephemeral));
  return false;
}

UserContext BreadyBot::GetUserContext(const dpp::slashcommand_t& event) const {
  UserContext ctx;
  ctx.discord_id = event.command.usr.id;
  ctx.is_linked = user_store_.IsLinked(ctx.discord_id);
  ctx.db_index = -1;  // Default: use primary.
  if (std::holds_alternative<int64_t>(event.get_parameter("db_id"))) {
    ctx.db_index =
        static_cast<int>(std::get<int64_t>(event.get_parameter("db_id")));
  }
  return ctx;
}

// Command registration

void BreadyBot::RegisterCommands() {
  // Odoo account-link commands
  dpp::slashcommand odoo_link_cmd("odoo_link",
                                  "Add a new linked Odoo instance to your "
                                  "account.",
                                  bot_.me.id);
  odoo_link_cmd.add_option(
      dpp::command_option(dpp::co_string, "url",
                          "Base URL of your Odoo instance "
                          "(e.g. https://mycompany.odoo.com).",
                          true));
  odoo_link_cmd.add_option(dpp::command_option(dpp::co_string, "database",
                                               "Odoo database name.", true));
  odoo_link_cmd.add_option(dpp::command_option(
      dpp::co_string, "username", "Your Odoo login e-mail.", true));
  odoo_link_cmd.add_option(dpp::command_option(
      dpp::co_string, "api_key",
      "Your Odoo API key (from Settings → API Keys).", true));

  dpp::slashcommand odoo_unlink_cmd(
      "odoo_unlink", "Remove all your linked Odoo accounts.", bot_.me.id);

  dpp::slashcommand odoo_db_list_cmd(
      "odoo_db_list",
      "List all your linked Odoo instances with their indices.", bot_.me.id);

  dpp::slashcommand odoo_db_remove_cmd(
      "odoo_db_remove", "Remove a specific linked Odoo instance.", bot_.me.id);
  odoo_db_remove_cmd.add_option(dpp::command_option(
      dpp::co_integer, "db_id",
      "Index of the Odoo instance to remove (see /odoo_db_list).", true));

  dpp::slashcommand odoo_db_set_primary_cmd(
      "odoo_db_set_primary", "Set which linked Odoo instance is your primary.",
      bot_.me.id);
  odoo_db_set_primary_cmd.add_option(dpp::command_option(
      dpp::co_integer, "db_id",
      "Index of the Odoo instance to make primary (see /odoo_db_list).", true));

  dpp::slashcommand odoo_link_user_cmd(
      "odoo_link_user",
      "Admin: link another user's Discord account to their Odoo account.",
      bot_.me.id);
  odoo_link_user_cmd.add_option(dpp::command_option(
      dpp::co_user, "user", "The Discord user to link.", true));
  odoo_link_user_cmd.add_option(dpp::command_option(
      dpp::co_string, "url", "Base URL of the user's Odoo instance.", true));
  odoo_link_user_cmd.add_option(dpp::command_option(
      dpp::co_string, "database", "Odoo database name.", true));
  odoo_link_user_cmd.add_option(dpp::command_option(
      dpp::co_string, "username", "The user's Odoo login e-mail.", true));
  odoo_link_user_cmd.add_option(dpp::command_option(
      dpp::co_string, "api_key", "The user's Odoo API key.", true));

  dpp::slashcommand odoo_whoami_cmd(
      "odoo_whoami", "Show your linked Odoo account status.", bot_.me.id);

  // Odoo data commands
  // Each data command accepts an optional db_id to target a specific linked
  // Odoo instance (defaults to primary).
  auto add_db_id_option = [](dpp::slashcommand& cmd) {
    cmd.add_option(dpp::command_option(
        dpp::co_integer, "db_id",
        "Index of the linked Odoo instance to use (default: primary).", false));
  };

  dpp::slashcommand odoo_projects_cmd("odoo_projects",
                                      "List your Odoo projects.", bot_.me.id);
  add_db_id_option(odoo_projects_cmd);

  dpp::slashcommand odoo_tasks_cmd(
      "odoo_tasks", "List Odoo tasks (optionally by project).", bot_.me.id);
  odoo_tasks_cmd.add_option(dpp::command_option(
      dpp::co_string, "project", "Filter tasks by project name.", false));
  add_db_id_option(odoo_tasks_cmd);

  dpp::slashcommand odoo_task_new_cmd("odoo_task_new",
                                      "Create a new task in Odoo.", bot_.me.id);
  odoo_task_new_cmd.add_option(
      dpp::command_option(dpp::co_string, "title", "Task title.", true));
  odoo_task_new_cmd.add_option(dpp::command_option(
      dpp::co_string, "project", "Project name (optional).", false));
  odoo_task_new_cmd.add_option(dpp::command_option(
      dpp::co_string, "description", "Task description (optional).", false));
  add_db_id_option(odoo_task_new_cmd);

  dpp::slashcommand odoo_todos_cmd(
      "odoo_todos", "List your personal Odoo to-dos.", bot_.me.id);
  add_db_id_option(odoo_todos_cmd);

  dpp::slashcommand odoo_todo_new_cmd(
      "odoo_todo_new", "Create a personal to-do in Odoo.", bot_.me.id);
  odoo_todo_new_cmd.add_option(
      dpp::command_option(dpp::co_string, "title", "To-do title.", true));
  add_db_id_option(odoo_todo_new_cmd);

  dpp::slashcommand odoo_crm_cmd(
      "odoo_crm", "List Odoo CRM leads and opportunities.", bot_.me.id);
  add_db_id_option(odoo_crm_cmd);

  dpp::slashcommand odoo_notes_cmd("odoo_notes",
                                   "List Odoo Knowledge articles.", bot_.me.id);
  add_db_id_option(odoo_notes_cmd);

  // Bridge commands (admin-only)
  dpp::slashcommand bridge_db_add_cmd(
      "bridge_db_add",
      "Admin: register bot Odoo credentials for use with channel bridges.",
      bot_.me.id);
  bridge_db_add_cmd.add_option(dpp::command_option(
      dpp::co_string, "url",
      "Base URL of the Odoo instance (e.g. https://mycompany.odoo.com).",
      true));
  bridge_db_add_cmd.add_option(dpp::command_option(
      dpp::co_string, "database", "Odoo database name.", true));
  bridge_db_add_cmd.add_option(dpp::command_option(
      dpp::co_string, "username", "Bot Odoo login e-mail.", true));
  bridge_db_add_cmd.add_option(dpp::command_option(
      dpp::co_string, "api_key",
      "Bot Odoo API key (from Settings → API Keys).", true));

  dpp::slashcommand bridge_db_list_cmd(
      "bridge_db_list",
      "Admin: list all registered bridge Odoo databases.", bot_.me.id);

  dpp::slashcommand bridge_db_remove_cmd(
      "bridge_db_remove",
      "Admin: remove a registered bridge Odoo database.", bot_.me.id);
  bridge_db_remove_cmd.add_option(dpp::command_option(
      dpp::co_integer, "bridge_db_id",
      "Index of the bridge database to remove (see /bridge_db_list).", true));

  dpp::slashcommand bridge_create_cmd(
      "bridge_create",
      "Admin: bridge a Discord channel to an Odoo Discuss channel.",
      bot_.me.id);
  bridge_create_cmd.add_option(dpp::command_option(
      dpp::co_channel, "discord_channel",
      "The Discord channel to bridge.", true));
  bridge_create_cmd.add_option(dpp::command_option(
      dpp::co_string, "odoo_channel",
      "Name of the Odoo Discuss channel to bridge to.", true));
  bridge_create_cmd.add_option(dpp::command_option(
      dpp::co_integer, "bridge_db_id",
      "Index of the bridge database to use (see /bridge_db_list)."
      " Omit to use the shared bot config.",
      false));

  dpp::slashcommand bridge_delete_cmd(
      "bridge_delete",
      "Admin: remove a Discord↔Odoo Discuss channel bridge.", bot_.me.id);
  bridge_delete_cmd.add_option(dpp::command_option(
      dpp::co_channel, "discord_channel",
      "The Discord channel whose bridge should be removed.", true));

  dpp::slashcommand bridge_list_cmd(
      "bridge_list", "Admin: list all active Discord↔Odoo channel bridges.",
      bot_.me.id);

  dpp::slashcommand help_cmd("help", "Show all Bready commands.", bot_.me.id);

  // Register all commands globally.
  bot_.global_bulk_command_create(
      {odoo_link_cmd,       odoo_unlink_cmd,     odoo_db_list_cmd,
       odoo_db_remove_cmd,  odoo_db_set_primary_cmd,
       odoo_link_user_cmd,  odoo_whoami_cmd,     odoo_projects_cmd,
       odoo_tasks_cmd,      odoo_task_new_cmd,   odoo_todos_cmd,
       odoo_todo_new_cmd,   odoo_crm_cmd,        odoo_notes_cmd,
       bridge_db_add_cmd,   bridge_db_list_cmd,  bridge_db_remove_cmd,
       bridge_create_cmd,   bridge_delete_cmd,   bridge_list_cmd,
       help_cmd},
      [](const dpp::confirmation_callback_t& cb) {
        if (cb.is_error()) {
          std::cerr << "Error registering slash commands: "
                    << cb.get_error().message << "\n";
        } else {
          std::cout << "Slash commands registered successfully.\n";
        }
      });
}

// Slash command dispatcher

void BreadyBot::OnSlashCommand(const dpp::slashcommand_t& event) {
  const std::string& cmd = event.command.get_command_name();

  // Help command (no role gate)
  if (cmd == "help") {
    constexpr uint32_t kOdooColor = 0x714B67;
    dpp::embed embed;
    embed.set_title("🔗 Bready — Odoo Integration Bot")
        .set_description("Connect your Discord server to Odoo.")
        .set_color(kOdooColor)
        .add_field("━━ Account ━━", "\u200b", false)
        .add_field("/odoo_whoami",
                   "Show whether your Discord account is linked to Odoo.",
                   false)
        .add_field("/odoo_link <url> <database> <username> <api_key>",
                   "Add a new linked Odoo instance.", false)
        .add_field("/odoo_unlink",
                   "Remove all your linked Odoo accounts.", false)
        .add_field("/odoo_db_list",
                   "List all your linked Odoo instances.", false)
        .add_field("/odoo_db_remove <db_id>",
                   "Remove a specific linked Odoo instance.", false)
        .add_field("/odoo_db_set_primary <db_id>",
                   "Set your primary Odoo instance.", false)
        .add_field(
            "/odoo_link_user <user> <url> <database> <username> <api_key>",
            "*(Admin only)* Link another user's Discord account to their Odoo "
            "account.",
            false)
        .add_field("━━ Data ━━", "\u200b", false)
        .add_field("/odoo_projects [db_id]",
                   "List Odoo projects (up to 25).", false)
        .add_field("/odoo_tasks [project] [db_id]",
                   "List tasks, optionally filtered by project.", false)
        .add_field("/odoo_task_new <title> [project] [description] [db_id]",
                   "Create a new Odoo task.", false)
        .add_field("/odoo_todos [db_id]", "List personal to-dos.", false)
        .add_field("/odoo_todo_new <title> [db_id]",
                   "Create a personal to-do.", false)
        .add_field("/odoo_crm [db_id]",
                   "List CRM leads and opportunities.", false)
        .add_field("/odoo_notes [db_id]", "List Knowledge articles.", false)
        .add_field("━━ Bridges (Admin) ━━", "\u200b", false)
        .add_field("/bridge_db_add <url> <database> <username> <api_key>",
                   "Register bot Odoo credentials for channel bridges.",
                   false)
        .add_field("/bridge_db_list",
                   "List all registered bridge Odoo databases.", false)
        .add_field("/bridge_db_remove <bridge_db_id>",
                   "Remove a registered bridge Odoo database.", false)
        .add_field("/bridge_create <discord_channel> <odoo_channel>"
                   " [bridge_db_id]",
                   "Bridge a Discord channel to an Odoo Discuss channel.",
                   false)
        .add_field("/bridge_delete <discord_channel>",
                   "Remove a Discord↔Odoo bridge.", false)
        .add_field("/bridge_list", "List all active bridges.", false)
        .set_footer(dpp::embed_footer().set_text(
            "Bready — Odoo integration for Discord. 🔗"));
    event.reply(dpp::message().add_embed(embed).set_flags(dpp::m_ephemeral));
    return;
  }

  // Admin-only commands
  if (cmd == "odoo_link_user") {
    if (!RequireAdminAccess(event)) return;
    HandleOdooLinkUserCommand(event, user_store_);
    return;
  } else if (cmd == "bridge_db_add") {
    if (!RequireAdminAccess(event)) return;
    HandleBridgeDbAddCommand(event, bridge_db_store_);
    return;
  } else if (cmd == "bridge_db_list") {
    if (!RequireAdminAccess(event)) return;
    HandleBridgeDbListCommand(event, bridge_db_store_);
    return;
  } else if (cmd == "bridge_db_remove") {
    if (!RequireAdminAccess(event)) return;
    HandleBridgeDbRemoveCommand(event, bridge_db_store_);
    return;
  } else if (cmd == "bridge_create") {
    if (!RequireAdminAccess(event)) return;
    HandleBridgeCreateCommand(event, odoo_client_, bridge_store_,
                              bridge_db_store_);
    return;
  } else if (cmd == "bridge_delete") {
    if (!RequireAdminAccess(event)) return;
    HandleBridgeDeleteCommand(event, bridge_store_);
    return;
  } else if (cmd == "bridge_list") {
    if (!RequireAdminAccess(event)) return;
    HandleBridgeListCommand(event, bridge_store_);
    return;
  }

  // Member-level commands
  if (!RequireMemberAccess(event)) return;

  // Account-link commands.
  if (cmd == "odoo_link") {
    HandleOdooLinkCommand(event, user_store_);
    return;
  } else if (cmd == "odoo_unlink") {
    HandleOdooUnlinkCommand(event, user_store_);
    return;
  } else if (cmd == "odoo_db_list") {
    HandleOdooDbListCommand(event, user_store_);
    return;
  } else if (cmd == "odoo_db_remove") {
    HandleOdooDbRemoveCommand(event, user_store_);
    return;
  } else if (cmd == "odoo_db_set_primary") {
    HandleOdooDbSetPrimaryCommand(event, user_store_);
    return;
  } else if (cmd == "odoo_whoami") {
    HandleOdooWhoamiCommand(event, user_store_);
    return;
  }

  // Per-user Odoo data commands
  UserContext user_ctx = GetUserContext(event);

  // Select the OdooClient to use: prefer the user's linked config, falling
  // back to the shared bot config.  If db_index was specified, use that
  // specific instance; otherwise use the primary.
  std::optional<OdooClient> user_client;
  if (user_ctx.is_linked) {
    std::optional<OdooConfig> cfg;
    if (user_ctx.db_index >= 0) {
      cfg = user_store_.GetUserConfigAt(user_ctx.discord_id, user_ctx.db_index);
      if (!cfg.has_value()) {
        event.reply(
            dpp::message("❌ Invalid `db_id`.  Use **/odoo_db_list** to see "
                         "your linked instances.")
                .set_flags(dpp::m_ephemeral));
        return;
      }
    } else {
      cfg = user_store_.GetUserConfig(user_ctx.discord_id);
    }
    if (cfg.has_value()) {
      user_client.emplace(*cfg);
    }
  }
  OdooClient& effective_client =
      user_client.has_value() ? *user_client : odoo_client_;

  if (effective_client.IsConfigured() &&
      !effective_client.ValidateCredentials()) {
    const std::string msg =
        user_ctx.is_linked
            ? "Your linked Odoo account credentials are invalid. "
              "Use **/odoo_link** to re-link with correct credentials."
            : "The bot's shared Odoo account could not authenticate. "
              "Please ask an administrator to verify the bot's Odoo "
              "configuration (ODOO_URL, ODOO_DB, ODOO_USER, ODOO_API_KEY).";
    event.reply(dpp::message()
                    .add_embed(dpp::embed()
                                   .set_title("❌ Odoo Authentication Failed")
                                   .set_description(msg)
                                   .set_color(0x714B67))
                    .set_flags(dpp::m_ephemeral));
    return;
  }

  if (cmd == "odoo_projects") {
    HandleOdooProjectsCommand(event, effective_client, user_ctx);
  } else if (cmd == "odoo_tasks") {
    HandleOdooTasksCommand(event, effective_client, user_ctx);
  } else if (cmd == "odoo_task_new") {
    HandleOdooTaskNewCommand(event, effective_client, user_ctx);
  } else if (cmd == "odoo_todos") {
    HandleOdooTodosCommand(event, effective_client, user_ctx);
  } else if (cmd == "odoo_todo_new") {
    HandleOdooTodoNewCommand(event, effective_client, user_ctx);
  } else if (cmd == "odoo_crm") {
    HandleOdooCrmCommand(event, effective_client, user_ctx);
  } else if (cmd == "odoo_notes") {
    HandleOdooNotesCommand(event, effective_client, user_ctx);
  }
}

// Discord-Odoo bridge

void BreadyBot::OnMessageCreate(const dpp::message_create_t& event) {
  // Skip messages sent by the bot itself to avoid echo loops.
  if (event.msg.author.is_bot()) return;

  if (!bridge_store_.HasBridge(event.msg.channel_id)) return;

  auto bridges = bridge_store_.GetBridges();
  for (const auto& bridge : bridges) {
    if (bridge.discord_channel_id != event.msg.channel_id) continue;

    // Resolve the OdooClient for this bridge.
    std::optional<OdooConfig> bridge_cfg;
    if (bridge.bridge_db_id >= 0) {
      bridge_cfg = bridge_db_store_.GetDb(bridge.bridge_db_id);
    }
    const OdooConfig& bot_cfg =
        bridge_cfg.has_value() ? *bridge_cfg : odoo_client_.GetConfig();

    // Check whether the Discord sender has a personal Odoo account linked to
    // the same database as this bridge.  If so, post through their account so
    // the message appears in Odoo under their own name.
    std::optional<OdooClient> sender_client;
    auto configs = user_store_.GetUserConfigs(event.msg.author.id);
    for (const auto& cfg : configs) {
      if (cfg.url == bot_cfg.url && cfg.database == bot_cfg.database) {
        sender_client.emplace(cfg);
        break;
      }
    }

    if (sender_client.has_value()) {
      // Post as the linked user.  Wrap content in a paragraph and append any
      // attachments so images, files, and audio round-trip to Odoo correctly.
      std::string body;
      if (!event.msg.content.empty()) {
        body = "<p>" + HtmlEscape(event.msg.content) + "</p>";
      }
      for (const auto& att : event.msg.attachments) {
        body += AttachmentHtml(att);
      }
      if (body.empty()) break;  // Nothing to post (e.g. empty system message).
      int posted_id =
          sender_client->PostDiscussMessage(bridge.odoo_channel_id, body);
      if (posted_id > 0) {
        std::lock_guard<std::mutex> lock(suppressed_mutex_);
        suppressed_odoo_ids_.insert(posted_id);
      }
    } else {
      // Post via the bridge bot account.  Prefix the message with the Discord
      // username so Odoo users can see who sent it.  HTML-escape both the
      // author name and the message content to prevent markup injection.
      OdooClient bridge_bot(bot_cfg);
      if (bridge_bot.IsConfigured()) {
        const std::string author = event.msg.author.username.empty()
                                       ? std::to_string(event.msg.author.id)
                                       : event.msg.author.username;
        std::string body;
        if (!event.msg.content.empty()) {
          body = "<p><strong>" + HtmlEscape(author) + "</strong>: " +
                 HtmlEscape(event.msg.content) + "</p>";
        } else {
          body = "<p><strong>" + HtmlEscape(author) + "</strong> shared:</p>";
        }
        for (const auto& att : event.msg.attachments) {
          body += AttachmentHtml(att);
        }
        int posted_id =
            bridge_bot.PostDiscussMessage(bridge.odoo_channel_id, body);
        if (posted_id > 0) {
          std::lock_guard<std::mutex> lock(suppressed_mutex_);
          suppressed_odoo_ids_.insert(posted_id);
        }
      }
    }
    break;
  }
}

// Odoo-Discord polling

void BreadyBot::PollOdooChannels() {
  while (running_) {
    auto bridges = bridge_store_.GetBridges();
    for (const auto& bridge : bridges) {
      // Resolve the Odoo credentials for this bridge.
      std::optional<OdooConfig> bridge_cfg;
      if (bridge.bridge_db_id >= 0) {
        bridge_cfg = bridge_db_store_.GetDb(bridge.bridge_db_id);
      }
      OdooClient* client_ptr = nullptr;
      std::optional<OdooClient> bridge_client;
      if (bridge_cfg.has_value()) {
        bridge_client.emplace(*bridge_cfg);
        client_ptr = &(*bridge_client);
      } else if (odoo_client_.IsConfigured()) {
        client_ptr = &odoo_client_;
      }
      if (client_ptr == nullptr) continue;

      // Get the bot's partner ID so we can suppress echo messages.
      // mail.message.author_id stores res.partner IDs, not res.users IDs,
      // so we must compare against GetPartnerUid() rather than GetUid().
      int bot_partner_id = client_ptr->GetPartnerUid();

      auto msgs = client_ptr->GetNewDiscussMessages(bridge.odoo_channel_id,
                                                    bridge.last_message_id);
      int highest_id = bridge.last_message_id;
      for (const auto& msg : msgs) {
        if (msg.id > highest_id) highest_id = msg.id;

        // Skip messages posted by this bridge's bot account to avoid echoing
        // back to Discord what the bot itself forwarded from Discord.
        if (bot_partner_id > 0 && msg.author_id == bot_partner_id) continue;

        // Also skip any message whose ID was recorded when we forwarded a
        // Discord message to Odoo (covers user-linked account posts too).
        {
          std::lock_guard<std::mutex> lock(suppressed_mutex_);
          if (suppressed_odoo_ids_.count(msg.id) > 0) {
            suppressed_odoo_ids_.erase(msg.id);
            continue;
          }
        }

        const std::string plain = StripHtml(msg.body);
        if (plain.empty() && msg.attachment_ids.empty()) continue;

        std::string discord_msg =
            plain.empty() ? ("**" + msg.author + "** shared:")
                          : ("**" + msg.author + "**: " + plain);

        // Append links for any files attached directly to the Odoo message.
        if (!msg.attachment_ids.empty()) {
          auto atts = client_ptr->GetAttachments(msg.attachment_ids);
          const std::string& base_url = client_ptr->GetConfig().url;
          for (const auto& att : atts) {
            const std::string url =
                base_url + "/web/content/" + std::to_string(att.id);
            discord_msg += "\n📎 [" + att.name + "](" + url + ")";
          }
        }
        bot_.message_create(
            dpp::message(bridge.discord_channel_id, discord_msg));
      }
      if (highest_id > bridge.last_message_id) {
        bridge_store_.UpdateLastMessageId(bridge.discord_channel_id,
                                          highest_id);
      }
    }

    // Sleep in small increments so the thread responds quickly to
    // running_ being set to false on shutdown.
    for (int i = 0; i < 100 && running_; ++i) {
      std::this_thread::sleep_for(kPollInterval / 100);
    }
  }
}

}  // namespace bready