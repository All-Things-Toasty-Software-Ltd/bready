// Copyright 2026 All Things Toasty Software Ltd
//
// Entry point for Bready – a Discord bot for Odoo integration, maintained by
// All Things Toasty Software Ltd.
//
// Usage:
//   export DISCORD_BOT_TOKEN="your_token_here"
//   # Odoo integration (Odoo 16-19, SaaS or self-hosted):
//   export ODOO_URL="https://yourcompany.odoo.com"
//   export ODOO_DB="your_database"
//   export ODOO_USER="you@example.com"
//   export ODOO_API_KEY="your_api_key"
//   # Role-based access control:
//   #   Admin role – bridge management, linking other users, all commands.
//   #   Member role – data commands + linking own account.
//   #   Users with neither role cannot use the bot.
//   export DISCORD_ADMIN_ROLE_ID="123456789012345678"
//   export DISCORD_MEMBER_ROLE_ID="987654321098765432"
//   # Optional: path for the user-account-link store (default: user_links.json)
//   export BOT_DATA_PATH="/path/to/user_links.json"
//   # Optional: path for the bridge store (default: bridges.json)
//   export BOT_BRIDGE_DATA_PATH="/path/to/bridges.json"
//   # Optional: path for the bridge-database credential store (default:
//   bridge_dbs.json) export BOT_BRIDGE_DB_DATA_PATH="/path/to/bridge_dbs.json"
//   ./bready

#include <cstdlib>
#include <iostream>
#include <string>

#include "bready_bot.h"
#include "odoo_client.h"

int main() {
  const char* token_env = std::getenv("DISCORD_BOT_TOKEN");
  if (token_env == nullptr || std::string(token_env).empty()) {
    std::cerr << "Error: DISCORD_BOT_TOKEN environment variable is not set.\n"
              << "Set it and try again:\n"
              << "  export DISCORD_BOT_TOKEN=\"your_token_here\"\n";
    return 1;
  }

  bready::OdooConfig odoo_cfg = bready::LoadOdooConfigFromEnv();

  // Path for the persistent Discord-Odoo user-link store.
  std::string user_store_path = "user_links.json";
  const char* data_path_env = std::getenv("BOT_DATA_PATH");
  if (data_path_env != nullptr && data_path_env[0] != '\0') {
    user_store_path = data_path_env;
  }

  // Path for the persistent channel-bridge store.
  std::string bridge_store_path = "bridges.json";
  const char* bridge_path_env = std::getenv("BOT_BRIDGE_DATA_PATH");
  if (bridge_path_env != nullptr && bridge_path_env[0] != '\0') {
    bridge_store_path = bridge_path_env;
  }

  // Path for the persistent bridge-database credential store.
  std::string bridge_db_store_path = "bridge_dbs.json";
  const char* bridge_db_path_env = std::getenv("BOT_BRIDGE_DB_DATA_PATH");
  if (bridge_db_path_env != nullptr && bridge_db_path_env[0] != '\0') {
    bridge_db_store_path = bridge_db_path_env;
  }

  // Helper lambda to parse a snowflake from an env var string.
  auto parse_snowflake = [](const char* env_val,
                            const char* var_name) -> dpp::snowflake {
    if (env_val == nullptr || env_val[0] == '\0') return dpp::snowflake{0};
    try {
      return dpp::snowflake(std::stoull(env_val));
    } catch (...) {
      std::cerr << "Warning: " << var_name
                << " is not a valid snowflake — role gate disabled for that "
                   "role.\n";
      return dpp::snowflake{0};
    }
  };

  // Role IDs that control bot access.
  dpp::snowflake admin_role_id = parse_snowflake(
      std::getenv("DISCORD_ADMIN_ROLE_ID"), "DISCORD_ADMIN_ROLE_ID");
  dpp::snowflake member_role_id = parse_snowflake(
      std::getenv("DISCORD_MEMBER_ROLE_ID"), "DISCORD_MEMBER_ROLE_ID");

  bready::BreadyBot bot(token_env, odoo_cfg, user_store_path, bridge_store_path,
                        bridge_db_store_path, admin_role_id, member_role_id);
  bot.Run();
  return 0;
}