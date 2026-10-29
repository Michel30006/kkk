// Validación de URLs y defensa SSRF.
//
// Reglas:
//   * sólo http/https (https obligatorio salvo ALLOW_HTTP=1);
//   * sin credenciales embebidas;
//   * el host se resuelve y TODAS sus direcciones deben ser públicas
//     (salvo ALLOW_PRIVATE_NETWORKS=1, sólo para pruebas locales);
//   * se fija la IP validada con CURLOPT_RESOLVE para evitar rebinding;
//   * allowlist opcional de sufijos de host.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace tds {

struct UrlPolicy {
  bool allow_http{false};
  bool allow_private{false};
  std::vector<std::string> allowed_suffixes;
};

struct UrlCheck {
  bool ok{false};
  std::string error;
  std::string scheme;
  std::string host;
  int port{443};
  std::string pinned_ip;
  std::string normalized;
};

UrlCheck validate_url(std::string_view url, const UrlPolicy& policy);

// Resuelve una cabecera Location (absoluta, //host, /ruta o relativa).
std::string resolve_location(std::string_view base_url,
                            std::string_view location);

}  // namespace tds
