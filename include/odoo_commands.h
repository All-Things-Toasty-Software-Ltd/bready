// Copyright 2026 All Things Toasty Software Ltd
//
// Handlers for Odoo-integration Discord slash commands.
// Requires an OdooClient that has been initialised with valid config.

#ifndef INCLUDE_ODOO_COMMANDS_H_
#define INCLUDE_ODOO_COMMANDS_H_

#include <dpp/dpp.h>

#include "bridge_db_store.h"
#include "bridge_store.h"
#include "odoo_client.h"
#include "user_store.h"

namespace bready {

// Identifies the Discord user who invoked a command and whether they have
// a linked Odoo account.
struct UserContext {
  dpp::snowflake discord_id;
  bool is_linked;   // true when the user has at least one personal Odoo account
                    // linked.
  int db_index{-1};  // -1 = use primary; >= 0 = specific DB index.
};

// Returns the Discord-tag prefix used when creating records on behalf of an
// unlinked user, e.g. "[Discord:123456789012345678]".
std::string DiscordTag(dpp::snowflake discord_id);

// Account-link commands (available to any allowed member)

// Handles /odoo_link – adds a new linked Odoo account for the invoking user.
void HandleOdooLinkCommand(const dpp::slashcommand_t& event,
                           UserStore& user_store);

// Handles /odoo_unlink – removes all linked Odoo accounts for the invoking
// user.
void HandleOdooUnlinkCommand(const dpp::slashcommand_t& event,
                             UserStore& user_store);

// Handles /odoo_db_list – lists all Odoo instances linked by the invoking user.
void HandleOdooDbListCommand(const dpp::slashcommand_t& event,
                             const UserStore& user_store);

// Handles /odoo_db_remove <db_id> – removes a specific linked Odoo instance.
void HandleOdooDbRemoveCommand(const dpp::slashcommand_t& event,
                               UserStore& user_store);

// Handles /odoo_db_set_primary <db_id> – sets which linked Odoo instance is
// the primary (default) one.
void HandleOdooDbSetPrimaryCommand(const dpp::slashcommand_t& event,
                                   UserStore& user_store);

// Handles /odoo_link_user – admin command to link another user's account.
// Caller must have already verified that the invoker holds the admin role.
void HandleOdooLinkUserCommand(const dpp::slashcommand_t& event,
                               UserStore& user_store);

// Handles /odoo_whoami – shows the invoking user's link status (ephemeral).
void HandleOdooWhoamiCommand(const dpp::slashcommand_t& event,
                             const UserStore& user_store);

// Data commands

// Handles /odoo_projects – lists the first 25 Odoo projects.
void HandleOdooProjectsCommand(const dpp::slashcommand_t& event,
                               OdooClient& client, const UserContext& user_ctx);

// Handles /odoo_tasks [project] – lists tasks, optionally by project.
// For unlinked users also surfaces records tagged with their Discord ID.
void HandleOdooTasksCommand(const dpp::slashcommand_t& event,
                            OdooClient& client, const UserContext& user_ctx);

// Handles /odoo_task_new <title> [project] [description] – creates a task.
// For unlinked users the title is automatically prefixed with their Discord
// tag so the record can be retrieved later.
void HandleOdooTaskNewCommand(const dpp::slashcommand_t& event,
                              OdooClient& client, const UserContext& user_ctx);

// Handles /odoo_todos – lists personal to-dos.
// For unlinked users also surfaces records tagged with their Discord ID.
void HandleOdooTodosCommand(const dpp::slashcommand_t& event,
                            OdooClient& client, const UserContext& user_ctx);

// Handles /odoo_todo_new <title> – creates a personal to-do.
// For unlinked users the title is automatically prefixed with their Discord
// tag.
void HandleOdooTodoNewCommand(const dpp::slashcommand_t& event,
                              OdooClient& client, const UserContext& user_ctx);

// Handles /odoo_crm – lists CRM leads / opportunities.
void HandleOdooCrmCommand(const dpp::slashcommand_t& event, OdooClient& client,
                          const UserContext& user_ctx);

// Handles /odoo_notes – lists Knowledge articles.
void HandleOdooNotesCommand(const dpp::slashcommand_t& event,
                            OdooClient& client, const UserContext& user_ctx);

// Bridge commands (admin-only)

// Handles /bridge_db_add <url> <database> <username> <api_key> – registers
// bot-level Odoo credentials for use with channel bridges.
void HandleBridgeDbAddCommand(const dpp::slashcommand_t& event,
                              BridgeDbStore& bridge_db_store);

// Handles /bridge_db_list – lists all registered bridge databases.
void HandleBridgeDbListCommand(const dpp::slashcommand_t& event,
                               const BridgeDbStore& bridge_db_store);

// Handles /bridge_db_remove <bridge_db_id> – removes a registered bridge
// database.
void HandleBridgeDbRemoveCommand(const dpp::slashcommand_t& event,
                                 BridgeDbStore& bridge_db_store);

// Handles /bridge_create <discord_channel> <odoo_channel> [bridge_db_id] –
// creates a Discord↔Odoo Discuss message bridge.  Uses the BridgeDbStore
// entry at bridge_db_id for Odoo credentials, or the shared bot config if
// bridge_db_id is -1 (the default).
void HandleBridgeCreateCommand(const dpp::slashcommand_t& event,
                               OdooClient& shared_odoo_client,
                               BridgeStore& bridge_store,
                               BridgeDbStore& bridge_db_store);

// Handles /bridge_delete <discord_channel> – removes an existing bridge.
void HandleBridgeDeleteCommand(const dpp::slashcommand_t& event,
                               BridgeStore& bridge_store);

// Handles /bridge_list – lists all active bridges.
void HandleBridgeListCommand(const dpp::slashcommand_t& event,
                             const BridgeStore& bridge_store);

}  // namespace bready

#endif  // INCLUDE_ODOO_COMMANDS_H_