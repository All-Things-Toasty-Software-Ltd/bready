// Copyright 2026 All Things Toasty Software Ltd
//
// OdooClient implementation – uses libcurl for HTTP and nlohmann/json for
// JSON-RPC serialisation.

#include "odoo_client.h"

#include <curl/curl.h>

#include <cstdlib>
#include <iostream>
#include <sstream>

namespace bready {

namespace {

// libcurl write callback – appends received bytes to a std::string.
size_t CurlWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* buf = static_cast<std::string*>(userdata);
  buf->append(ptr, size * nmemb);
  return size * nmemb;
}

}  // namespace

OdooClient::OdooClient(const OdooConfig& config) : config_(config) {}

bool OdooClient::IsConfigured() const {
  return !config_.url.empty() && !config_.database.empty() &&
         !config_.username.empty() && !config_.api_key.empty();
}

bool OdooClient::ValidateCredentials() { return EnsureUid() > 0; }

// HTTP / JSON-RPC helpers

nlohmann::json OdooClient::CallJsonRpc(const std::string& endpoint,
                                       const nlohmann::json& payload) {
  CURL* curl = curl_easy_init();
  if (curl == nullptr) {
    std::cerr << "[odoo] curl_easy_init failed\n";
    return nullptr;
  }

  const std::string url = config_.url + endpoint;
  const std::string body = payload.dump();
  std::string response;

  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");

  // Build Basic Auth credentials.  The Odoo username is an e-mail address
  // (no colon) and the API key is a hexadecimal string, so neither field
  // introduces a ':' that would break the Basic Auth format.

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);

  CURLcode res = curl_easy_perform(curl);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK) {
    std::cerr << "[odoo] curl error: " << curl_easy_strerror(res) << "\n";
    return nullptr;
  }

  try {
    auto doc = nlohmann::json::parse(response);
    if (doc.contains("error")) {
      std::cerr << "[odoo] JSON-RPC error: " << doc["error"].dump() << "\n";
      return nullptr;
    }
    return doc.value("result", nlohmann::json(nullptr));
  } catch (const nlohmann::json::exception& e) {
    std::cerr << "[odoo] JSON parse error: " << e.what() << "\n";
    return nullptr;
  }
}

nlohmann::json OdooClient::CallKw(const std::string& model,
                                  const std::string& method,
                                  const nlohmann::json& args,
                                  const nlohmann::json& kwargs) {
  if (EnsureUid() < 0) {
    return nullptr;
  }

  nlohmann::json payload = {
      {"jsonrpc", "2.0"},
      {"method", "call"},
      {"id", 1},
      {"params",
       {{"service", "object"},
        {"method", "execute_kw"},
        {"args", nlohmann::json::array({config_.database, uid_, config_.api_key,
                                        model, method, args, kwargs})}}}};

  return CallJsonRpc("/jsonrpc", payload);
}

// Authentication

int OdooClient::AuthenticateLocked() {
  nlohmann::json payload = {
      {"jsonrpc", "2.0"},
      {"method", "call"},
      {"id", 1},
      {"params",
       {{"service", "common"},
        {"method", "authenticate"},
        {"args",
         nlohmann::json::array({config_.database, config_.username,
                                config_.api_key, nlohmann::json::object()})}}}};

  nlohmann::json result = CallJsonRpc("/jsonrpc", payload);

  if (!result.is_number_integer()) {
    std::cerr << "[odoo] Authentication failed\n";
    uid_ = -1;
    return -1;
  }

  uid_ = result.get<int>();
  return uid_;
}

int OdooClient::EnsureUid() {
  std::lock_guard<std::mutex> lock(auth_mutex_);
  if (uid_ > 0) {
    return uid_;
  }
  return AuthenticateLocked();
}

int OdooClient::GetUid() { return EnsureUid(); }

std::vector<OdooRecord> OdooClient::GetProjects() {
  if (EnsureUid() < 0) return {};

  nlohmann::json result = CallKw(
      "project.project", "search_read",
      nlohmann::json::array({nlohmann::json::array()}),
      {{"fields", {"name", "description", "task_count"}}, {"limit", 25}});

  std::vector<OdooRecord> records;
  if (!result.is_array()) return records;
  for (const auto& item : result) {
    OdooRecord rec;
    rec.id = item.value("id", 0);
    rec.name = item.value("name", "");
    int count = item.value("task_count", 0);
    rec.extra = std::to_string(count) + " task(s)";
    records.push_back(std::move(rec));
  }
  return records;
}

