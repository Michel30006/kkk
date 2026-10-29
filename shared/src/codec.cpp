#include "tds/codec.hpp"

#include <array>

namespace tds {

std::string to_hex(const void* data, std::size_t size) {
  static const char* digits = "0123456789abcdef";
  const auto* bytes = static_cast<const unsigned char*>(data);
  std::string out;
  out.resize(size * 2);
  for (std::size_t i = 0; i < size; ++i) {
    out[i * 2] = digits[bytes[i] >> 4];
    out[i * 2 + 1] = digits[bytes[i] & 0x0F];
  }
  return out;
}

std::string to_hex(std::string_view data) { return to_hex(data.data(), data.size()); }

bool from_hex(std::string_view hex, std::string& out) {
  if (hex.size() % 2 != 0) return false;
  auto value = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  out.clear();
  out.resize(hex.size() / 2);
  for (std::size_t i = 0; i < out.size(); ++i) {
    const int hi = value(hex[i * 2]);
    const int lo = value(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<char>((hi << 4) | lo);
  }
  return true;
}

std::string base64_encode(std::string_view data) {
  static const char* alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((data.size() + 2) / 3) * 4);
  std::size_t i = 0;
  while (i + 3 <= data.size()) {
    const auto a = static_cast<unsigned char>(data[i]);
    const auto b = static_cast<unsigned char>(data[i + 1]);
    const auto c = static_cast<unsigned char>(data[i + 2]);
    out.push_back(alphabet[a >> 2]);
    out.push_back(alphabet[((a & 0x03) << 4) | (b >> 4)]);
    out.push_back(alphabet[((b & 0x0F) << 2) | (c >> 6)]);
    out.push_back(alphabet[c & 0x3F]);
    i += 3;
  }
  const auto rest = data.size() - i;
  if (rest == 1) {
    const auto a = static_cast<unsigned char>(data[i]);
    out.push_back(alphabet[a >> 2]);
    out.push_back(alphabet[(a & 0x03) << 4]);
    out.append("==");
  } else if (rest == 2) {
    const auto a = static_cast<unsigned char>(data[i]);
    const auto b = static_cast<unsigned char>(data[i + 1]);
    out.push_back(alphabet[a >> 2]);
    out.push_back(alphabet[((a & 0x03) << 4) | (b >> 4)]);
    out.push_back(alphabet[(b & 0x0F) << 2]);
    out.push_back('=');
  }
  return out;
}

bool base64_decode(std::string_view text, std::string& out) {
  auto value = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  out.clear();
  if (text.size() % 4 != 0) return false;
  out.reserve(text.size() / 4 * 3);
  for (std::size_t i = 0; i < text.size(); i += 4) {
    int v[4];
    int padding = 0;
    for (int k = 0; k < 4; ++k) {
      const char c = text[i + static_cast<std::size_t>(k)];
      if (c == '=') {
        v[k] = 0;
        ++padding;
      } else {
        v[k] = value(c);
        if (v[k] < 0) return false;
      }
    }
    if (padding > 2 || (padding > 0 && i + 4 != text.size())) return false;
    const auto triple = (v[0] << 18) | (v[1] << 12) | (v[2] << 6) | v[3];
    out.push_back(static_cast<char>((triple >> 16) & 0xFF));
    if (padding < 2) out.push_back(static_cast<char>((triple >> 8) & 0xFF));
    if (padding < 1) out.push_back(static_cast<char>(triple & 0xFF));
  }
  return true;
}

}  // namespace tds
