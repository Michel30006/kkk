#include "url_guard.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <optional>

#include "tds/util.hpp"

namespace tds {
namespace {

bool is_private_v4(const struct in_addr& addr) {
  const std::uint32_t ip = ntohl(addr.s_addr);
  auto in = [&](std::uint32_t net, int bits) {
    const std::uint32_t mask = bits == 0 ? 0 : (0xFFFFFFFFu << (32 - bits));
    return (ip & mask) == (net & mask);
  };
  return in(0x00000000u, 8) ||    // "este" host
         in(0x0A000000u, 8) ||    // 10/8
         in(0x64400000u, 10) ||   // 100.64/10 (CGNAT)
         in(0x7F000000u, 8) ||    // loopback
         in(0xA9FE0000u, 16) ||   // link-local
         in(0xAC100000u, 12) ||   // 172.16/12
         in(0xC0A80000u, 16) ||   // 192.168/16
         in(0xC0000000u, 24) ||   // 192.0.0/24
         in(0xC0000200u, 24) ||   // 192.0.2/24 (TEST-NET)
         in(0xC6120000u, 15) ||   // 198.18/15
         in(0xC6336400u, 24) ||   // 198.51.100/24
         in(0xCB007100u, 24) ||   // 203.0.113/24
         in(0xE0000000u, 4) ||    // multicast
         in(0xF0000000u, 4);      // reservado
}

bool is_private_v6(const struct in6_addr& addr) {
  const auto* b = addr.s6_addr;
  const bool all_zero = std::all_of(b, b + 16, [](unsigned char v) { return v == 0; });
  if (all_zero) return true;
  if (b[0] == 0 && b[1] == 0 && b[2] == 0 && b[3] == 0 && b[4] == 0 && b[5] == 0 &&
      b[6] == 0 && b[7] == 0 && b[8] == 0 && b[9] == 0 && b[10] == 0 && b[11] == 0 &&
      b[12] == 0 && b[13] == 0 && b[14] == 0 && b[15] == 1)
    return true;  // ::1
  if ((b[0] & 0xFE) == 0xFC) return true;           // fc00::/7
  if (b[0] == 0xFE && (b[1] & 0xC0) == 0x80) return true;  // fe80::/10
  if (b[0] == 0xFF) return true;                    // multicast
  if (b[0] == 0x20 && b[1] == 0x01 && b[2] == 0x0D && b[3] == 0xB8) return true;  // doc
  if (b[0] == 0x20 && b[1] == 0x02) return true;    // 6to4
  if (b[0] == 0x20 && b[1] == 0x01 && b[2] == 0x00 && b[3] == 0x00) return true;  // Teredo
  // IPv4 mapeada ::ffff:a.b.c.d
  bool mapped = true;
  for (int i = 0; i < 10; ++i) mapped = mapped && b[i] == 0;
  mapped = mapped && b[10] == 0xFF && b[11] == 0xFF;
  if (mapped) {
    struct in_addr v4{};
    std::memcpy(&v4, b + 12, 4);
    return is_private_v4(v4);
  }
  return false;
}

bool is_public_address(const struct sockaddr& sa) {
  if (sa.sa_family == AF_INET) {
    return !is_private_v4(reinterpret_cast<const sockaddr_in&>(sa).sin_addr);
  }
  if (sa.sa_family == AF_INET6) {
    return !is_private_v6(reinterpret_cast<const sockaddr_in6&>(sa).sin6_addr);
  }
  return false;
}

std::string ip_string(const struct sockaddr& sa) {
  char buf[INET6_ADDRSTRLEN] = {0};
  if (sa.sa_family == AF_INET) {
    const auto& v4 = reinterpret_cast<const sockaddr_in&>(sa);
    inet_ntop(AF_INET, &v4.sin_addr, buf, sizeof buf);
  } else if (sa.sa_family == AF_INET6) {
    const auto& v6 = reinterpret_cast<const sockaddr_in6&>(sa);
    inet_ntop(AF_INET6, &v6.sin6_addr, buf, sizeof buf);
  }
  return buf;
}

bool host_allowed(std::string_view host, const UrlPolicy& policy) {
  if (policy.allowed_suffixes.empty()) return true;
  for (const auto& suffix : policy.allowed_suffixes) {
    if (host == suffix) return true;
    if (host.size() > suffix.size() &&
        host.substr(host.size() - suffix.size()) == suffix &&
        host[host.size() - suffix.size() - 1] == '.')
      return true;
  }
  return false;
}

bool parse_port(std::string_view text, int& port) {
  if (text.empty() || text.size() > 5) return false;
  int value = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') return false;
    value = value * 10 + (c - '0');
  }
  if (value < 1 || value > 65535) return false;
  port = value;
  return true;
}

}  // namespace

