#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace tds {

struct HttpResponse {
  long status{0};
  std::string body;
  std::string error;
  bool ok() const { return error.empty() && status >= 200 && status < 300; }
};

struct HttpOptions {
  int timeout_ms{10000};
  std::vector<std::string> headers;
  std::string user_agent{"ing-inversa/1.0"};
};

HttpResponse http_get(std::string_view url, const HttpOptions& options = {});
HttpResponse http_post(std::string_view url, std::string_view body,
                       const HttpOptions& options = {});

// Descarga en streaming a un fichero temporal con límite de tamaño, timeout y
// hash SHA-256 calculado al vuelo. No sigue redirecciones (el llamador valida
// cada salto).
struct DownloadResult {
  bool ok{false};
  long status{0};
  std::uint64_t size{0};
  std::string sha256;
  std::string location;  // cabecera Location si es 3xx
  std::string error;
  std::string content_type;
};

struct DownloadOptions {
  std::uint64_t max_bytes{25ull * 1024 * 1024};
  int connect_timeout_seconds{15};
  int max_seconds{300};
  std::string user_agent{"ing-inversa/1.0"};
  std::string temp_path;
  // Entrada CURLOPT_RESOLVE ya formateada ("host:port:ip"). Fija la IP
  // validada para evitar DNS rebinding. Vacío = resolución normal.
  std::string resolve_entry;
};

DownloadResult download_to_file(std::string_view url,
                                const DownloadOptions& options);

}  // namespace tds