std::vector<OdooRecord> OdooClient::GetTasks(const std::string& project_name) {
  if (EnsureUid() < 0) return {};

  nlohmann::json domain = nlohmann::json::array(
      {nlohmann::json::array({"project_id", "!=", false})});

  if (!project_name.empty()) {
    domain = nlohmann::json::array(
        {nlohmann::json::array({"project_id.name", "ilike", project_name}),
         nlohmann::json::array({"project_id", "!=", false})});
  }

  nlohmann::json result =
      CallKw("project.task", "search_read", nlohmann::json::array({domain}),
             {{"fields", {"name", "project_id", "stage_id", "description"}},
              {"limit", 25}});

  std::vector<OdooRecord> records;
  if (!result.is_array()) return records;
  for (const auto& item : result) {
    OdooRecord rec;
    rec.id = item.value("id", 0);
    rec.name = item.value("name", "");
    // project_id is [id, "name"] or false
    if (item.contains("project_id") && item["project_id"].is_array() &&
        item["project_id"].size() >= 2) {
      rec.extra = "Project: " + item["project_id"][1].get<std::string>();
    }
    if (item.contains("stage_id") && item["stage_id"].is_array() &&
        item["stage_id"].size() >= 2) {
      rec.extra += "  Stage: " + item["stage_id"][1].get<std::string>();
    }
    records.push_back(std::move(rec));
  }
  return records;
}

int OdooClient::CreateTask(const std::string& name,
                           const std::string& project_name,
                           const std::string& description) {
  if (EnsureUid() < 0) return -1;

  nlohmann::json vals = {{"name", name}};
  if (!description.empty()) {
    vals["description"] = description;
  }

  // Resolve project ID if a name was provided.
  if (!project_name.empty()) {
    nlohmann::json proj_result =
        CallKw("project.project", "search_read",
               nlohmann::json::array({nlohmann::json::array(
                   {nlohmann::json::array({"name", "ilike", project_name})})}),
               {{"fields", {"id", "name"}}, {"limit", 1}});
    if (proj_result.is_array() && !proj_result.empty()) {
      vals["project_id"] = proj_result[0]["id"].get<int>();
    }
  }

  nlohmann::json result =
      CallKw("project.task", "create", nlohmann::json::array({vals}),
             nlohmann::json::object());

  if (result.is_number_integer()) {
    return result.get<int>();
  }
  return -1;
}

std::vector<OdooRecord> OdooClient::GetTodos() {
  if (EnsureUid() < 0) return {};

  // Personal to-dos in Odoo 17+ are tasks where project_id is false.
  nlohmann::json domain = nlohmann::json::array(
      {nlohmann::json::array({"project_id", "=", false})});

  nlohmann::json result = CallKw(
      "project.task", "search_read", nlohmann::json::array({domain}),
      {{"fields",
        {"name", "description", "date_deadline", "personal_stage_type_id"}},
       {"limit", 25}});

  std::vector<OdooRecord> records;
  if (!result.is_array()) return records;
  for (const auto& item : result) {
    OdooRecord rec;
    rec.id = item.value("id", 0);
    rec.name = item.value("name", "");
    if (item.contains("personal_stage_type_id") &&
        item["personal_stage_type_id"].is_array() &&
        item["personal_stage_type_id"].size() >= 2) {
      rec.extra =
          "Stage: " + item["personal_stage_type_id"][1].get<std::string>();
    }
    if (item.contains("date_deadline") && item["date_deadline"].is_string()) {
      rec.extra += "  Due: " + item["date_deadline"].get<std::string>();
    }
    records.push_back(std::move(rec));
  }
  return records;
}

int OdooClient::CreateTodo(const std::string& title) {
  if (EnsureUid() < 0) return -1;

  // A personal to-do has no project_id.
  nlohmann::json vals = {{"name", title}};

  nlohmann::json result =
      CallKw("project.task", "create", nlohmann::json::array({vals}),
             nlohmann::json::object());

  if (result.is_number_integer()) {
    return result.get<int>();
  }
  return -1;
}

std::vector<OdooRecord> OdooClient::GetCrmLeads() {
  if (EnsureUid() < 0) return {};

  nlohmann::json result =
      CallKw("crm.lead", "search_read",
             nlohmann::json::array({nlohmann::json::array()}),
             {{"fields", {"name", "partner_name", "stage_id", "probability"}},
              {"limit", 25}});

  std::vector<OdooRecord> records;
  if (!result.is_array()) return records;
  for (const auto& item : result) {
    OdooRecord rec;
    rec.id = item.value("id", 0);
    rec.name = item.value("name", "");
    std::string partner = item.value("partner_name", "");
    if (!partner.empty()) {
      rec.extra = "Customer: " + partner;
    }
    if (item.contains("stage_id") && item["stage_id"].is_array() &&
        item["stage_id"].size() >= 2) {
      if (!rec.extra.empty()) rec.extra += "  ";
      rec.extra += "Stage: " + item["stage_id"][1].get<std::string>();
    }
    double prob = item.value("probability", 0.0);
    if (prob > 0.0) {
      std::ostringstream ss;
      ss << static_cast<int>(prob) << "%";
      rec.extra += "  Win: " + ss.str();
    }
    records.push_back(std::move(rec));
  }
  return records;
}

