// Copyright 2026 All Things Toasty Software Ltd
//
// Odoo Discord slash command handler implementations.

#include "odoo_commands.h"

#include <dpp/dpp.h>

#include <algorithm>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "bridge_db_store.h"
#include "bridge_store.h"
#include "odoo_client.h"
#include "user_store.h"

namespace bready {

namespace {

// Accent colour used by all Odoo embeds (Odoo brand purple).
constexpr uint32_t kOdooColor = 0x714B67;

// Ephemeral embed with an error title and message.
dpp::embed ErrorEmbed(const std::string& title, const std::string& msg) {
  dpp::embed embed;
  embed.set_title(title).set_description(msg).set_color(kOdooColor);
  return embed;
}

// Returns a nicely formatted embed for a list of OdooRecords.
// max_rows: how many rows to include before adding a "… and N more" footer.
dpp::embed RecordListEmbed(const std::string& title, const std::string& icon,
                           const std::vector<OdooRecord>& records,
                           const std::string& footer_hint) {
  dpp::embed embed;
  embed.set_title(icon + " " + title).set_color(kOdooColor);

  if (records.empty()) {
    embed.set_description("No records found.");
    embed.set_footer(dpp::embed_footer().set_text(footer_hint));
    return embed;
  }

  std::ostringstream desc;
  constexpr int kMaxRows = 20;
  int shown = 0;
  for (const auto& rec : records) {
    if (shown >= kMaxRows) break;
    desc << "**[" << rec.id << "] " << rec.name << "**";
    if (!rec.extra.empty()) {
      desc << "\n*" << rec.extra << "*";
    }
    desc << "\n\n";
    ++shown;
  }
  if (static_cast<int>(records.size()) > kMaxRows) {
    desc << "… and " << (records.size() - static_cast<std::size_t>(kMaxRows))
         << " more.";
  }
  embed.set_description(desc.str());
  embed.set_footer(dpp::embed_footer().set_text(footer_hint));
  return embed;
}

// Returns true when Odoo is not configured; replies with an ephemeral error.
bool ReplyIfNotConfigured(const dpp::slashcommand_t& event,
                          const OdooClient& client) {
  if (!client.IsConfigured()) {
    event.reply(
        dpp::message()
            .add_embed(ErrorEmbed(
                "⚠️ Odoo Not Configured",
                "The bot owner has not set up the Odoo connection yet.\n"
                "Please set the following environment variables and restart "
                "the bot:\n"
                "```\n"
                "ODOO_URL=https://yourcompany.odoo.com\n"
                "ODOO_DB=your_database\n"
                "ODOO_USER=you@example.com\n"
                "ODOO_API_KEY=your_api_key\n"
                "```"))
            .set_flags(dpp::m_ephemeral));
    return true;
  }
  return false;
}

// Merges |extra| records into |base|, skipping any ID already present.
void MergeRecords(std::vector<OdooRecord>& base,
                  const std::vector<OdooRecord>& extra) {
  std::set<int> seen;
  for (const auto& r : base) seen.insert(r.id);
  for (const auto& r : extra) {
    if (seen.insert(r.id).second) {
      base.push_back(r);
    }
  }
}

}  // namespace

// Public helper

std::string DiscordTag(dpp::snowflake discord_id) {
  return "[Discord:" + std::to_string(discord_id) + "]";
}

// Account-link commands

void HandleOdooLinkCommand(const dpp::slashcommand_t& event,
                           UserStore& user_store) {
  std::string url, database, username, api_key;
  if (std::holds_alternative<std::string>(event.get_parameter("url")))
    url = std::get<std::string>(event.get_parameter("url"));
  if (std::holds_alternative<std::string>(event.get_parameter("database")))
    database = std::get<std::string>(event.get_parameter("database"));
  if (std::holds_alternative<std::string>(event.get_parameter("username")))
    username = std::get<std::string>(event.get_parameter("username"));
  if (std::holds_alternative<std::string>(event.get_parameter("api_key")))
    api_key = std::get<std::string>(event.get_parameter("api_key"));

  if (url.empty() || database.empty() || username.empty() || api_key.empty()) {
    event.reply(
        dpp::message("❌ All four fields (url, database, username, api_key)"
                     " are required.")
            .set_flags(dpp::m_ephemeral));
    return;
  }

  // Validate the credentials by attempting authentication.
  OdooConfig cfg{url, database, username, api_key};
  OdooClient test_client(cfg);
  if (!test_client.ValidateCredentials()) {
    event.reply(
        dpp::message("❌ Could not authenticate with the provided credentials."
                     "  Please check your URL, database, username, and API "
                     "key.")
            .set_flags(dpp::m_ephemeral));
    return;
  }

  user_store.AddUserDb(event.command.usr.id, cfg);

  // Determine the index of the newly added DB.
  auto configs = user_store.GetUserConfigs(event.command.usr.id);
  int new_idx = static_cast<int>(configs.size()) - 1;
  bool is_first = (new_idx == 0);

  dpp::embed embed;
  embed.set_title("🔗 Odoo Account Linked")
      .set_color(kOdooColor)
      .set_description(
          is_first
              ? "Your Discord account has been linked to your Odoo "
                "account.  All Odoo commands will now use your "
                "personal credentials."
              : "A new Odoo instance has been added (index **" +
                    std::to_string(new_idx) +
                    "**).  Use **/odoo_db_set_primary** to make it the "
                    "default, or pass `db_id:" + std::to_string(new_idx) +
                    "` on any data command.")
      .add_field("Odoo URL", url, false)
      .add_field("Database", database, true)
      .add_field("Username", username, true)
      .add_field("DB index", std::to_string(new_idx), true)
      .set_footer(dpp::embed_footer().set_text(
          "Use /odoo_db_list to see all your linked instances."));
  event.reply(dpp::message().add_embed(embed).set_flags(dpp::m_ephemeral));
}

void HandleOdooUnlinkCommand(const dpp::slashcommand_t& event,
                             UserStore& user_store) {
  if (!user_store.IsLinked(event.command.usr.id)) {
    event.reply(dpp::message("ℹ️ You don't have a linked Odoo account.")
                    .set_flags(dpp::m_ephemeral));
    return;
  }
  user_store.UnlinkUser(event.command.usr.id);
  event.reply(
      dpp::message("✅ All your Odoo account links have been removed.  "
                   "Future Odoo commands will use the bot's shared account.")
          .set_flags(dpp::m_ephemeral));
}

void HandleOdooDbListCommand(const dpp::slashcommand_t& event,
                             const UserStore& user_store) {
  auto configs = user_store.GetUserConfigs(event.command.usr.id);
  if (configs.empty()) {
    event.reply(
        dpp::message(
            "ℹ️ You have no linked Odoo accounts.  Use **/odoo_link** to add "
            "one.")
            .set_flags(dpp::m_ephemeral));
    return;
  }

  int primary = user_store.GetPrimaryIndex(event.command.usr.id);
  std::ostringstream desc;
  for (int i = 0; i < static_cast<int>(configs.size()); ++i) {
    desc << "**[" << i << "]** " << configs[static_cast<std::size_t>(i)].url
         << " — `" << configs[static_cast<std::size_t>(i)].database << "`";
    if (i == primary) desc << "  ⭐ *primary*";
    desc << "\n";
  }

  dpp::embed embed;
  embed.set_title("🔗 Your Linked Odoo Instances")
      .set_color(kOdooColor)
      .set_description(desc.str())
      .set_footer(dpp::embed_footer().set_text(
          "Use /odoo_db_set_primary <db_id> to change the primary instance."));
  event.reply(dpp::message().add_embed(embed).set_flags(dpp::m_ephemeral));
}

void HandleOdooDbRemoveCommand(const dpp::slashcommand_t& event,
                               UserStore& user_store) {
  int64_t db_id_raw = -1;
  if (std::holds_alternative<int64_t>(event.get_parameter("db_id")))
    db_id_raw = std::get<int64_t>(event.get_parameter("db_id"));
  if (db_id_raw < 0) {
    event.reply(dpp::message("❌ Please provide a valid `db_id` (use "
                             "**/odoo_db_list** to see your indices).")
                    .set_flags(dpp::m_ephemeral));
    return;
  }
  int db_index = static_cast<int>(db_id_raw);
  auto configs = user_store.GetUserConfigs(event.command.usr.id);
  if (db_index >= static_cast<int>(configs.size())) {
    event.reply(dpp::message("❌ Invalid `db_id`.  Use **/odoo_db_list** to "
                             "see your current indices.")
                    .set_flags(dpp::m_ephemeral));
    return;
  }
  user_store.RemoveUserDb(event.command.usr.id, db_index);
  event.reply(dpp::message("✅ Odoo instance **" + std::to_string(db_index) +
                           "** has been removed.")
                  .set_flags(dpp::m_ephemeral));
}

void HandleOdooDbSetPrimaryCommand(const dpp::slashcommand_t& event,
                                   UserStore& user_store) {
  int64_t db_id_raw = -1;
  if (std::holds_alternative<int64_t>(event.get_parameter("db_id")))
    db_id_raw = std::get<int64_t>(event.get_parameter("db_id"));
  if (db_id_raw < 0) {
    event.reply(dpp::message("❌ Please provide a valid `db_id`.")
                    .set_flags(dpp::m_ephemeral));
    return;
  }
  int db_index = static_cast<int>(db_id_raw);
  auto configs = user_store.GetUserConfigs(event.command.usr.id);
  if (configs.empty()) {
    event.reply(dpp::message("ℹ️ You have no linked Odoo accounts.")
                    .set_flags(dpp::m_ephemeral));
    return;
  }
  if (db_index >= static_cast<int>(configs.size())) {
    event.reply(dpp::message("❌ Invalid `db_id`.  Use **/odoo_db_list** to "
                             "see your current indices.")
                    .set_flags(dpp::m_ephemeral));
    return;
  }
  user_store.SetPrimaryDb(event.command.usr.id, db_index);
  event.reply(dpp::message("✅ Primary Odoo instance set to **" +
                           std::to_string(db_index) + "** (" +
                           configs[static_cast<std::size_t>(db_index)].url +
                           ").")
                  .set_flags(dpp::m_ephemeral));
}

void HandleOdooLinkUserCommand(const dpp::slashcommand_t& event,
                               UserStore& user_store) {
  dpp::snowflake target_id{0};
  if (std::holds_alternative<dpp::snowflake>(event.get_parameter("user"))) {
    target_id = std::get<dpp::snowflake>(event.get_parameter("user"));
  }
  if (target_id == dpp::snowflake{0}) {
    event.reply(dpp::message("❌ A valid user must be specified.")
                    .set_flags(dpp::m_ephemeral));
    return;
  }

  std::string url, database, username, api_key;
  if (std::holds_alternative<std::string>(event.get_parameter("url")))
    url = std::get<std::string>(event.get_parameter("url"));
  if (std::holds_alternative<std::string>(event.get_parameter("database")))
    database = std::get<std::string>(event.get_parameter("database"));
  if (std::holds_alternative<std::string>(event.get_parameter("username")))
    username = std::get<std::string>(event.get_parameter("username"));
  if (std::holds_alternative<std::string>(event.get_parameter("api_key")))
    api_key = std::get<std::string>(event.get_parameter("api_key"));

  if (url.empty() || database.empty() || username.empty() || api_key.empty()) {
    event.reply(
        dpp::message("❌ All four fields (url, database, username, api_key)"
                     " are required.")
            .set_flags(dpp::m_ephemeral));
    return;
  }

  OdooConfig cfg{url, database, username, api_key};
  user_store.AddUserDb(target_id, cfg);

  dpp::embed embed;
  embed.set_title("🔗 Odoo Account Linked (Admin)")
      .set_color(kOdooColor)
      .set_description("Successfully linked <@" + std::to_string(target_id) +
                       "> to the provided Odoo account.")
      .add_field("Odoo URL", url, false)
      .add_field("Database", database, true)
      .add_field("Username", username, true);
  event.reply(dpp::message().add_embed(embed).set_flags(dpp::m_ephemeral));
}

void HandleOdooWhoamiCommand(const dpp::slashcommand_t& event,
                             const UserStore& user_store) {
  auto configs = user_store.GetUserConfigs(event.command.usr.id);
  int primary = user_store.GetPrimaryIndex(event.command.usr.id);

  dpp::embed embed;
  embed.set_color(kOdooColor);
  if (!configs.empty()) {
    embed.set_title("🔗 Linked Odoo Accounts");
    std::ostringstream desc;
    desc << "You have **" << configs.size() << "** linked Odoo instance"
         << (configs.size() == 1 ? "" : "s") << ".\n\n";
    for (int i = 0; i < static_cast<int>(configs.size()); ++i) {
      const auto& cfg = configs[static_cast<std::size_t>(i)];
      desc << "**[" << i << "]** " << cfg.url << " — `" << cfg.database << "`"
           << " (" << cfg.username << ")";
      if (i == primary) desc << "  ⭐ *primary*";
      desc << "\n";
    }
    embed.set_description(desc.str())
        .set_footer(dpp::embed_footer().set_text(
            "Use /odoo_db_set_primary to change the primary instance.  "
            "Pass db_id:<n> on any data command to target a specific one."));
  } else {
    embed.set_title("ℹ️ No Linked Odoo Account")
        .set_description(
            "You do not have a personal Odoo account linked.\n"
            "Odoo commands will use the bot's shared account.\n"
            "Records you create will be prefixed with your Discord ID.\n\n"
            "Use **/odoo_link** to link your own Odoo account.");
  }
  event.reply(dpp::message().add_embed(embed).set_flags(dpp::m_ephemeral));
}

// Data command handlers

void HandleOdooProjectsCommand(const dpp::slashcommand_t& event,
                               OdooClient& client,
                               const UserContext& /*user_ctx*/) {
  if (ReplyIfNotConfigured(event, client)) return;

  auto projects = client.GetProjects();
  event.reply(dpp::message().add_embed(RecordListEmbed(
      "Odoo Projects", "📁", projects,
      "Use /odoo_tasks [project] to view tasks for a project.")));
}

void HandleOdooTasksCommand(const dpp::slashcommand_t& event,
                            OdooClient& client, const UserContext& user_ctx) {
  if (ReplyIfNotConfigured(event, client)) return;

  std::string project;
  if (std::holds_alternative<std::string>(event.get_parameter("project"))) {
    project = std::get<std::string>(event.get_parameter("project"));
  }

  auto tasks = client.GetTasks(project);

  // For unlinked users: also surface any tasks they created via the bot
  // (identified by the [Discord:<id>] prefix in the task name).
  if (!user_ctx.is_linked) {
    auto tagged = client.GetTasksByNamePrefix(DiscordTag(user_ctx.discord_id));
    MergeRecords(tasks, tagged);
  }

  std::string title = project.empty() ? "Odoo Tasks" : "Tasks — " + project;
  event.reply(dpp::message().add_embed(RecordListEmbed(
      title, "✅", tasks, "Use /odoo_task_new to create a task.")));
}

void HandleOdooTaskNewCommand(const dpp::slashcommand_t& event,
                              OdooClient& client, const UserContext& user_ctx) {
  if (ReplyIfNotConfigured(event, client)) return;

  std::string title;
  if (std::holds_alternative<std::string>(event.get_parameter("title"))) {
    title = std::get<std::string>(event.get_parameter("title"));
  }
  if (title.empty()) {
    event.reply(dpp::message("❌ A task title is required.")
                    .set_flags(dpp::m_ephemeral));
    return;
  }

  std::string project;
  if (std::holds_alternative<std::string>(event.get_parameter("project"))) {
    project = std::get<std::string>(event.get_parameter("project"));
  }
  std::string description;
  if (std::holds_alternative<std::string>(event.get_parameter("description"))) {
    description = std::get<std::string>(event.get_parameter("description"));
  }

  // For unlinked users: prefix the title with their Discord tag so the
  // record can be identified and retrieved later.
  const std::string stored_title =
      user_ctx.is_linked ? title
                         : DiscordTag(user_ctx.discord_id) + " " + title;

  int new_id = client.CreateTask(stored_title, project, description);
  if (new_id < 0) {
    event.reply(dpp::message()
                    .add_embed(ErrorEmbed(
                        "❌ Failed to Create Task",
                        "Could not create the task in Odoo.  Check the bot "
                        "logs for details."))
                    .set_flags(dpp::m_ephemeral));
    return;
  }

  dpp::embed embed;
  embed.set_title("✅ Task Created")
      .set_color(kOdooColor)
      .add_field("Title", title, false);
  if (!project.empty()) embed.add_field("Project", project, true);
  if (!description.empty()) embed.add_field("Description", description, false);
  if (!user_ctx.is_linked) {
    embed.add_field("Note",
                    "Created via the shared bot account.  Your Discord ID "
                    "has been added as a prefix so you can find it later.",
                    false);
  }
  embed.set_footer(
      dpp::embed_footer().set_text("Task ID: " + std::to_string(new_id)));
  event.reply(dpp::message().add_embed(embed));
}

void HandleOdooTodosCommand(const dpp::slashcommand_t& event,
                            OdooClient& client, const UserContext& user_ctx) {
  if (ReplyIfNotConfigured(event, client)) return;

  auto todos = client.GetTodos();

  // For unlinked users: also surface their tagged to-dos.
  if (!user_ctx.is_linked) {
    auto tagged = client.GetTodosByNamePrefix(DiscordTag(user_ctx.discord_id));
    MergeRecords(todos, tagged);
  }

  event.reply(dpp::message().add_embed(RecordListEmbed(
      "My To-Dos", "📋", todos, "Use /odoo_todo_new to add a to-do.")));
}

void HandleOdooTodoNewCommand(const dpp::slashcommand_t& event,
                              OdooClient& client, const UserContext& user_ctx) {
  if (ReplyIfNotConfigured(event, client)) return;

  std::string title;
  if (std::holds_alternative<std::string>(event.get_parameter("title"))) {
    title = std::get<std::string>(event.get_parameter("title"));
  }
  if (title.empty()) {
    event.reply(dpp::message("❌ A to-do title is required.")
                    .set_flags(dpp::m_ephemeral));
    return;
  }

  // Prefix title for unlinked users.
  const std::string stored_title =
      user_ctx.is_linked ? title
                         : DiscordTag(user_ctx.discord_id) + " " + title;

  int new_id = client.CreateTodo(stored_title);
  if (new_id < 0) {
    event.reply(dpp::message()
                    .add_embed(ErrorEmbed(
                        "❌ Failed to Create To-Do",
                        "Could not create the to-do in Odoo.  Check the bot "
                        "logs for details."))
                    .set_flags(dpp::m_ephemeral));
    return;
  }

  dpp::embed embed;
  embed.set_title("✅ To-Do Created")
      .set_color(kOdooColor)
      .add_field("Title", title, false);
  if (!user_ctx.is_linked) {
    embed.add_field("Note",
                    "Created via the shared bot account.  Your Discord ID "
                    "has been added as a prefix so you can find it later.",
                    false);
  }
  embed.set_footer(
      dpp::embed_footer().set_text("To-Do ID: " + std::to_string(new_id)));
  event.reply(dpp::message().add_embed(embed));
}

void HandleOdooCrmCommand(const dpp::slashcommand_t& event, OdooClient& client,
                          const UserContext& /*user_ctx*/) {
  if (ReplyIfNotConfigured(event, client)) return;

  auto leads = client.GetCrmLeads();
  event.reply(dpp::message().add_embed(
      RecordListEmbed("CRM Leads & Opportunities", "💼", leads,
                      "Manage leads in full detail at your Odoo instance.")));
}

void HandleOdooNotesCommand(const dpp::slashcommand_t& event,
                            OdooClient& client,
                            const UserContext& /*user_ctx*/) {
  if (ReplyIfNotConfigured(event, client)) return;

  auto articles = client.GetKnowledgeArticles();
  event.reply(dpp::message().add_embed(
      RecordListEmbed("Knowledge Articles", "📚", articles,
                      "Open your Odoo Knowledge app for the full content.")));
}

// Bridge command handlers

void HandleBridgeDbAddCommand(const dpp::slashcommand_t& event,
                              BridgeDbStore& bridge_db_store) {
  std::string url, database, username, api_key;
  if (std::holds_alternative<std::string>(event.get_parameter("url")))
    url = std::get<std::string>(event.get_parameter("url"));
  if (std::holds_alternative<std::string>(event.get_parameter("database")))
    database = std::get<std::string>(event.get_parameter("database"));
  if (std::holds_alternative<std::string>(event.get_parameter("username")))
    username = std::get<std::string>(event.get_parameter("username"));
  if (std::holds_alternative<std::string>(event.get_parameter("api_key")))
    api_key = std::get<std::string>(event.get_parameter("api_key"));

  if (url.empty() || database.empty() || username.empty() || api_key.empty()) {
    event.reply(
        dpp::message("❌ All four fields (url, database, username, api_key)"
                     " are required.")
            .set_flags(dpp::m_ephemeral));
    return;
  }

  OdooConfig cfg{url, database, username, api_key};
  OdooClient test_client(cfg);
  if (!test_client.ValidateCredentials()) {
    event.reply(
        dpp::message("❌ Could not authenticate with the provided credentials."
                     " Please check your URL, database, username, and API "
                     "key.")
            .set_flags(dpp::m_ephemeral));
    return;
  }

  int new_id = bridge_db_store.AddDb(cfg);

  dpp::embed embed;
  embed.set_title("🗄️ Bridge Database Registered")
      .set_color(kOdooColor)
      .set_description("The Odoo database has been registered and can now be "
                       "used when creating channel bridges.")
      .add_field("Bridge DB ID", std::to_string(new_id), true)
      .add_field("Odoo URL", url, false)
      .add_field("Database", database, true)
      .add_field("Bot Username", username, true)
      .set_footer(dpp::embed_footer().set_text(
          "Use /bridge_create with bridge_db_id:" + std::to_string(new_id) +
          " to target this database."));
  event.reply(dpp::message().add_embed(embed).set_flags(dpp::m_ephemeral));
}

void HandleBridgeDbListCommand(const dpp::slashcommand_t& event,
                               const BridgeDbStore& bridge_db_store) {
  auto dbs = bridge_db_store.GetAllDbs();
  if (dbs.empty()) {
    event.reply(
        dpp::message(
            "ℹ️ No bridge databases registered.  Use **/bridge_db_add** to "
            "register one.")
            .set_flags(dpp::m_ephemeral));
    return;
  }

  std::ostringstream desc;
  for (int i = 0; i < static_cast<int>(dbs.size()); ++i) {
    const auto& cfg = dbs[static_cast<std::size_t>(i)];
    desc << "**[" << i << "]** " << cfg.url << " — `" << cfg.database
         << "` (" << cfg.username << ")\n";
  }

  dpp::embed embed;
  embed.set_title("🗄️ Registered Bridge Databases")
      .set_color(kOdooColor)
      .set_description(desc.str())
      .set_footer(dpp::embed_footer().set_text(
          "Use /bridge_create with bridge_db_id:<n> to target a database."));
  event.reply(dpp::message().add_embed(embed).set_flags(dpp::m_ephemeral));
}

void HandleBridgeDbRemoveCommand(const dpp::slashcommand_t& event,
                                 BridgeDbStore& bridge_db_store) {
  int64_t db_id_raw = -1;
  if (std::holds_alternative<int64_t>(event.get_parameter("bridge_db_id")))
    db_id_raw = std::get<int64_t>(event.get_parameter("bridge_db_id"));
  if (db_id_raw < 0) {
    event.reply(
        dpp::message("❌ Please provide a valid `bridge_db_id` (use "
                     "**/bridge_db_list** to see registered databases).")
            .set_flags(dpp::m_ephemeral));
    return;
  }
  int index = static_cast<int>(db_id_raw);
  if (!bridge_db_store.GetDb(index).has_value()) {
    event.reply(dpp::message("❌ Invalid `bridge_db_id`.  Use "
                             "**/bridge_db_list** to see valid indices.")
                    .set_flags(dpp::m_ephemeral));
    return;
  }
  bridge_db_store.RemoveDb(index);
  event.reply(
      dpp::message("✅ Bridge database **" + std::to_string(index) +
                   "** has been removed.")
          .set_flags(dpp::m_ephemeral));
}

void HandleBridgeCreateCommand(const dpp::slashcommand_t& event,
                               OdooClient& shared_odoo_client,
                               BridgeStore& bridge_store,
                               BridgeDbStore& bridge_db_store) {
  dpp::snowflake discord_channel_id{0};
  if (std::holds_alternative<dpp::snowflake>(
          event.get_parameter("discord_channel"))) {
    discord_channel_id =
        std::get<dpp::snowflake>(event.get_parameter("discord_channel"));
  }
  if (discord_channel_id == dpp::snowflake{0}) {
    event.reply(dpp::message("❌ A valid Discord channel must be specified.")
                    .set_flags(dpp::m_ephemeral));
    return;
  }

  std::string odoo_channel_name;
  if (std::holds_alternative<std::string>(
          event.get_parameter("odoo_channel"))) {
    odoo_channel_name =
        std::get<std::string>(event.get_parameter("odoo_channel"));
  }
  if (odoo_channel_name.empty()) {
    event.reply(
        dpp::message("❌ An Odoo Discuss channel name must be specified.")
            .set_flags(dpp::m_ephemeral));
    return;
  }

  // Resolve which OdooClient to use for channel lookup.
  int bridge_db_id = -1;
  if (std::holds_alternative<int64_t>(event.get_parameter("bridge_db_id"))) {
    bridge_db_id =
        static_cast<int>(std::get<int64_t>(event.get_parameter("bridge_db_id")));
  }

  std::optional<OdooClient> db_client;
  OdooClient* lookup_client = &shared_odoo_client;
  if (bridge_db_id >= 0) {
    auto cfg = bridge_db_store.GetDb(bridge_db_id);
    if (!cfg.has_value()) {
      event.reply(dpp::message("❌ Invalid `bridge_db_id`.  Use "
                               "**/bridge_db_list** to see valid indices.")
                      .set_flags(dpp::m_ephemeral));
      return;
    }
    db_client.emplace(*cfg);
    lookup_client = &(*db_client);
  }

  if (ReplyIfNotConfigured(event, *lookup_client)) return;

  int odoo_channel_id = lookup_client->FindDiscussChannel(odoo_channel_name);
  if (odoo_channel_id < 0) {
    event.reply(
        dpp::message("❌ Could not find an Odoo Discuss channel named **" +
                     odoo_channel_name +
                     "**.  Check the name and try again.")
            .set_flags(dpp::m_ephemeral));
    return;
  }

  bridge_store.AddBridge(discord_channel_id, odoo_channel_id, bridge_db_id);

  std::string db_label = bridge_db_id >= 0
                             ? "bridge database **" + std::to_string(bridge_db_id) + "**"
                             : "the shared bot Odoo account";

  dpp::embed embed;
  embed.set_title("🌉 Bridge Created")
      .set_color(kOdooColor)
      .set_description("Messages in <#" +
                       std::to_string(discord_channel_id) +
                       "> will now be bridged to the Odoo Discuss channel **" +
                       odoo_channel_name + "** (ID: " +
                       std::to_string(odoo_channel_id) + ") using " +
                       db_label + ".")
      .set_footer(dpp::embed_footer().set_text(
          "Use /bridge_delete to remove this bridge."));
  event.reply(dpp::message().add_embed(embed).set_flags(dpp::m_ephemeral));
}

void HandleBridgeDeleteCommand(const dpp::slashcommand_t& event,
                               BridgeStore& bridge_store) {
  dpp::snowflake discord_channel_id{0};
  if (std::holds_alternative<dpp::snowflake>(
          event.get_parameter("discord_channel"))) {
    discord_channel_id =
        std::get<dpp::snowflake>(event.get_parameter("discord_channel"));
  }
  if (discord_channel_id == dpp::snowflake{0}) {
    event.reply(dpp::message("❌ A valid Discord channel must be specified.")
                    .set_flags(dpp::m_ephemeral));
    return;
  }

  if (!bridge_store.HasBridge(discord_channel_id)) {
    event.reply(dpp::message("ℹ️ No bridge exists for <#" +
                             std::to_string(discord_channel_id) + ">.")
                    .set_flags(dpp::m_ephemeral));
    return;
  }

  bridge_store.RemoveBridge(discord_channel_id);
  event.reply(dpp::message("✅ Bridge for <#" +
                           std::to_string(discord_channel_id) +
                           "> has been removed.")
                  .set_flags(dpp::m_ephemeral));
}

void HandleBridgeListCommand(const dpp::slashcommand_t& event,
                             const BridgeStore& bridge_store) {
  auto bridges = bridge_store.GetBridges();
  if (bridges.empty()) {
    event.reply(
        dpp::message(
            "ℹ️ No bridges are currently configured.  Use **/bridge_create** "
            "to add one.")
            .set_flags(dpp::m_ephemeral));
    return;
  }

  std::ostringstream desc;
  for (const auto& b : bridges) {
    desc << "<#" << b.discord_channel_id << "> ↔ Odoo channel ID **"
         << b.odoo_channel_id << "**\n";
  }

  dpp::embed embed;
  embed.set_title("🌉 Active Bridges")
      .set_color(kOdooColor)
      .set_description(desc.str())
      .set_footer(dpp::embed_footer().set_text(
          "Use /bridge_delete <channel> to remove a bridge."));
  event.reply(dpp::message().add_embed(embed).set_flags(dpp::m_ephemeral));
}

}  // namespace bready