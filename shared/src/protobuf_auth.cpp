#include "tds/protobuf_auth.hpp"

#include <cstddef>

namespace tds {
namespace {

void put_varint(std::string& out, std::uint64_t value) {
  while (value >= 0x80) {
    out.push_back(static_cast<char>((value & 0x7F) | 0x80));
    value >>= 7;
  }
  out.push_back(static_cast<char>(value));
}

void put_string(std::string& out, int field, std::string_view value) {
  put_varint(out, static_cast<std::uint64_t>((field << 3) | 2));
  put_varint(out, value.size());
  out.append(value);
}

bool read_varint(std::string_view body, std::size_t& pos, std::uint64_t& value) {
  value = 0;
  int shift = 0;
  while (pos < body.size()) {
    const auto b = static_cast<unsigned char>(body[pos++]);
    value |= static_cast<std::uint64_t>(b & 0x7F) << shift;
    if ((b & 0x80) == 0) return true;
    shift += 7;
    if (shift > 63) return false;
  }
  return false;
}

}  // namespace

std::string encode_auth_token_request(std::string_view username,
                                      std::string_view password,
                                      std::string_view version) {
  std::string out;
  put_string(out, 1, username);
  if (!password.empty()) put_string(out, 2, password);
  if (!version.empty()) put_string(out, 3, version);
  return out;
}

bool decode_auth_token_response(std::string_view body, std::string& token_out,
                                std::string& err) {
  token_out.clear();
  std::size_t pos = 0;
  while (pos < body.size()) {
    std::uint64_t key = 0;
    if (!read_varint(body, pos, key)) {
      err = "varint de campo inválido";
      return false;
    }
    const auto field = key >> 3;
    const auto wire = key & 0x07;
    if (wire == 0) {
      std::uint64_t ignored = 0;
      if (!read_varint(body, pos, ignored)) {
        err = "varint de valor inválido";
        return false;
      }
    } else if (wire == 2) {
      std::uint64_t len = 0;
      if (!read_varint(body, pos, len)) {
        err = "longitud inválida";
        return false;
      }
      if (len > body.size() - pos) {
        err = "campo truncado";
        return false;
      }
      if (field == 2) token_out.assign(body.substr(pos, len));
      pos += static_cast<std::size_t>(len);
    } else if (wire == 1) {
      if (body.size() - pos < 8) {
        err = "campo fixed64 truncado";
        return false;
      }
      pos += 8;
    } else if (wire == 5) {
      if (body.size() - pos < 4) {
        err = "campo fixed32 truncado";
        return false;
      }
      pos += 4;
    } else {
      err = "wire type no soportado";
      return false;
    }
  }
  if (token_out.empty()) {
    err = "la respuesta no contiene tokenString";
    return false;
  }
  return true;
}

}  // namespace tds
