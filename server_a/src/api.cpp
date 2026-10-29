#include "api.hpp"

#include "url_guard.hpp"

#include <drogon/drogon.h>
#include <nlohmann/json.hpp>

#include <cctype>

#include "tds/encoding.hpp"
#include "tds/log.hpp"
#include "tds/sha256.hpp"
#include "tds/util.hpp"

namespace tds {
namespace {

using json = nlohmann::json;
using drogon::HttpRequestPtr;
using drogon::HttpResponsePtr;

bool tokens_equal(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  unsigned char diff = 0;
  for (std::size_t i = 0; i < a.size(); ++i)
    diff |= static_cast<unsigned char>(a[i] ^ b[i]);
  return diff == 0;
}

HttpResponsePtr make_json(const json& body,
                          drogon::HttpStatusCode code = drogon::k200OK) {
  auto response = drogon::HttpResponse::newHttpResponse();
  response->setStatusCode(code);
  response->setContentTypeCode(drogon::CT_APPLICATION_JSON);
  response->setBody(body.dump());
  response->addHeader("Cache-Control", "no-store");
  return response;
}

HttpResponsePtr error_json(drogon::HttpStatusCode code, std::string_view message) {
  return make_json(json{{"error", message}}, code);
}

std::string header_value(const HttpRequestPtr& req, const std::string& name) {
  return req->getHeader(name);
}

bool is_internal(const HttpRequestPtr& req, const Config& config) {
  return tokens_equal(header_value(req, "x-internal-token"), config.internal_token);
}

bool client_authorized(const HttpRequestPtr& req, const JobRecord& job,
                       const Config& config) {
  if (is_internal(req, config)) return true;
  const auto token = header_value(req, "x-client-token");
  if (token.empty()) return false;
  return tokens_equal(sha256_hex(token), job.client_token_hash);
}

bool is_recipient(std::string_view recipient) {
  if (recipient.size() < 5 || recipient.size() > 15) return false;
  for (const char c : recipient)
    if (c < '0' || c > '9') return false;
  return true;
}

json job_public_json(const JobRecord& job) {
  json out{
      {"job_id", job.job_id},
      {"status", job.status},
      {"transfer_id", job.transfer_id},
      {"size_bytes", job.size_bytes},
      {"sha256", job.sha256},
      {"encoding_version", job.encoding_version},
      {"total_chunks", job.total_chunks},
      {"created_at", job.created_at == 0 ? "" : now_iso8601(job.created_at)},
      {"updated_at", job.updated_at == 0 ? "" : now_iso8601(job.updated_at)},
      {"completed_at", job.completed_at == 0 ? "" : now_iso8601(job.completed_at)},
      {"error_code", job.error_code},
  };
  return out;
}

UrlPolicy policy_from(const Config& config) {
  UrlPolicy policy;
  policy.allow_http = config.allow_http;
  policy.allow_private = config.allow_private_networks;
  if (!config.allowed_host_suffixes.empty())
    policy.allowed_suffixes = split(config.allowed_host_suffixes, ',');
  return policy;
}

}  // namespace

void register_server_a_routes(JobStore& store, const Config& config,
                              ServerAStats& stats) {
  using namespace drogon;

  app().registerHandler(
      "/health",
      [&store](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& cb) {
        std::string err;
        const bool db_ok = store.ping(err);
        cb(make_json(json{{"status", db_ok ? "ok" : "degraded"},
                          {"service", "server_a"},
                          {"database", db_ok ? "ok" : "error"},
                          {"time", now_iso8601()}},
                      db_ok ? k200OK : k503ServiceUnavailable));
      },
      {Get});

  app().registerHandler(
      "/ready",
      [&store, &config](
          const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& cb) {
        std::string err;
        const bool db_ok = store.ping(err);
        // La cola vive en PostgreSQL: si la base responde, se pueden aceptar
        // trabajos. No se promete capacidad ilimitada (MAX_CONCURRENT_JOBS).
        const bool accepting = db_ok;
        cb(make_json(json{{"ready", accepting},
                          {"service", config.service_name},
                          {"database", db_ok ? "ok" : "error"}},
                      accepting ? k200OK : k503ServiceUnavailable));
      },
      {Get});

  app().registerHandler(
      "/jobs",
      [&store, &config](const HttpRequestPtr& req,
                        std::function<void(const HttpResponsePtr&)>&& cb) {
        if (!is_internal(req, config)) {
          cb(error_json(k401Unauthorized, "token interno inválido"));
          return;
        }
        json body;
        try {
          body = json::parse(req->body());
        } catch (...) {
          cb(error_json(k400BadRequest, "JSON inválido"));
          return;
        }
        const auto url = body.value("url", "");
        const auto recipient = body.value("recipient", "");
        if (url.empty() || !is_recipient(recipient)) {
          cb(error_json(k400BadRequest, "url y recipient son obligatorios"));
          return;
        }
        const auto check = validate_url(url, policy_from(config));
        if (!check.ok) {
          cb(error_json(k400BadRequest, "URL rechazada: " + check.error));
          return;
        }
        const auto job_id = new_uuid();
        const auto client_token = random_hex(24);
        CreateJobInput input;
        input.job_id = job_id;
        input.source_url = std::string(url);
        input.recipient = std::string(recipient);
        input.client_token_hash = sha256_hex(client_token);
        input.transfer_id = new_uuid();
        input.encoding_version = kEncodingVersion;
        input.part_bytes = config.encode_part_bytes;
        std::string err;
        if (!store.create_job(input, err)) {
          spdlog::error("no se pudo crear el job: {}", err);
          cb(error_json(k500InternalServerError, "no se pudo crear el trabajo"));
          return;
        }
        spdlog::info("job {} creado para {}", job_id, recipient);
        json out{{"job_id", job_id},
                 {"transfer_id", input.transfer_id},
                 {"status", "queued"},
                 {"client_token", client_token}};
        cb(make_json(out, k201Created));
      },
      {Post});

  // Drogon no interpreta expresiones regulares en registerHandler: los
  // segmentos variables se declaran como `{id}` y llegan como argumento.
  app().registerHandler(
      "/jobs/{id}",
      [&store, &config](const HttpRequestPtr& req,
                        std::function<void(const HttpResponsePtr&)>&& cb,
                        const std::string& id) {
        JobRecord job;
        std::string err;
        if (!store.get_job(id, job, err)) {
          cb(error_json(k404NotFound, "trabajo no encontrado"));
          return;
        }
        if (!client_authorized(req, job, config)) {
          cb(error_json(k403Forbidden, "no autorizado para este trabajo"));
          return;
        }
        // Progreso observable: tramas ya aceptadas por el transporte. Permite
        // al cliente mostrar avance sin exponer los índices internos.
        std::uint32_t delivered = 0;
        std::string count_err;
        json body = job_public_json(job);
        if (store.delivered_count(job.job_id, delivered, count_err)) {
          body["delivered_chunks"] = delivered;
        }
        cb(make_json(body));
      },
      {Get});

  app().registerHandler(
      "/jobs/{id}/confirm",
      [&store, &config](const HttpRequestPtr& req,
                        std::function<void(const HttpResponsePtr&)>&& cb,
                        const std::string& id) {
        JobRecord job;
        std::string err;
        if (!store.get_job(id, job, err)) {
          cb(error_json(k404NotFound, "trabajo no encontrado"));
          return;
        }
        if (!client_authorized(req, job, config)) {
          cb(error_json(k403Forbidden, "no autorizado para este trabajo"));
          return;
        }
        json body;
        try {
          body = json::parse(req->body());
        } catch (...) {
          cb(error_json(k400BadRequest, "JSON inválido"));
          return;
        }
        const auto sha = body.value("sha256", "");
        const auto size = body.value("size", static_cast<std::uint64_t>(0));
        if (sha.size() != 64 || size == 0) {
          cb(error_json(k400BadRequest, "sha256 y size son obligatorios"));
          return;
        }
        // Idempotencia: un cliente que reintenta la confirmación tras perder
        // la respuesta no debe recibir un error si ya se completó igual.
        if (job.status == "completed" || job.status == "cleaned") {
          if (job.sha256 == sha && job.size_bytes == static_cast<std::int64_t>(size)) {
            cb(make_json(json{{"job_id", id}, {"status", job.status},
                              {"idempotent", true}}));
          } else {
            cb(error_json(k409Conflict,
                          "el trabajo ya se completó con otro hash o tamaño"));
          }
          return;
        }
        if (!store.confirm_reconstruction(id, sha, size, err)) {
          spdlog::warn("confirmación rechazada para {}: {}", id, err);
          store.add_event(id, "confirm_rejected", err, err);
          cb(error_json(k409Conflict, err));
          return;
        }
        std::string cleanup_err;
        if (!store.cleanup_job_data(id, cleanup_err))
          spdlog::warn("limpieza de {} falló: {}", id, cleanup_err);
        spdlog::info("job {} confirmado y limpiado", id);
        cb(make_json(json{{"job_id", id}, {"status", "cleaned"}}));
      },
      {Post});

  app().registerHandler(
      "/jobs/{id}/events",
      [&store, &config](const HttpRequestPtr& req,
                        std::function<void(const HttpResponsePtr&)>&& cb,
                        const std::string& id) {
        if (!is_internal(req, config)) {
          cb(error_json(k401Unauthorized, "token interno inválido"));
          return;
        }
        std::vector<std::string> events;
        std::string err;
        if (!store.list_events(id, events, err)) {
          cb(error_json(k404NotFound, "trabajo no encontrado"));
          return;
        }
        cb(make_json(json{{"events", events}}));
      },
      {Get});

  app().registerHandler(
      "/stats",
      [&config, &stats](const HttpRequestPtr& req,
                        std::function<void(const HttpResponsePtr&)>&& cb) {
        if (!is_internal(req, config)) {
          cb(error_json(k401Unauthorized, "token interno inválido"));
          return;
        }
        cb(make_json(json{{"jobs_encoded", stats.jobs_encoded.load()},
                          {"jobs_failed", stats.jobs_failed.load()},
                          {"bytes_encoded", stats.bytes_encoded.load()}}));
      },
      {Get});
}

}  // namespace tds
