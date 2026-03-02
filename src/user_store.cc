// Copyright 2026 All Things Toasty Software Ltd
//
// UserStore implementation – uses nlohmann/json for file I/O.
// Supports multiple linked Odoo instances per user with a primary index.
// Backward-compatible with the old single-config JSON format.

#include "user_store.h"

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

namespace bready {

UserStore::UserStore(const std::string& file_path) : file_path_(file_path) {
  Load();
}

void UserStore::Load() {
  std::ifstream file(file_path_);
  if (!file.is_open()) return;  // No saved data yet – start empty.

  try {
    nlohmann::json doc;
    file >> doc;
    for (auto& [key, val] : doc.items()) {
      UserDbEntry entry;
      if (val.contains("configs") && val["configs"].is_array()) {
        // New multi-DB format.
        for (const auto& cfg_json : val["configs"]) {
          OdooConfig cfg;
          cfg.url = cfg_json.value("url", "");
          cfg.database = cfg_json.value("database", "");
          cfg.username = cfg_json.value("username", "");
          cfg.api_key = cfg_json.value("api_key", "");
          entry.configs.push_back(std::move(cfg));
        }
        entry.primary_index = val.value("primary_index", 0);
      } else if (val.contains("url")) {
        // Old single-config format — migrate transparently.
        OdooConfig cfg;
        cfg.url = val.value("url", "");
        cfg.database = val.value("database", "");
        cfg.username = val.value("username", "");
        cfg.api_key = val.value("api_key", "");
        entry.configs.push_back(std::move(cfg));
        entry.primary_index = 0;
      }
      if (!entry.configs.empty()) {
        links_[key] = std::move(entry);
      }
    }
  } catch (const nlohmann::json::exception& e) {
    std::cerr << "[user_store] Failed to load '" << file_path_
              << "': " << e.what() << "\n";
  }
}

void UserStore::Save() const {
  nlohmann::json doc;
  for (const auto& [key, entry] : links_) {
    nlohmann::json configs_arr = nlohmann::json::array();
    for (const auto& cfg : entry.configs) {
      configs_arr.push_back({{"url", cfg.url},
                             {"database", cfg.database},
                             {"username", cfg.username},
                             {"api_key", cfg.api_key}});
    }
    doc[key] = {{"configs", configs_arr},
                {"primary_index", entry.primary_index}};
  }
  std::ofstream file(file_path_);
  if (!file.is_open()) {
    std::cerr << "[user_store] Cannot write to '" << file_path_ << "'\n";
    return;
  }
  file << doc.dump(2);
}

std::optional<OdooConfig> UserStore::GetUserConfig(
    dpp::snowflake discord_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = links_.find(std::to_string(discord_id));
  if (it == links_.end() || it->second.configs.empty()) return std::nullopt;
  int idx = it->second.primary_index;
  if (idx < 0 || idx >= static_cast<int>(it->second.configs.size())) idx = 0;
  return it->second.configs[static_cast<std::size_t>(idx)];
}

std::optional<OdooConfig> UserStore::GetUserConfigAt(
    dpp::snowflake discord_id, int db_index) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = links_.find(std::to_string(discord_id));
  if (it == links_.end()) return std::nullopt;
  if (db_index < 0 ||
      db_index >= static_cast<int>(it->second.configs.size()))
    return std::nullopt;
  return it->second.configs[static_cast<std::size_t>(db_index)];
}

std::vector<OdooConfig> UserStore::GetUserConfigs(
    dpp::snowflake discord_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = links_.find(std::to_string(discord_id));
  if (it == links_.end()) return {};
  return it->second.configs;
}

int UserStore::GetPrimaryIndex(dpp::snowflake discord_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = links_.find(std::to_string(discord_id));
  if (it == links_.end()) return 0;
  return it->second.primary_index;
}

bool UserStore::IsLinked(dpp::snowflake discord_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = links_.find(std::to_string(discord_id));
  return it != links_.end() && !it->second.configs.empty();
}

void UserStore::AddUserDb(dpp::snowflake discord_id, const OdooConfig& config) {
  std::lock_guard<std::mutex> lock(mutex_);
  links_[std::to_string(discord_id)].configs.push_back(config);
  Save();
}

void UserStore::RemoveUserDb(dpp::snowflake discord_id, int db_index) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = links_.find(std::to_string(discord_id));
  if (it == links_.end()) return;
  auto& entry = it->second;
  if (db_index < 0 || db_index >= static_cast<int>(entry.configs.size()))
    return;
  entry.configs.erase(entry.configs.begin() + db_index);
  if (entry.configs.empty()) {
    links_.erase(it);
  } else {
    if (entry.primary_index >= static_cast<int>(entry.configs.size())) {
      entry.primary_index = 0;
    }
  }
  Save();
}

void UserStore::SetPrimaryDb(dpp::snowflake discord_id, int db_index) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = links_.find(std::to_string(discord_id));
  if (it == links_.end()) return;
  if (db_index < 0 ||
      db_index >= static_cast<int>(it->second.configs.size()))
    return;
  it->second.primary_index = db_index;
  Save();
}

void UserStore::LinkUser(dpp::snowflake discord_id, const OdooConfig& config) {
  std::lock_guard<std::mutex> lock(mutex_);
  UserDbEntry entry;
  entry.configs.push_back(config);
  entry.primary_index = 0;
  links_[std::to_string(discord_id)] = std::move(entry);
  Save();
}

void UserStore::UnlinkUser(dpp::snowflake discord_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  links_.erase(std::to_string(discord_id));
  Save();
}

}  // namespace bready