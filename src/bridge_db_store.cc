// Copyright 2026 All Things Toasty Software Ltd
//
// BridgeDbStore implementation – uses nlohmann/json for file I/O.

#include "bridge_db_store.h"

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

namespace bready {

BridgeDbStore::BridgeDbStore(const std::string& file_path)
    : file_path_(file_path) {
  Load();
}

void BridgeDbStore::Load() {
  std::ifstream file(file_path_);
  if (!file.is_open()) return;  // No saved data yet – start empty.

  try {
    nlohmann::json doc;
    file >> doc;
    if (!doc.is_array()) return;
    for (const auto& item : doc) {
      OdooConfig cfg;
      cfg.url = item.value("url", "");
      cfg.database = item.value("database", "");
      cfg.username = item.value("username", "");
      cfg.api_key = item.value("api_key", "");
      if (!cfg.url.empty() && !cfg.database.empty()) {
        dbs_.push_back(std::move(cfg));
      }
    }
  } catch (const nlohmann::json::exception& e) {
    std::cerr << "[bridge_db_store] Failed to load '" << file_path_
              << "': " << e.what() << "\n";
  }
}

void BridgeDbStore::Save() const {
  nlohmann::json doc = nlohmann::json::array();
  for (const auto& cfg : dbs_) {
    doc.push_back({{"url", cfg.url},
                   {"database", cfg.database},
                   {"username", cfg.username},
                   {"api_key", cfg.api_key}});
  }
  std::ofstream file(file_path_);
  if (!file.is_open()) {
    std::cerr << "[bridge_db_store] Cannot write to '" << file_path_ << "'\n";
    return;
  }
  file << doc.dump(2);
}

int BridgeDbStore::AddDb(const OdooConfig& config) {
  std::lock_guard<std::mutex> lock(mutex_);
  dbs_.push_back(config);
  Save();
  return static_cast<int>(dbs_.size()) - 1;
}

void BridgeDbStore::RemoveDb(int index) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (index < 0 || index >= static_cast<int>(dbs_.size())) return;
  dbs_.erase(dbs_.begin() + index);
  Save();
}

std::optional<OdooConfig> BridgeDbStore::GetDb(int index) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (index < 0 || index >= static_cast<int>(dbs_.size())) return std::nullopt;
  return dbs_[static_cast<std::size_t>(index)];
}

std::vector<OdooConfig> BridgeDbStore::GetAllDbs() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return dbs_;
}

int BridgeDbStore::Size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return static_cast<int>(dbs_.size());
}

}  // namespace bready