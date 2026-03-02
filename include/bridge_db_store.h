// Copyright 2026 All Things Toasty Software Ltd
//
// BridgeDbStore: persists bot-level Odoo credentials for use with channel
// bridges.  An admin registers an Odoo instance once (with a dedicated bot
// account) and then references it by index when creating bridges.  Multiple
// bridges to the same Odoo database share the same credential entry.
// Thread-safe: all public methods are protected by an internal mutex.

#ifndef INCLUDE_BRIDGE_DB_STORE_H_
#define INCLUDE_BRIDGE_DB_STORE_H_

#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "odoo_client.h"

namespace bready {

    // Manages a persistent list of Odoo bot credentials used by channel bridges.
    // All public methods are safe to call from multiple threads concurrently.
    class BridgeDbStore {
    public:
        // Constructs a BridgeDbStore backed by |file_path|.
        // Loads existing data from the file if it exists.
        explicit BridgeDbStore(const std::string& file_path);

        // Appends |config| and returns the new index.  Persists immediately.
        int AddDb(const OdooConfig& config);

        // Removes the entry at |index|.  No-op if out of range.  Persists
        // immediately.
        void RemoveDb(int index);

        // Returns the OdooConfig at |index|, or nullopt if out of range.
        std::optional<OdooConfig> GetDb(int index) const;

        // Returns a snapshot of all entries.
        std::vector<OdooConfig> GetAllDbs() const;

        // Returns the number of registered databases.
        int Size() const;

    private:
        // Loads entries from file_path_.  Called once from the constructor.
        void Load();

        // Serialises dbs_ to file_path_.  Must be called with mutex_ held.
        void Save() const;

        std::string file_path_;
        std::vector<OdooConfig> dbs_;
        mutable std::mutex mutex_;
    };

}  // namespace bready

#endif  // INCLUDE_BRIDGE_DB_STORE_H_