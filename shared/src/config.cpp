#include "tds/config.hpp"

#include <cstdlib>
#include <stdexcept>

#include "tds/log.hpp"

namespace tds {
namespace {

std::string env_str(const char* name, std::string fallback) {
  const char* value = std::getenv(name);
  return (value != nullptr && *value != '\0') ? std::string(value) : fallback;
}

long long env_int(const char* name, long long fallback) {
  const char* value = std::getenv(name);
  if (value == nullptr || *value == '\0') return fallback;
  try {
    return std::stoll(value);
  } catch (...) {
    throw std::runtime_error(std::string("valor no numérico en ") + name);
  }
}

bool env_bool(const char* name, bool fallback) {
  const auto value = env_str(name, fallback ? "1" : "0");
  return value == "1" || value == "true" || value == "yes" || value == "on";
}

}  // namespace

Config load_config_from_env() {
  Config c;
  c.service_name = env_str("SERVICE_NAME", "server_a");
  c.listen_address = env_str("LISTEN_ADDRESS", "0.0.0.0");
  c.port = static_cast<int>(env_int("PORT", 8080));
  c.database_url = env_str("DATABASE_URL", "");
  c.internal_token = env_str("INTERNAL_TOKEN", "");
  c.server_a_url = env_str("SERVER_A_URL", "");
  c.server_b_url = env_str("SERVER_B_URL", "");
  c.ping_interval_seconds =
      static_cast<int>(env_int("PING_INTERVAL_SECONDS", 600));
  c.http_timeout_seconds = static_cast<int>(env_int("HTTP_TIMEOUT_SECONDS", 10));
  c.max_video_bytes = static_cast<std::uint64_t>(
      env_int("MAX_VIDEO_BYTES", 25LL * 1024 * 1024));
  c.download_connect_timeout_seconds =
      static_cast<int>(env_int("DOWNLOAD_CONNECT_TIMEOUT_SECONDS", 15));
  c.download_max_seconds = static_cast<int>(env_int("DOWNLOAD_MAX_SECONDS", 300));
  c.max_redirects = static_cast<int>(env_int("MAX_REDIRECTS", 3));
  c.encode_part_bytes = static_cast<int>(env_int("ENCODE_PART_BYTES", 65536));
  c.max_stanza_bytes = static_cast<std::size_t>(
      env_int("MAX_STANZA_BYTES", 8192));
  c.send_delay_ms = static_cast<int>(env_int("SEND_DELAY_MS", 120));
  c.max_send_retries = static_cast<int>(env_int("MAX_SEND_RETRIES", 3));
  c.ack_timeout_ms = static_cast<int>(env_int("ACK_TIMEOUT_MS", 15000));
  c.transport = env_str("TRANSPORT", "mock");
  c.mock_outbox = env_str("MOCK_OUTBOX", "/tmp/tds-outbox");
  c.todus_username = env_str("TODUS_USERNAME", "");
  c.todus_password = env_str("TODUS_PASSWORD", "");
  c.todus_token = env_str("TODUS_TOKEN", "");
  c.todus_auth_url =
      env_str("TODUS_AUTH_URL", "https://auth.todus.cu/v2/auth/token");
  c.todus_domain = env_str("TODUS_DOMAIN", "im.todus.cu");
  c.todus_port = static_cast<int>(env_int("TODUS_PORT", 5222));
  c.todus_resource = env_str("TODUS_RESOURCE", "tds");
  c.todus_verify_tls = env_bool("TODUS_VERIFY_TLS", true);
  c.worker_poll_ms = static_cast<int>(env_int("WORKER_POLL_MS", 1000));
  c.job_lease_seconds = static_cast<int>(env_int("JOB_LEASE_SECONDS", 180));
  c.stale_job_minutes = static_cast<int>(env_int("STALE_JOB_MINUTES", 60));
  c.retention_hours = static_cast<int>(env_int("RETENTION_HOURS", 24));
  c.client_confirm_hours = static_cast<int>(env_int("CLIENT_CONFIRM_HOURS", 24));
  c.maintenance_interval_seconds =
      static_cast<int>(env_int("MAINTENANCE_INTERVAL_SECONDS", 300));
  c.max_concurrent_jobs = static_cast<int>(env_int("MAX_CONCURRENT_JOBS", 1));
  c.allow_http = env_bool("ALLOW_HTTP", false);
  c.allow_private_networks = env_bool("ALLOW_PRIVATE_NETWORKS", false);
  c.allowed_host_suffixes = env_str("ALLOWED_HOST_SUFFIXES", "");
  return c;
}

bool validate_config(const Config& cfg, std::string& err) {
  if (cfg.port < 1 || cfg.port > 65535) {
    err = "PORT fuera de rango";
    return false;
  }
  if (cfg.database_url.empty()) {
    err = "DATABASE_URL es obligatoria";
    return false;
  }
  if (cfg.internal_token.size() < 16) {
    err = "INTERNAL_TOKEN debe tener al menos 16 caracteres";
    return false;
  }
  if (cfg.max_stanza_bytes < 512 || cfg.max_stanza_bytes > 65536) {
    err = "MAX_STANZA_BYTES fuera de rango razonable (512..65536)";
    return false;
  }
  if (cfg.max_stanza_bytes > 8192)
    err = "aviso: MAX_STANZA_BYTES supera el límite verificado de ToDus (8192)";
  if (cfg.max_video_bytes < 1024) {
    err = "MAX_VIDEO_BYTES demasiado pequeño";
    return false;
  }
  if (cfg.transport != "mock" && cfg.transport != "todus") {
    err = "TRANSPORT debe ser 'mock' o 'todus'";
    return false;
  }
  if (cfg.transport == "todus") {
    if (cfg.todus_username.empty()) {
      err = "TODUS_USERNAME es obligatorio con TRANSPORT=todus";
      return false;
    }
    if (cfg.todus_password.empty() && cfg.todus_token.empty()) {
      err = "hace falta TODUS_PASSWORD o TODUS_TOKEN con TRANSPORT=todus";
      return false;
    }
  }
  return true;
}

std::string describe_config(const Config& cfg) {
  std::string out;
  out += "service=" + cfg.service_name;
  out += " listen=" + cfg.listen_address + ":" + std::to_string(cfg.port);
  out += " db=" + (cfg.database_url.empty() ? std::string("<vacía>")
                                            : std::string("<configurada>"));
  out += " token=" + redact(cfg.internal_token);
  out += " transport=" + cfg.transport;
  out += " max_video_bytes=" + std::to_string(cfg.max_video_bytes);
  out += " max_stanza_bytes=" + std::to_string(cfg.max_stanza_bytes);
  out += " ping_interval_s=" + std::to_string(cfg.ping_interval_seconds);
  // No se registra el número de teléfono (dato personal): sólo si está puesto.
  out += " todus_user=" +
         (cfg.todus_username.empty() ? std::string("<vacío>")
                                     : std::string("<configurado>"));
  return out;
}

}  // namespace tds
