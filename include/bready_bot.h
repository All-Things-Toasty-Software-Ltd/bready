// Copyright 2026 All Things Toasty Software Ltd
//
// BreadyBot class: manages the Discord bot lifecycle, registers Odoo slash
// commands, and dispatches incoming slash command interactions.

#ifndef INCLUDE_BREADY_BOT_H_
#define INCLUDE_BREADY_BOT_H_

#include <dpp/dpp.h>

#include <atomic>
#include <mutex>
#include <optional>
#include <set>
#include <thread>

#include "bridge_db_store.h"
#include "bridge_store.h"
#include "odoo_client.h"
#include "odoo_commands.h"
#include "user_store.h"

namespace bready {

// BreadyBot owns the dpp::cluster and manages all bot state.
//
// Two Discord roles control access:
//   admin_role_id – full access: bridges, linking other users, all commands.
//   member_role_id – basic access: data commands + linking own account.
//   (either ID = 0 means that role gate is disabled)
//   A user that holds neither role cannot use the bot at all.
//
// Usage:
//   OdooConfig cfg = LoadOdooConfigFromEnv();
//   BreadyBot bot("TOKEN", cfg,
//                 "user_links.json", "bridges.json", "bridge_dbs.json",
//                 admin_role_id, member_role_id);
//   bot.Run();
class BreadyBot {
 public:
  BreadyBot(const std::string& token, const OdooConfig& odoo_config,
            const std::string& user_store_path,
            const std::string& bridge_store_path,
            const std::string& bridge_db_store_path,
            dpp::snowflake admin_role_id, dpp::snowflake member_role_id);

  ~BreadyBot();

  // Starts the bot, launches the Odoo-polling thread, and blocks until the
  // process exits.
  void Run();

 private:
  // Access level returned by GetAccessLevel().
  enum class AccessLevel { kNone, kMember, kAdmin };

  // Registers all slash commands with Discord on the ready event.
  void RegisterCommands();

  // Dispatches a slash command interaction to the appropriate handler.
  void OnSlashCommand(const dpp::slashcommand_t& event);

  // Handles incoming guild messages; forwards bridged-channel messages to
  // the corresponding Odoo Discuss channel.
  void OnMessageCreate(const dpp::message_create_t& event);

  // Background thread: polls Odoo Discuss channels for new messages and
  // forwards them to the corresponding Discord channels.
  void PollOdooChannels();

  // Returns the effective access level of the member who sent |event|.
  AccessLevel GetAccessLevel(const dpp::slashcommand_t& event) const;

  // Replies with an ephemeral error and returns false when the invoker does
  // not hold at least the member role.
  bool RequireMemberAccess(const dpp::slashcommand_t& event) const;

  // Replies with an ephemeral error and returns false when the invoker does
  // not hold the admin role.
  bool RequireAdminAccess(const dpp::slashcommand_t& event) const;

  // Builds a UserContext for the member who sent |event|, including any
  // db_id parameter they may have supplied.
  UserContext GetUserContext(const dpp::slashcommand_t& event) const;

  dpp::cluster bot_;
  OdooClient odoo_client_;
  UserStore user_store_;
  BridgeStore bridge_store_;
  BridgeDbStore bridge_db_store_;
  dpp::snowflake admin_role_id_;   // 0 means no admin-role restriction.
  dpp::snowflake member_role_id_;  // 0 means no member-role restriction.

  std::thread poll_thread_;
  std::atomic<bool> running_{false};

  // Message IDs of Odoo messages posted by this bot (Discord→Odoo).
  // The polling loop skips these IDs to prevent echo loops.
  mutable std::mutex suppressed_mutex_;
  std::set<int> suppressed_odoo_ids_;
};

}  // namespace bready

#endif  // INCLUDE_BREADY_BOT_H_