#include "tds/pinger.hpp"

#include <chrono>

#include "tds/http.hpp"
#include "tds/log.hpp"

namespace tds {
namespace {

// Espera interrumpible por la parada del proceso.
bool wait_ms(const std::atomic<bool>& stop, int milliseconds) {
  for (int waited = 0; waited < milliseconds && !stop.load(); waited += 100)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  return !stop.load();
}

bool ping_once(const std::string& peer_name, const std::string& peer_base_url,
               int timeout_ms, const std::string& token) {
  HttpOptions options;
  options.timeout_ms = timeout_ms;
  if (!token.empty()) options.headers.push_back("X-Internal-Token: " + token);
  const auto started = std::chrono::steady_clock::now();
  const auto response = http_get(peer_base_url + "/health", options);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - started)
                           .count();
  if (response.ok()) {
    spdlog::info("ping {} OK HTTP {} en {} ms", peer_name, response.status,
                 elapsed);
    return true;
  }
  spdlog::warn("ping {} falló: {} ({} ms)", peer_name,
               response.error.empty() ? std::to_string(response.status)
                                      : response.error,
               elapsed);
  return false;
}

}  // namespace

std::thread start_health_pinger(const std::atomic<bool>& stop,
                                std::string peer_name, std::string peer_base_url,
                                int interval_seconds, int timeout_ms,
                                std::string internal_token) {
  return std::thread([&stop, peer_name = std::move(peer_name),
                      peer_base_url = std::move(peer_base_url), interval_seconds,
                      timeout_ms, token = std::move(internal_token)] {
    if (peer_base_url.empty()) return;
    spdlog::info("ping mutuo hacia {} cada {} s", peer_name, interval_seconds);
    // Tras un fallo se reintenta un máximo de 3 veces con espera progresiva
    // (2, 4 y 8 s). Es deliberadamente ligero: el objetivo es despertar al
    // otro servicio, no saturarlo, y nunca se convierte en un bucle intenso.
    constexpr int kMaxRetries = 3;
    while (!stop.load()) {
      bool ok = ping_once(peer_name, peer_base_url, timeout_ms, token);
      for (int attempt = 1; !ok && attempt <= kMaxRetries && !stop.load();
           ++attempt) {
        const int backoff_ms = 2000 << (attempt - 1);  // 2 s, 4 s, 8 s
        spdlog::info("reintento {}/{} de ping a {} en {} ms", attempt,
                     kMaxRetries, peer_name, backoff_ms);
        if (!wait_ms(stop, backoff_ms)) return;
        ok = ping_once(peer_name, peer_base_url, timeout_ms, token);
      }
      if (!wait_ms(stop, interval_seconds * 1000)) return;
    }
  });
}

}  // namespace tds
