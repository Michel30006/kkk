#pragma once

#include <cstdint>
#include <string>

#include "url_guard.hpp"

namespace tds {

struct DownloadLimits {
  std::uint64_t max_bytes{25ull * 1024 * 1024};
  int connect_timeout_seconds{15};
  int max_seconds{300};
  int max_redirects{3};
};

struct DownloadOutcome {
  bool ok{false};
  std::string path;
  std::string sha256;
  std::string content_type;
  std::string error;
  std::uint64_t size{0};
  int hops{0};
};

// Descarga con validación SSRF en cada salto, IP fijada y límite de tamaño.
// Deja el fichero en `temp_path` (el llamador lo borra).
DownloadOutcome download_guarded(std::string url, const UrlPolicy& policy,
                                 const DownloadLimits& limits,
                                 const std::string& temp_path);

}  // namespace tds
