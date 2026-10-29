#include "tds/encoding.hpp"

#include <array>
#include <cstdio>
#include <set>
#include <vector>

#include "encoding_table.inc"

namespace tds {
namespace {

// La tabla v1 vive en el rango U+0021..U+017F, así que una LUT directa basta.
constexpr std::size_t kLutSize = 0x180;

const std::array<std::int16_t, kLutSize>& decode_lut() {
  static const std::array<std::int16_t, kLutSize> lut = [] {
    std::array<std::int16_t, kLutSize> t{};
    t.fill(-1);
    for (std::size_t i = 0; i < kSymbolCount; ++i) {
      const auto cp = kTableCodepoints[i];
      if (cp < kLutSize) t[cp] = static_cast<std::int16_t>(i);
    }
    return t;
  }();
  return lut;
}

bool next_codepoint(std::string_view text, std::size_t& pos, char32_t& cp,
                    std::string& err) {
  if (pos >= text.size()) {
    err = "fin de texto inesperado";
    return false;
  }
  const auto b0 = static_cast<unsigned char>(text[pos]);
  if (b0 < 0x80) {
    cp = b0;
    ++pos;
    return true;
  }
  int extra = 0;
  char32_t value = 0;
  char32_t min = 0;
  if ((b0 & 0xE0) == 0xC0) {
    extra = 1;
    value = b0 & 0x1F;
    min = 0x80;
  } else if ((b0 & 0xF0) == 0xE0) {
    extra = 2;
    value = b0 & 0x0F;
    min = 0x800;
  } else if ((b0 & 0xF8) == 0xF0) {
    extra = 3;
    value = b0 & 0x07;
    min = 0x10000;
  } else {
    err = "byte inicial UTF-8 inválido";
    return false;
  }
  if (pos + static_cast<std::size_t>(extra) >= text.size()) {
    err = "secuencia UTF-8 truncada";
    return false;
  }
  for (int i = 1; i <= extra; ++i) {
    const auto b = static_cast<unsigned char>(text[pos + static_cast<std::size_t>(i)]);
    if ((b & 0xC0) != 0x80) {
      err = "continuación UTF-8 inválida";
      return false;
    }
    value = (value << 6) | (b & 0x3F);
  }
  if (value < min) {
    err = "secuencia UTF-8 sobrelarga";
    return false;
  }
  if (value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
    err = "code point UTF-8 no válido";
    return false;
  }
  cp = value;
  pos += static_cast<std::size_t>(extra) + 1;
  return true;
}

std::size_t utf8_length(char32_t cp) {
  if (cp < 0x80) return 1;
  if (cp < 0x800) return 2;
  if (cp < 0x10000) return 3;
  return 4;
}

}  // namespace

std::string_view table_sha256() { return kTableSha256; }

std::string_view symbol_for(std::uint8_t b) { return kTableUtf8[b]; }

bool decode_symbol(char32_t cp, std::uint8_t& out) {
  if (cp >= kLutSize) return false;
  const auto v = decode_lut()[cp];
  if (v < 0) return false;
  out = static_cast<std::uint8_t>(v);
  return true;
}

void encode_append(std::span<const std::uint8_t> bytes, std::string& out) {
  out.reserve(out.size() + bytes.size() * 2);
  for (const auto b : bytes) out.append(kTableUtf8[b]);
}

std::string encode(std::span<const std::uint8_t> bytes) {
  std::string out;
  encode_append(bytes, out);
  return out;
}

bool measure_encoded(std::string_view utf8, std::uint64_t& decoded_bytes,
                     std::uint64_t& utf8_bytes, std::string& err) {
  decoded_bytes = 0;
  utf8_bytes = 0;
  std::size_t pos = 0;
  while (pos < utf8.size()) {
    char32_t cp = 0;
    if (!next_codepoint(utf8, pos, cp, err)) return false;
    std::uint8_t b = 0;
    if (!decode_symbol(cp, b)) {
      err = "símbolo fuera de la tabla v1";
      return false;
    }
    ++decoded_bytes;
    utf8_bytes += utf8_length(cp);
  }
  return true;
}

bool decode_text(std::string_view utf8, std::string& bytes_out, std::string& err) {
  bytes_out.clear();
  bytes_out.reserve(utf8.size());
  std::size_t pos = 0;
  while (pos < utf8.size()) {
    char32_t cp = 0;
    if (!next_codepoint(utf8, pos, cp, err)) return false;
    std::uint8_t b = 0;
    if (!decode_symbol(cp, b)) {
      char buf[64];
      std::snprintf(buf, sizeof buf, "símbolo U+%04X fuera de la tabla v1",
                    static_cast<unsigned>(cp));
      err = buf;
      return false;
    }
    bytes_out.push_back(static_cast<char>(b));
  }
  return true;
}

bool validate_table(std::string& err) {
  std::set<char32_t> seen;
  for (std::size_t i = 0; i < kSymbolCount; ++i) {
    const auto cp = kTableCodepoints[i];
    if (!seen.insert(cp).second) {
      err = "símbolo duplicado en la tabla";
      return false;
    }
    if (cp <= 0x20 || (cp >= 0x7F && cp < 0xA0)) {
      err = "símbolo no imprimible";
      return false;
    }
    if (cp == '<' || cp == '>' || cp == '&' || cp == '"' || cp == '\'' ||
        cp == '|') {
      err = "símbolo incompatible con XML o con el separador de trama";
      return false;
    }
    const auto sv = kTableUtf8[i];
    if (sv != symbol_for(static_cast<std::uint8_t>(i))) {
      err = "tabla UTF-8 incoherente";
      return false;
    }
    std::uint8_t back = 0;
    if (!decode_symbol(cp, back) || back != static_cast<std::uint8_t>(i)) {
      err = "la tabla no es reversible";
      return false;
    }
    if (sv.empty() || sv.size() > 2) {
      err = "símbolo con longitud UTF-8 inesperada";
      return false;
    }
  }
  return true;
}

}  // namespace tds
