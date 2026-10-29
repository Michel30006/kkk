#include "tds/maintenance.hpp"

#include <chrono>

#include "tds/log.hpp"
#include "tds/storage.hpp"

namespace tds {

std::thread start_maintenance(const std::atomic<bool>& stop, JobStore& store,
                              int interval_seconds, int stale_minutes,
                              int client_confirm_hours, int retention_hours) {
  if (interval_seconds <= 0) return {};
  return std::thread([&stop, &store, interval_seconds, stale_minutes,
                      client_confirm_hours, retention_hours] {
    spdlog::info(
        "mantenimiento cada {} s (stale={} min, confirmación={} h, retención={} h)",
        interval_seconds, stale_minutes, client_confirm_hours, retention_hours);
    while (!stop.load()) {
      // Espera por tramos para atender la parada con rapidez.
      for (int waited = 0; waited < interval_seconds * 10 && !stop.load();
           ++waited) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      if (stop.load()) break;
      try {
        int expired = 0;
        std::string err;
        if (store.expire_stale(stale_minutes, client_confirm_hours,
                               retention_hours, expired, err)) {
          if (expired > 0)
            spdlog::info("mantenimiento: {} trabajos expirados", expired);
        } else {
          spdlog::warn("mantenimiento: expire_stale falló: {}", err);
        }
        int purged = 0;
        if (store.purge_cleaned(retention_hours, purged, err)) {
          if (purged > 0)
            spdlog::info("mantenimiento: {} trabajos purgados", purged);
        } else {
          spdlog::warn("mantenimiento: purge_cleaned falló: {}", err);
        }
      } catch (const std::exception& e) {
        spdlog::warn("mantenimiento: excepción: {}", e.what());
      }
    }
    spdlog::info("mantenimiento detenido");
  });
}

}  // namespace tds
