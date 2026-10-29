#pragma once

#include <atomic>
#include <string>
#include <thread>

namespace tds {

// Hilo que hace GET <peer>/health cada `interval_seconds` para intentar
// mantener el servicio despierto en Render. No es una garantía (ver docs).
std::thread start_health_pinger(const std::atomic<bool>& stop,
                                std::string peer_name, std::string peer_base_url,
                                int interval_seconds, int timeout_ms,
                                std::string internal_token);

}  // namespace tds
