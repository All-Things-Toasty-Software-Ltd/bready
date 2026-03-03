// Copyright 2026 All Things Toasty Software Ltd
//
// OdooClient: JSON-RPC client for Odoo 16-19 (SaaS and self-hosted).
// Authenticates with an API key and exposes helpers for Tasks, To-dos,
// Projects, CRM, Knowledge articles, and Discuss channels.

#ifndef INCLUDE_ODOO_CLIENT_H_
#define INCLUDE_ODOO_CLIENT_H_

#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace bready {

// Connection settings for an Odoo instance.
struct OdooConfig {
  std::string url;       // Base URL, e.g. "https://mycompany.odoo.com"
  std::string database;  // Database name (required for self-hosted)
  std::string username;  // User e-mail / login
  std::string api_key;   // API key (or password for older instances)
};

// A simplified Odoo record returned from queries.
struct OdooRecord {
  int id{0};
  std::string name;
  std::string extra;  // Additional context (project name, stage, etc.)
};

// A message fetched from an Odoo Discuss channel.
struct DiscussMessage {
  int id{0};
  int author_id{0};    // res.partner ID of the author (0 if unknown).
  std::string author;  // Display name of the Odoo user who wrote the message.
  std::string body;    // Raw HTML body from Odoo.
  std::vector<int>
      attachment_ids;  // IDs of ir.attachment records on this message.
};

// Metadata for a single Odoo attachment (ir.attachment record).
struct OdooAttachment {
  int id{0};
  std::string name;      // Filename / display name.
  std::string mimetype;  // MIME type (e.g. "image/png", "application/pdf").
};

// Client for the Odoo JSON-RPC 2.0 API.
// Compatible with Odoo 16, 17, 18, and 19 on both SaaS and self-hosted.
// Thread-safe: a single OdooClient may be shared across command handlers.
class OdooClient {
 public:
  explicit OdooClient(const OdooConfig& config);

  // Returns true when all required config fields are non-empty.
  bool IsConfigured() const;

  // Attempts to authenticate with the configured credentials.
  // Returns true on success, false on any auth failure.
  bool ValidateCredentials();

  // Retrieves the first 25 projects.
  std::vector<OdooRecord> GetProjects();

  // Retrieves the first 25 tasks.  Pass a non-empty project_name to filter.
  std::vector<OdooRecord> GetTasks(const std::string& project_name = "");

  // Creates a task.  Returns the new record ID, or -1 on error.
  int CreateTask(const std::string& name, const std::string& project_name = "",
                 const std::string& description = "");

  // Retrieves the first 25 personal to-dos (tasks with no project).
  std::vector<OdooRecord> GetTodos();

  // Creates a personal to-do.  Returns the new record ID, or -1 on error.
  int CreateTodo(const std::string& title);

  // Retrieves the first 25 CRM leads / opportunities.
  std::vector<OdooRecord> GetCrmLeads();

  // Retrieves the first 25 Knowledge articles.
  std::vector<OdooRecord> GetKnowledgeArticles();

  // Retrieves tasks whose names start with |name_prefix| (any project or
  // personal to-do).  Used to surface unlinked-user tagged records.
  std::vector<OdooRecord> GetTasksByNamePrefix(const std::string& name_prefix);

  // Retrieves personal to-dos (no project) whose names start with
  // |name_prefix|.
  std::vector<OdooRecord> GetTodosByNamePrefix(const std::string& name_prefix);

  // Discuss / messaging helpers

  // Returns the first 50 Discuss channels (mail.channel records).
  std::vector<OdooRecord> GetDiscussChannels();

  // Returns the ID of the first Discuss channel whose name matches |name|
  // (case-insensitive), or -1 if not found.
  int FindDiscussChannel(const std::string& name);

  // Posts |body| (plain text or HTML) to the Discuss channel identified by
  // |channel_id|.  Returns the new message ID, or -1 on error.
  int PostDiscussMessage(int channel_id, const std::string& body);

  // Returns the authenticated Odoo user ID (res.users.id), or -1 if not yet
  // authenticated.  Triggers authentication if not already performed.
  int GetUid();

  // Returns the res.partner ID of the authenticated user.  This matches the
  // author_id field on mail.message records, which stores partner IDs rather
  // than user IDs.  Returns -1 on error.  Result is cached after first call.
  int GetPartnerUid();

  // Returns metadata for the given ir.attachment IDs (e.g. from a
  // DiscussMessage's attachment_ids field).
  std::vector<OdooAttachment> GetAttachments(const std::vector<int>& ids);

  // Returns the configuration this client was constructed with.
  const OdooConfig& GetConfig() const { return config_; }

  // Returns all Discuss messages in |channel_id| with ID > |after_id|,
  // ordered by ID ascending.  Returns at most 50 messages per call.
  std::vector<DiscussMessage> GetNewDiscussMessages(int channel_id,
                                                    int after_id);

 private:
  // Sends a JSON-RPC 2.0 POST and returns the parsed "result" field.
  // Returns a null json value on any error.
  nlohmann::json CallJsonRpc(const std::string& endpoint,
                             const nlohmann::json& payload);

  // Wraps CallJsonRpc for the /web/dataset/call_kw endpoint.
  nlohmann::json CallKw(const std::string& model, const std::string& method,
                        const nlohmann::json& args,
                        const nlohmann::json& kwargs);

  // Authenticates and caches the user ID.  Returns uid, or -1 on failure.
  // Must be called with auth_mutex_ held.
  int AuthenticateLocked();

  // Returns a valid uid, (re-)authenticating if necessary.
  int EnsureUid();

  OdooConfig config_;
  int uid_{-1};         // Cached authenticated user ID (res.users.id).
  int partner_id_{-1};  // Cached res.partner ID of the authenticated user.
  std::mutex auth_mutex_;
};

// Loads OdooConfig from environment variables:
//   ODOO_URL, ODOO_DB, ODOO_USER, ODOO_API_KEY
OdooConfig LoadOdooConfigFromEnv();

}  // namespace bready

#endif  // INCLUDE_ODOO_CLIENT_H_