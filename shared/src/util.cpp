#include "tds/util.hpp"

#include <openssl/rand.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>

namespace tds {

bool is_valid_id(std::string_view id) {
  if (id.empty() || id.size() > 64) return false;
  for (const char c : id) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '_' || c == '-';
    if (!ok) return false;
  }
  return true;
}

std::string random_hex(std::size_t bytes) {
  std::string raw(bytes, '\0');
  if (RAND_bytes(reinterpret_cast<unsigned char*>(raw.data()),
                 static_cast<int>(bytes)) != 1) {
    throw std::runtime_error("RAND_bytes falló");
  }
  static const char* digits = "0123456789abcdef";
  std::string out;
  out.resize(bytes * 2);
  for (std::size_t i = 0; i < bytes; ++i) {
    const auto b = static_cast<unsigned char>(raw[i]);
    out[i * 2] = digits[b >> 4];
    out[i * 2 + 1] = digits[b & 0x0F];
  }
  return out;
}

std::string new_uuid() {
  std::string raw(16, '\0');
  if (RAND_bytes(reinterpret_cast<unsigned char*>(raw.data()), 16) != 1) {
    throw std::runtime_error("RAND_bytes falló");
  }
  auto* b = reinterpret_cast<unsigned char*>(raw.data());
  b[6] = static_cast<unsigned char>((b[6] & 0x0F) | 0x40);
  b[8] = static_cast<unsigned char>((b[8] & 0x3F) | 0x80);
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (int i = 0; i < 16; ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) out << '-';
    out << std::setw(2) << static_cast<unsigned>(b[i]);
  }
  return out.str();
}

std::string now_iso8601(std::int64_t unix_seconds) {
  const std::time_t t = static_cast<std::time_t>(unix_seconds);
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[32];
  std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buf;
}

std::string now_iso8601() { return now_iso8601(unix_now()); }

std::int64_t unix_now() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string trim(std::string_view text) {
  std::size_t begin = 0;
  std::size_t end = text.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])))
    ++begin;
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(text[end - 1])))
    --end;
  return std::string(text.substr(begin, end - begin));
}

std::string to_lower(std::string_view text) {
  std::string out(text);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

std::vector<std::string> split(std::string_view text, char separator) {
  std::vector<std::string> out;
  std::size_t start = 0;
  while (true) {
    const auto pos = text.find(separator, start);
    if (pos == std::string_view::npos) {
      out.emplace_back(text.substr(start));
      break;
    }
    out.emplace_back(text.substr(start, pos - start));
    start = pos + 1;
  }
  return out;
}

bool starts_with(std::string_view text, std::string_view prefix) {
  return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

bool ends_with(std::string_view text, std::string_view suffix) {
  return text.size() >= suffix.size() &&
         text.substr(text.size() - suffix.size()) == suffix;
}

std::string_view basename_of(std::string_view path) {
  const auto pos = path.find_last_of("/\\");
  return pos == std::string_view::npos ? path : path.substr(pos + 1);
}

std::string todus_jid(std::string_view recipient, std::string_view domain) {
  std::string user(recipient);
  // Acepta el JID completo tal cual.
  const auto at = user.find('@');
  if (at != std::string::npos) {
    if (at == 0 || at + 1 >= user.size()) return {};
    return user;
  }
  if (user.empty() || user.size() > 16) return {};
  for (const char c : user)
    if (c < '0' || c > '9') return {};
  // 8 dígitos = teléfono; se antepone el prefijo 53.
  if (user.size() == 8) user = "53" + user;
  if (user.size() != 10 || user.substr(0, 2) != "53") return {};
  return user + "@" + std::string(domain);
}

}  // namespace tds
