#include "tds/crc32.hpp"

#include <array>

namespace tds {
namespace {

const std::array<std::uint32_t, 256>& table() {
  static const std::array<std::uint32_t, 256> t = [] {
    std::array<std::uint32_t, 256> out{};
    for (std::uint32_t i = 0; i < 256; ++i) {
      std::uint32_t c = i;
      for (int k = 0; k < 8; ++k)
        c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      out[i] = c;
    }
    return out;
  }();
  return t;
}

}  // namespace

std::uint32_t crc32(std::string_view data) {
  std::uint32_t crc = 0xFFFFFFFFu;
  for (const auto ch : data) {
    const auto idx = (crc ^ static_cast<unsigned char>(ch)) & 0xFFu;
    crc = table()[idx] ^ (crc >> 8);
  }
  return crc ^ 0xFFFFFFFFu;
}

}  // namespace tds
