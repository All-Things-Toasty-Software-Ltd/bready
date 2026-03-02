// Copyright 2026 All Things Toasty Software Ltd
//
// BridgeStore implementation – uses nlohmann/json for file I/O.

#include "bridge_store.h"

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

namespace bready {

BridgeStore::BridgeStore(const std::string& file_path)
    : file_path_(file_path) {
  Load();
}

void BridgeStore::Load() {
  std::ifstream file(file_path_);
  if (!file.is_open()) return;  // No saved data yet – start empty.

  try {
    nlohmann::json doc;
    file >> doc;
    if (!doc.is_array()) return;
    for (const auto& item : doc) {
      BridgeEntry entry;
      entry.discord_channel_id =
          dpp::snowflake(std::stoull(item.value("discord_channel_id", "0")));
      entry.odoo_channel_id = item.value("odoo_channel_id", 0);
      entry.last_message_id = item.value("last_message_id", 0);
      entry.bridge_db_id = item.value("bridge_db_id", -1);
      if (entry.discord_channel_id != dpp::snowflake{0} &&
          entry.odoo_channel_id > 0) {
        bridges_.push_back(entry);
      }
    }
  } catch (const nlohmann::json::exception& e) {
    std::cerr << "[bridge_store] Failed to load '" << file_path_
              << "': " << e.what() << "\n";
  }
}

void BridgeStore::Save() const {
  nlohmann::json doc = nlohmann::json::array();
  for (const auto& entry : bridges_) {
    doc.push_back(
        {{"discord_channel_id", std::to_string(entry.discord_channel_id)},
         {"odoo_channel_id", entry.odoo_channel_id},
         {"last_message_id", entry.last_message_id},
         {"bridge_db_id", entry.bridge_db_id}});
  }
  std::ofstream file(file_path_);
  if (!file.is_open()) {
    std::cerr << "[bridge_store] Cannot write to '" << file_path_ << "'\n";
    return;
  }
  file << doc.dump(2);
}

void BridgeStore::AddBridge(dpp::snowflake discord_channel_id,
                             int odoo_channel_id, int bridge_db_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  // Replace existing bridge for this Discord channel if present.
  for (auto& entry : bridges_) {
    if (entry.discord_channel_id == discord_channel_id) {
      entry.odoo_channel_id = odoo_channel_id;
      entry.last_message_id = 0;
      entry.bridge_db_id = bridge_db_id;
      Save();
      return;
    }
  }
  BridgeEntry entry;
  entry.discord_channel_id = discord_channel_id;
  entry.odoo_channel_id = odoo_channel_id;
  entry.last_message_id = 0;
  entry.bridge_db_id = bridge_db_id;
  bridges_.push_back(entry);
  Save();
}

void BridgeStore::RemoveBridge(dpp::snowflake discord_channel_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  bridges_.erase(
      std::remove_if(bridges_.begin(), bridges_.end(),
                     [discord_channel_id](const BridgeEntry& e) {
                       return e.discord_channel_id == discord_channel_id;
                     }),
      bridges_.end());
  Save();
}

bool BridgeStore::HasBridge(dpp::snowflake discord_channel_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto& entry : bridges_) {
    if (entry.discord_channel_id == discord_channel_id) return true;
  }
  return false;
}

std::vector<BridgeEntry> BridgeStore::GetBridges() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return bridges_;
}

void BridgeStore::UpdateLastMessageId(dpp::snowflake discord_channel_id,
                                      int last_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& entry : bridges_) {
    if (entry.discord_channel_id == discord_channel_id) {
      entry.last_message_id = last_id;
      Save();
      return;
    }
  }
}

}  // namespace bready