UrlCheck validate_url(std::string_view url, const UrlPolicy& policy) {
  UrlCheck out;
  if (url.empty() || url.size() > 2048) {
    out.error = "URL vacía o demasiado larga";
    return out;
  }
  const auto scheme_end = url.find("://");
  if (scheme_end == std::string_view::npos) {
    out.error = "URL sin esquema";
    return out;
  }
  const auto scheme = to_lower(url.substr(0, scheme_end));
  if (scheme != "https" && scheme != "http") {
    out.error = "esquema no permitido";
    return out;
  }
  if (scheme == "http" && !policy.allow_http) {
    out.error = "sólo se permite https (ALLOW_HTTP=0)";
    return out;
  }
  out.scheme = scheme;
  out.port = scheme == "https" ? 443 : 80;

  auto authority = url.substr(scheme_end + 3);
  const auto path_pos = authority.find_first_of("/?#");
  const auto host_port = authority.substr(0, path_pos);
  const auto path = path_pos == std::string_view::npos ? std::string() : std::string(authority.substr(path_pos));
  if (host_port.empty()) {
    out.error = "URL sin host";
    return out;
  }
  if (host_port.find('@') != std::string_view::npos) {
    out.error = "no se admiten credenciales en la URL";
    return out;
  }
  std::string host;
  std::string port_text;
  if (host_port.front() == '[') {
    const auto close = host_port.find(']');
    if (close == std::string_view::npos) {
      out.error = "IPv6 mal formada";
      return out;
    }
    host = std::string(host_port.substr(1, close - 1));
    if (close + 1 < host_port.size()) {
      if (host_port[close + 1] != ':') {
        out.error = "puerto mal formado";
        return out;
      }
      port_text = std::string(host_port.substr(close + 2));
    }
  } else {
    const auto colon = host_port.rfind(':');
    host = std::string(host_port.substr(0, colon));
    if (colon != std::string_view::npos) port_text = std::string(host_port.substr(colon + 1));
  }
  if (host.empty()) {
    out.error = "URL sin host";
    return out;
  }
  if (!port_text.empty() && !parse_port(port_text, out.port)) {
    out.error = "puerto inválido";
    return out;
  }
  if (!host_allowed(host, policy)) {
    out.error = "host fuera de la lista permitida";
    return out;
  }
  out.host = host;

  struct addrinfo hints {};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo* result = nullptr;
  const auto rc = getaddrinfo(host.c_str(), std::to_string(out.port).c_str(), &hints, &result);
  if (rc != 0 || result == nullptr) {
    out.error = "no se pudo resolver el host";
    return out;
  }
  bool any_public = false;
  std::string chosen;
  for (auto* it = result; it != nullptr; it = it->ai_next) {
    if (it->ai_addr == nullptr) continue;
    if (policy.allow_private) {
      if (chosen.empty()) chosen = ip_string(*it->ai_addr);
      any_public = true;
    } else if (is_public_address(*it->ai_addr)) {
      any_public = true;
      if (chosen.empty()) chosen = ip_string(*it->ai_addr);
    }
  }
  freeaddrinfo(result);
  if (!any_public) {
    out.error = "el host resuelve a una dirección privada o reservada";
    return out;
  }
  out.pinned_ip = chosen;
  out.normalized = scheme + "://" + host;
  if (out.port != (scheme == "https" ? 443 : 80))
    out.normalized += ":" + std::to_string(out.port);
  out.normalized += path;
  out.ok = true;
  return out;
}

std::string resolve_location(std::string_view base_url,
                             std::string_view location) {
  if (location.empty()) return {};
  if (starts_with(to_lower(location), "https://") ||
      starts_with(to_lower(location), "http://"))
    return std::string(location);
  const auto scheme_end = base_url.find("://");
  if (scheme_end == std::string_view::npos) return {};
  const auto scheme = base_url.substr(0, scheme_end);
  const auto authority_start = scheme_end + 3;
  const auto authority_end = base_url.find_first_of("/?#", authority_start);
  const auto authority = base_url.substr(
      authority_start, authority_end == std::string_view::npos
                           ? std::string_view::npos
                           : authority_end - authority_start);
  if (starts_with(location, "//")) return std::string(scheme) + ":" + std::string(location);
  if (location.front() == '/')
    return std::string(scheme) + "://" + std::string(authority) + std::string(location);

  // Ruta relativa: se resuelve contra el directorio del base.
  std::string base_path;
  if (authority_end != std::string_view::npos) base_path = std::string(base_url.substr(authority_end));
  const auto query = base_path.find_first_of("?#");
  if (query != std::string::npos) base_path.resize(query);
  const auto slash = base_path.rfind('/');
  if (slash == std::string::npos) base_path = "/";
  else base_path.resize(slash + 1);
  return std::string(scheme) + "://" + std::string(authority) + base_path +
         std::string(location);
}

}  // namespace tds
