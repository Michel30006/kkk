#include "api.hpp"

#include <drogon/drogon.h>
#include <nlohmann/json.hpp>

#include "tds/log.hpp"
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

bool is_internal(const HttpRequestPtr& req, const Config& config) {
  return tokens_equal(req->getHeader("x-internal-token"), config.internal_token);
}

json job_json(const JobRecord& job) {
  return json{
      {"job_id", job.job_id},
      {"status", job.status},
      {"recipient", job.recipient},
      {"transfer_id", job.transfer_id},
      {"size_bytes", job.size_bytes},
      {"sha256", job.sha256},
      {"encoding_version", job.encoding_version},
      {"total_chunks", job.total_chunks},
      {"data_parts", job.data_parts},
      {"attempts", job.attempts},
      {"error_code", job.error_code},
      {"error_detail", job.error_detail},
      {"updated_at", job.updated_at == 0 ? "" : now_iso8601(job.updated_at)},
  };
}

}  // namespace

void register_server_b_routes(JobStore& store, const Config& config,
                              ServerBStats& stats, DeliveryWorker& worker,
                              Transport& transport) {
  using namespace drogon;

  app().registerHandler(
      "/health",
      [&store, &transport](
          const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& cb) {
        std::string err;
        const bool db_ok = store.ping(err);
        cb(make_json(json{{"status", db_ok ? "ok" : "degraded"},
                          {"service", "server_b"},
                          {"database", db_ok ? "ok" : "error"},
                          {"transport", transport.name()},
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
        cb(make_json(json{{"ready", db_ok},
                          {"service", "server_b"},
                          {"transport", config.transport},
                          {"database", db_ok ? "ok" : "error"}},
                      db_ok ? k200OK : k503ServiceUnavailable));
      },
      {Get});

  // Aviso interno de A tras terminar la codificación. No entrega en la misma
  // petición: despierta al worker para que el HTTP responda rápido.
  // Drogon no interpreta expresiones regulares en registerHandler: los
  // segmentos variables se declaran como `{id}` y llegan como argumento.
  app().registerHandler(
      "/jobs/{id}/deliver",
      [&store, &config, &worker](
          const HttpRequestPtr& req,
          std::function<void(const HttpResponsePtr&)>&& cb,
          const std::string& id) {
        if (!is_internal(req, config)) {
          cb(error_json(k401Unauthorized, "token interno inválido"));
          return;
        }
        JobRecord job;
        std::string err;
        if (!store.get_job(id, job, err)) {
          cb(error_json(k404NotFound, "trabajo no encontrado"));
          return;
        }
        if (job.status == "queued" || job.status == "downloading" ||
            job.status == "encoding") {
          cb(make_json(json{{"job_id", id}, {"accepted", false},
                            {"status", job.status},
                            {"reason", "todavía no está codificado"}},
                        k409Conflict));
          return;
        }
        if (job.status == "completed" || job.status == "cleaned" ||
            job.status == "cancelled" || job.status == "expired" ||
            job.status == "failed_download" || job.status == "failed_encoding") {
          cb(make_json(json{{"job_id", id}, {"accepted", false},
                            {"status", job.status}},
                        k409Conflict));
          return;
        }
        bool retry = false;
        if (job.status == "failed_delivery") {
          // Reintento manual explícito. Las tramas ya confirmadas se omiten
          // gracias a chunk_deliveries, así que no se duplica el envío.
          std::string reset_err;
          if (!store.set_status(id, "ready_for_delivery", "", "", reset_err)) {
            cb(error_json(k500InternalServerError,
                          "no se pudo reprogramar la entrega: " + reset_err));
            return;
          }
          job.status = "ready_for_delivery";
          retry = true;
          spdlog::info("job {} reprogramado para reintento manual", id);
        }
        worker.wake();
        cb(make_json(json{{"job_id", id}, {"accepted", true},
                          {"status", job.status}, {"retry", retry}},
                      k202Accepted));
      },
      {Post});

  app().registerHandler(
      "/jobs/{id}",
      [&store, &config](const HttpRequestPtr& req,
                        std::function<void(const HttpResponsePtr&)>&& cb,
                        const std::string& id) {
        if (!is_internal(req, config)) {
          cb(error_json(k401Unauthorized, "token interno inválido"));
          return;
        }
        JobRecord job;
        std::string err;
        if (!store.get_job(id, job, err)) {
          cb(error_json(k404NotFound, "trabajo no encontrado"));
          return;
        }
        cb(make_json(job_json(job)));
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
        cb(make_json(json{{"jobs_delivered", stats.jobs_delivered.load()},
                          {"jobs_failed", stats.jobs_failed.load()},
                          {"frames_sent", stats.frames_sent.load()},
                          {"bytes_sent", stats.bytes_sent.load()}}));
      },
      {Get});
}

}  // namespace tds