std::vector<OdooRecord> OdooClient::GetKnowledgeArticles() {
  if (EnsureUid() < 0) return {};

  nlohmann::json result =
      CallKw("knowledge.article", "search_read",
             nlohmann::json::array({nlohmann::json::array()}),
             {{"fields", {"name", "parent_id"}}, {"limit", 25}});

  std::vector<OdooRecord> records;
  if (!result.is_array()) return records;
  for (const auto& item : result) {
    OdooRecord rec;
    rec.id = item.value("id", 0);
    if (item.contains("name") && item["name"].is_string()) {
      rec.name = item["name"].get<std::string>();
    } else {
      rec.name = "(Untitled)";
    }
    if (item.contains("parent_id")) {
      const auto& parent = item["parent_id"];
      if (parent.is_array() && parent.size() >= 2 && parent[1].is_string()) {
        rec.extra = "Parent: " + parent[1].get<std::string>();
      }
    }
    records.push_back(std::move(rec));
  }
  return records;
}

// Tagged-record search helpers

std::vector<OdooRecord> OdooClient::GetTasksByNamePrefix(
    const std::string& name_prefix) {
  if (EnsureUid() < 0) return {};

  nlohmann::json domain = nlohmann::json::array(
      {nlohmann::json::array({"name", "like", name_prefix + "%"})});

  nlohmann::json result =
      CallKw("project.task", "search_read", nlohmann::json::array({domain}),
             {{"fields", {"name", "project_id", "stage_id"}}, {"limit", 25}});

  std::vector<OdooRecord> records;
  if (!result.is_array()) return records;
  for (const auto& item : result) {
    OdooRecord rec;
    rec.id = item.value("id", 0);
    rec.name = item.value("name", "");
    if (item.contains("project_id") && item["project_id"].is_array() &&
        item["project_id"].size() >= 2) {
      rec.extra = "Project: " + item["project_id"][1].get<std::string>();
    }
    if (item.contains("stage_id") && item["stage_id"].is_array() &&
        item["stage_id"].size() >= 2) {
      if (!rec.extra.empty()) rec.extra += "  ";
      rec.extra += "Stage: " + item["stage_id"][1].get<std::string>();
    }
    records.push_back(std::move(rec));
  }
  return records;
}

std::vector<OdooRecord> OdooClient::GetTodosByNamePrefix(
    const std::string& name_prefix) {
  if (EnsureUid() < 0) return {};

  nlohmann::json domain = nlohmann::json::array(
      {nlohmann::json::array({"name", "like", name_prefix + "%"}),
       nlohmann::json::array({"project_id", "=", false})});

  nlohmann::json result =
      CallKw("project.task", "search_read", nlohmann::json::array({domain}),
             {{"fields", {"name", "personal_stage_type_id", "date_deadline"}},
              {"limit", 25}});

  std::vector<OdooRecord> records;
  if (!result.is_array()) return records;
  for (const auto& item : result) {
    OdooRecord rec;
    rec.id = item.value("id", 0);
    rec.name = item.value("name", "");
    if (item.contains("personal_stage_type_id") &&
        item["personal_stage_type_id"].is_array() &&
        item["personal_stage_type_id"].size() >= 2) {
      rec.extra =
          "Stage: " + item["personal_stage_type_id"][1].get<std::string>();
    }
    if (item.contains("date_deadline") && item["date_deadline"].is_string()) {
      if (!rec.extra.empty()) rec.extra += "  ";
      rec.extra += "Due: " + item["date_deadline"].get<std::string>();
    }
    records.push_back(std::move(rec));
  }
  return records;
}

// Discuss helpers

std::vector<OdooRecord> OdooClient::GetDiscussChannels() {
  if (EnsureUid() < 0) return {};

  nlohmann::json result =
      CallKw("discuss.channel", "search_read",
             nlohmann::json::array({nlohmann::json::array()}),
             {{"fields", {"name", "id"}}, {"limit", 50}});

  std::vector<OdooRecord> records;
  if (!result.is_array()) return records;
  for (const auto& item : result) {
    OdooRecord rec;
    rec.id = item.value("id", 0);
    rec.name = item.value("name", "");
    records.push_back(std::move(rec));
  }
  return records;
}

