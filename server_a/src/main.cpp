#include <drogon/drogon.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

#include "api.hpp"
#include "pipeline.hpp"
#include "tds/config.hpp"
#include "tds/log.hpp"
#include "tds/maintenance.hpp"
#include "tds/pinger.hpp"
#include "tds/storage.hpp"
#include "tds/util.hpp"

int main() {
  using namespace tds;
  init_logging("server_a");

  Config config;
  try {
    config = load_config_from_env();
  } catch (const std::exception& e) {
    spdlog::error("configuración inválida: {}", e.what());
    return 1;
  }
  config.service_name = "server_a";
  config.port = static_cast<int>(std::getenv("PORT") != nullptr
                                     ? std::strtol(std::getenv("PORT"), nullptr, 10)
                                     : config.port);
  std::string err;
  if (!validate_config(config, err)) {
    spdlog::error("configuración inválida: {}", err);
    return 1;
  }
  spdlog::info("arrancando: {}", describe_config(config));

  const char* migrations_env = std::getenv("MIGRATIONS_DIR");
  const std::string migrations_dir =
      migrations_env != nullptr ? migrations_env : "./database/migrations";
  auto store = make_store(config.database_url, migrations_dir, err);
  if (!store) {
    spdlog::error("base de datos no disponible: {}", err);
    return 1;
  }
  store->set_lease_seconds(config.job_lease_seconds);

  ServerAStats stats;
  stats.started_at = unix_now();
  EncodingPipeline pipeline(*store, config, stats);

  std::atomic<bool> stop{false};
  std::vector<std::thread> workers;
  const int worker_count = std::max(1, config.max_concurrent_jobs);
  for (int i = 0; i < worker_count; ++i)
    workers.emplace_back([&pipeline, &stop, i] { pipeline.run(stop, i); });

  register_server_a_routes(*store, config, stats);

  auto pinger = start_health_pinger(stop, "server_b", config.server_b_url,
                                    config.ping_interval_seconds,
                                    config.http_timeout_seconds * 1000,
                                    config.internal_token);
  auto maintenance =
      start_maintenance(stop, *store, config.maintenance_interval_seconds,
                        config.stale_job_minutes, config.client_confirm_hours,
                        config.retention_hours);

  drogon::app()
      .addListener(config.listen_address, config.port)
      .setThreadNum(2)
      .setClientMaxBodySize(64 * 1024)
      .setLogLevel(trantor::Logger::kWarn);

  spdlog::info("Servidor A escuchando en {}:{}", config.listen_address, config.port);
  drogon::app().run();

  stop.store(true);
  for (auto& worker : workers) worker.join();
  if (pinger.joinable()) pinger.join();
  if (maintenance.joinable()) maintenance.join();
  spdlog::info("Servidor A detenido");
  return 0;
}
