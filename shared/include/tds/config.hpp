#pragma once

#include <cstdint>
#include <string>

namespace tds {

struct Config {
  std::string service_name = "server_a";
  std::string listen_address = "0.0.0.0";
  int port = 8080;

  std::string database_url;
  std::string internal_token;

  std::string server_a_url;  // p. ej. https://ing-inversa-a.onrender.com
  std::string server_b_url;  // p. ej. https://ing-inversa-b.onrender.com
  int ping_interval_seconds = 600;
  int http_timeout_seconds = 10;

  std::uint64_t max_video_bytes = 25ull * 1024 * 1024;
  int download_connect_timeout_seconds = 15;
  int download_max_seconds = 300;
  int max_redirects = 3;
  int encode_part_bytes = 65536;

  std::size_t max_stanza_bytes = 8192;
  int send_delay_ms = 120;
  int max_send_retries = 3;
  int ack_timeout_ms = 15000;
  std::string transport = "mock";
  std::string mock_outbox = "/tmp/tds-outbox";

  std::string todus_username;
  std::string todus_password;
  std::string todus_token;  // JWT ya obtenido (alternativa a usuario/clave)
  std::string todus_auth_url = "https://auth.todus.cu/v2/auth/token";
  std::string todus_domain = "im.todus.cu";
  int todus_port = 5222;
  std::string todus_resource = "tds";
  bool todus_verify_tls = true;

  int worker_poll_ms = 1000;
  int job_lease_seconds = 180;
  int stale_job_minutes = 60;
  int retention_hours = 24;
  int client_confirm_hours = 24;
  int maintenance_interval_seconds = 300;
  int max_concurrent_jobs = 1;

  // Endurecimiento de descargas.
  bool allow_http = false;
  bool allow_private_networks = false;
  std::string allowed_host_suffixes;  // vacío = cualquiera público
};

Config load_config_from_env();
// Informe legible sin secretos (para logs de arranque).
std::string describe_config(const Config& cfg);
// Comprueba los campos imprescindibles de cada servicio.
bool validate_config(const Config& cfg, std::string& err);

}  // namespace tds