int OdooClient::FindDiscussChannel(const std::string& name) {
  if (EnsureUid() < 0) return -1;

  nlohmann::json domain =
      nlohmann::json::array({nlohmann::json::array({"name", "ilike", name})});

  nlohmann::json result =
      CallKw("discuss.channel", "search_read", nlohmann::json::array({domain}),
             {{"fields", {"id", "name"}}, {"limit", 1}});

  if (result.is_array() && !result.empty()) {
    return result[0].value("id", -1);
  }
  return -1;
}

int OdooClient::PostDiscussMessage(int channel_id, const std::string& body) {
  if (EnsureUid() < 0) return -1;

  nlohmann::json result =
      CallKw("discuss.channel", "message_post",
             nlohmann::json::array({nlohmann::json::array({channel_id})}),
             {{"body", body}, {"message_type", "comment"}});

  if (result.is_number_integer()) {
    return result.get<int>();
  }
  return -1;
}

std::vector<DiscussMessage> OdooClient::GetNewDiscussMessages(int channel_id,
                                                              int after_id) {
  if (EnsureUid() < 0) return {};

  nlohmann::json domain = nlohmann::json::array(
      {nlohmann::json::array({"model", "=", "discuss.channel"}),
       nlohmann::json::array({"res_id", "=", channel_id}),
       nlohmann::json::array({"message_type", "=", "comment"}),
       nlohmann::json::array({"id", ">", after_id})});

  nlohmann::json result =
      CallKw("mail.message", "search_read", nlohmann::json::array({domain}),
             {{"fields", {"id", "body", "author_id", "attachment_ids"}},
              {"limit", 50},
              {"order", "id asc"}});

  std::vector<DiscussMessage> messages;
  if (!result.is_array()) return messages;
  for (const auto& item : result) {
    DiscussMessage msg;
    msg.id = item.value("id", 0);
    msg.body = item.value("body", "");
    // author_id is [id, "Display Name"] or false.
    if (item.contains("author_id") && item["author_id"].is_array() &&
        item["author_id"].size() >= 2) {
      msg.author_id = item["author_id"][0].get<int>();
      msg.author = item["author_id"][1].get<std::string>();
    } else {
      msg.author = "Unknown";
    }
    // attachment_ids is a list of ir.attachment record IDs.
    if (item.contains("attachment_ids") && item["attachment_ids"].is_array()) {
      for (const auto& att_id : item["attachment_ids"]) {
        if (att_id.is_number_integer()) {
          msg.attachment_ids.push_back(att_id.get<int>());
        }
      }
    }
    messages.push_back(std::move(msg));
  }
  return messages;
}

int OdooClient::GetPartnerUid() {
  int uid = EnsureUid();
  if (uid < 0) return -1;

  {
    std::lock_guard<std::mutex> lock(auth_mutex_);
    if (partner_id_ > 0) return partner_id_;
  }

  nlohmann::json result =
      CallKw("res.users", "read",
             nlohmann::json::array({nlohmann::json::array({uid})}),
             {{"fields", nlohmann::json::array({"partner_id"})}});

  if (result.is_array() && !result.empty() &&
      result[0].contains("partner_id") && result[0]["partner_id"].is_array() &&
      // partner_id is a Many2one: [res.partner.id, "Display Name"]
      result[0]["partner_id"].size() >= 2) {
    int pid = result[0]["partner_id"][0].get<int>();
    std::lock_guard<std::mutex> lock(auth_mutex_);
    partner_id_ = pid;
    return pid;
  }
  return -1;
}

std::vector<OdooAttachment> OdooClient::GetAttachments(
    const std::vector<int>& ids) {
  if (EnsureUid() < 0 || ids.empty()) return {};

  nlohmann::json domain = nlohmann::json::array(
      {nlohmann::json::array({"id", "in", nlohmann::json(ids)})});

  nlohmann::json result =
      CallKw("ir.attachment", "search_read", nlohmann::json::array({domain}),
             {{"fields", {"id", "name", "mimetype"}}, {"limit", 50}});

  std::vector<OdooAttachment> attachments;
  if (!result.is_array()) return attachments;
  for (const auto& item : result) {
    OdooAttachment att;
    att.id = item.value("id", 0);
    att.name = item.value("name", "");
    att.mimetype = item.value("mimetype", "");
    attachments.push_back(std::move(att));
  }
  return attachments;
}

OdooConfig LoadOdooConfigFromEnv() {
  OdooConfig cfg;
  const char* url = std::getenv("ODOO_URL");
  const char* db = std::getenv("ODOO_DB");
  const char* usr = std::getenv("ODOO_USER");
  const char* key = std::getenv("ODOO_API_KEY");
  if (url != nullptr) cfg.url = url;
  if (db != nullptr) cfg.database = db;
  if (usr != nullptr) cfg.username = usr;
  if (key != nullptr) cfg.api_key = key;
  return cfg;
}

}  // namespace bready