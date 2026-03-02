// Copyright 2026 All Things Toasty Software Ltd
//
// UserStore: maps Discord user IDs to one or more personal Odoo account
// credentials.  Each user may link multiple Odoo instances and designate one
// as the primary.  Persists the mapping to a JSON file for durability across
// restarts.  Thread-safe: all public methods are protected by an internal
// mutex.

#ifndef INCLUDE_USER_STORE_H_
#define INCLUDE_USER_STORE_H_

#include <dpp/dpp.h>

#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "odoo_client.h"

namespace bready {

// Holds all Odoo instances linked by a single Discord user.
struct UserDbEntry {
  std::vector<OdooConfig> configs;
  int primary_index{0};  // Index into configs of the primary instance.
};

// Manages persistent Discord→Odoo account links.
// A user may have multiple linked Odoo instances.
// All public methods are safe to call from multiple threads concurrently.
class UserStore {
 public:
  // Constructs a UserStore backed by |file_path|.
  // Loads existing data from the file if it exists.
  explicit UserStore(const std::string& file_path);

  // Returns the primary OdooConfig for |discord_id|, or nullopt if not linked.
  std::optional<OdooConfig> GetUserConfig(dpp::snowflake discord_id) const;

  // Returns the OdooConfig at |db_index| for |discord_id|, or nullopt if the
  // index is out of range or the user has no linked accounts.
  std::optional<OdooConfig> GetUserConfigAt(dpp::snowflake discord_id,
                                            int db_index) const;

  // Returns all OdooConfigs linked to |discord_id|.
  std::vector<OdooConfig> GetUserConfigs(dpp::snowflake discord_id) const;

  // Returns the current primary index for |discord_id| (0 if not set).
  int GetPrimaryIndex(dpp::snowflake discord_id) const;

  // Returns true if |discord_id| has at least one linked Odoo account.
  bool IsLinked(dpp::snowflake discord_id) const;

  // Appends |config| as a new linked Odoo instance for |discord_id|.
  // The first config added becomes the primary.  Persists immediately.
  void AddUserDb(dpp::snowflake discord_id, const OdooConfig& config);

  // Removes the config at |db_index| for |discord_id|.
  // If the primary is removed the primary index resets to 0.  Persists
  // immediately.
  void RemoveUserDb(dpp::snowflake discord_id, int db_index);

  // Sets the primary config index for |discord_id|.  Persists immediately.
  void SetPrimaryDb(dpp::snowflake discord_id, int db_index);

  // Legacy helper: replaces all existing links with a single |config|.
  // Persists the change immediately.
  void LinkUser(dpp::snowflake discord_id, const OdooConfig& config);

  // Removes all links for |discord_id|.  Persists the change immediately.
  void UnlinkUser(dpp::snowflake discord_id);

 private:
  // Loads links from file_path_.  Called once from the constructor.
  void Load();

  // Serialises links_ to file_path_.  Must be called with mutex_ held.
  void Save() const;

  std::string file_path_;
  // Key is the Discord snowflake rendered as a decimal string.
  std::map<std::string, UserDbEntry> links_;
  mutable std::mutex mutex_;
};

}  // namespace bready

#endif  // INCLUDE_USER_STORE_H_