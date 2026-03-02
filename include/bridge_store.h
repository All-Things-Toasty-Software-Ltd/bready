// Copyright 2026 All Things Toasty Software Ltd
//
// BridgeStore: persists Discord-Odoo Discuss channel bridges.
// Each bridge links a Discord channel (by snowflake) to an Odoo mail.channel
// record (by integer ID).  The store also tracks the last forwarded Odoo
// message ID so the polling loop only fetches new messages.
// Thread-safe: all public methods are protected by an internal mutex.

#ifndef INCLUDE_BRIDGE_STORE_H_
#define INCLUDE_BRIDGE_STORE_H_

#include <dpp/dpp.h>

#include <mutex>
#include <string>
#include <vector>

namespace bready {

// A single Discord-Odoo channel bridge entry.
struct BridgeEntry {
  dpp::snowflake discord_channel_id;
  int odoo_channel_id{0};
  int last_message_id{0};  // Highest Odoo message ID already forwarded.
  // Index into the BridgeDbStore for bot credentials.
  // -1 means "use the shared bot config from environment variables".
  int bridge_db_id{-1};
};

// Manages persistent Discord↔Odoo Discuss channel bridges.
// All public methods are safe to call from multiple threads concurrently.
class BridgeStore {
 public:
  // Constructs a BridgeStore backed by |file_path|.
  // Loads existing data from the file if it exists.
  explicit BridgeStore(const std::string& file_path);

  // Adds (or replaces) a bridge between |discord_channel_id| and
  // |odoo_channel_id|, using the given |bridge_db_id| for Odoo credentials
  // (-1 = shared bot config).  Persists the change immediately.
  void AddBridge(dpp::snowflake discord_channel_id, int odoo_channel_id,
                 int bridge_db_id = -1);

  // Removes the bridge for |discord_channel_id|.  No-op if none exists.
  // Persists the change immediately.
  void RemoveBridge(dpp::snowflake discord_channel_id);

  // Returns true if a bridge exists for |discord_channel_id|.
  bool HasBridge(dpp::snowflake discord_channel_id) const;

  // Returns a snapshot of all bridges.
  std::vector<BridgeEntry> GetBridges() const;

  // Updates the last-forwarded Odoo message ID for |discord_channel_id|.
  // Persists the change immediately.
  void UpdateLastMessageId(dpp::snowflake discord_channel_id, int last_id);

 private:
  // Loads bridges from file_path_.  Called once from the constructor.
  void Load();

  // Serialises bridges_ to file_path_.  Must be called with mutex_ held.
  void Save() const;

  std::string file_path_;
  std::vector<BridgeEntry> bridges_;
  mutable std::mutex mutex_;
};

}  // namespace bready

#endif  // INCLUDE_BRIDGE_STORE_H_