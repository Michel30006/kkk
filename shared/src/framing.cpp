#include "tds/framing.hpp"

#include <array>
#include <cstdio>
#include <limits>

#include "tds/codec.hpp"
#include "tds/crc32.hpp"
#include "tds/encoding.hpp"
#include "tds/util.hpp"

namespace tds {
namespace {

constexpr std::size_t kMaxIdLength = 64;
constexpr std::uint32_t kMaxChunks = 1'000'000;

std::size_t digits(std::uint32_t value) {
  std::size_t n = 1;
  while (value >= 10) {
    value /= 10;
    ++n;
  }
  return n;
}

// Longitud de cabecera contando 5 dígitos para índice y total, de modo que el
// presupuesto de payload no dependa de los valores concretos.
std::size_t conservative_header(std::string_view job_id,
                                std::string_view transfer_id) {
  return kFrameMagic.size() + 1 + job_id.size() + 1 + transfer_id.size() + 1 +
         5 + 1 + 5 + 1 + 5 + 1 + 8 + 1;
}

bool valid_field(std::string_view text) {
  for (const char c : text) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '_' || c == '-';
    if (!ok) return false;
  }
  return true;
}

}  // namespace

std::size_t Frame::wire_size() const {
  return kFrameMagic.size() + 1 + job_id.size() + 1 + transfer_id.size() + 1 +
         digits(chunk_index) + 1 + digits(total_chunks) + 1 +
         digits(encoding_version) + 1 + 8 + 1 + payload.size();
}

std::string serialize_frame(const Frame& frame) {
  std::string out;
  out.reserve(frame.wire_size() + 8);
  out.append(kFrameMagic);
  out.push_back('|');
  out.append(frame.job_id);
  out.push_back('|');
  out.append(frame.transfer_id);
  out.push_back('|');
  out.append(std::to_string(frame.chunk_index));
  out.push_back('|');
  out.append(std::to_string(frame.total_chunks));
  out.push_back('|');
  out.append(std::to_string(frame.encoding_version));
  out.push_back('|');
  char crc[9];
  std::snprintf(crc, sizeof crc, "%08x",
                crc32(frame.payload));
  out.append(crc);
  out.push_back('|');
  out.append(frame.payload);
  return out;
}

bool parse_frame(std::string_view wire, Frame& out, std::string& err) {
  std::array<std::string_view, 7> fields{};
  std::size_t start = 0;
  std::size_t field = 0;
  while (field < 7) {
    const auto pos = wire.find('|', start);
    if (pos == std::string_view::npos) {
      err = "trama truncada: faltan campos";
      return false;
    }
    fields[field++] = wire.substr(start, pos - start);
    start = pos + 1;
  }
  if (fields[0] != kFrameMagic) {
    err = "magic de trama desconocido";
    return false;
  }
  if (fields[1].empty() || fields[1].size() > kMaxIdLength ||
      !valid_field(fields[1])) {
    err = "job_id inválido";
    return false;
  }
  if (fields[2].empty() || fields[2].size() > kMaxIdLength ||
      !valid_field(fields[2])) {
    err = "transfer_id inválido";
    return false;
  }
  auto parse_u32 = [&](std::string_view text, std::uint32_t& value) {
    if (text.empty() || text.size() > 10) return false;
    std::uint64_t acc = 0;
    for (const char c : text) {
      if (c < '0' || c > '9') return false;
      acc = acc * 10 + static_cast<std::uint64_t>(c - '0');
      if (acc > std::numeric_limits<std::uint32_t>::max()) return false;
    }
    value = static_cast<std::uint32_t>(acc);
    return true;
  };
  std::uint32_t total = 0;
  std::uint32_t index = 0;
  if (!parse_u32(fields[3], index) || !parse_u32(fields[4], total)) {
    err = "índices de trama inválidos";
    return false;
  }
  if (total == 0 || total > kMaxChunks || index >= total) {
    err = "índices de trama fuera de rango";
    return false;
  }
  std::uint32_t version = 0;
  if (!parse_u32(fields[5], version) || version > 65535) {
    err = "versión de codificación inválida";
    return false;
  }
  if (fields[6].size() != 8) {
    err = "crc de trama inválido";
    return false;
  }
  std::uint32_t expected_crc = 0;
  for (const char c : fields[6]) {
    int v = -1;
    if (c >= '0' && c <= '9') v = c - '0';
    else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
    if (v < 0) {
      err = "crc de trama no hexadecimal";
      return false;
    }
    expected_crc = (expected_crc << 4) | static_cast<std::uint32_t>(v);
  }
  const auto payload = wire.substr(start);
  if (payload.empty()) {
    err = "payload vacío";
    return false;
  }
  if (crc32(payload) != expected_crc) {
    err = "crc de payload no coincide";
    return false;
  }
  std::uint64_t decoded = 0;
  std::uint64_t utf8_bytes = 0;
  if (!measure_encoded(payload, decoded, utf8_bytes, err)) return false;
  out.job_id = std::string(fields[1]);
  out.transfer_id = std::string(fields[2]);
  out.chunk_index = index;
  out.total_chunks = total;
  out.encoding_version = static_cast<std::uint16_t>(version);
  out.payload_crc32 = expected_crc;
  out.payload = std::string(payload);
  out.payload_bytes = decoded;
  return true;
}

std::size_t frame_header_reserve(std::string_view job_id,
                                 std::string_view transfer_id) {
  return conservative_header(job_id, transfer_id);
}

std::size_t payload_budget(std::size_t max_frame_bytes,
                           std::string_view job_id,
                           std::string_view transfer_id) {
  const auto header = conservative_header(job_id, transfer_id);
  if (max_frame_bytes <= header + 1) return 0;
  return max_frame_bytes - header;
}

bool iterate_frames(const std::vector<std::string_view>& parts,
                    std::string_view job_id, std::string_view transfer_id,
                    std::uint32_t total_chunks, std::size_t max_frame_bytes,
                    std::uint32_t first_index, const FrameSink& sink,
                    std::uint32_t& emitted, std::string& err) {
  emitted = 0;
  const auto budget = payload_budget(max_frame_bytes, job_id, transfer_id);
  if (budget < 2) {
    err = "presupuesto de payload insuficiente para el límite de stanza";
    return false;
  }
  std::uint32_t index = first_index;
  std::string payload;
  std::uint64_t payload_bytes = 0;

  auto flush = [&]() -> bool {
    if (payload.empty()) return true;
    Frame frame;
    frame.job_id = std::string(job_id);
    frame.transfer_id = std::string(transfer_id);
    frame.chunk_index = index;
    frame.total_chunks = total_chunks;
    frame.encoding_version = kEncodingVersion;
    frame.payload_crc32 = crc32(payload);
    frame.payload = std::move(payload);
    frame.payload_bytes = payload_bytes;
    payload.clear();
    payload_bytes = 0;
    if (frame.wire_size() > max_frame_bytes) {
      err = "trama generada supera el límite configurado";
      return false;
    }
    if (!sink(std::move(frame))) {
      err = "sink detuvo la iteración";
      return false;
    }
    ++index;
    ++emitted;
    return true;
  };

  for (const auto part : parts) {
    std::size_t pos = 0;
    while (pos < part.size()) {
      const auto b = static_cast<unsigned char>(part[pos]);
      std::size_t len = 1;
      if ((b & 0xE0) == 0xC0) len = 2;
      else if ((b & 0xF0) == 0xE0) len = 3;
      else if ((b & 0xF8) == 0xF0) len = 4;
      if (pos + len > part.size()) {
        err = "parte termina en mitad de un símbolo UTF-8";
        return false;
      }
      if (!payload.empty() && payload.size() + len > budget) {
        if (!flush()) return false;
      }
      payload.append(part.substr(pos, len));
      payload_bytes += 1;
      pos += len;
    }
  }
  if (!flush()) return false;
  if (total_chunks != 0 && first_index + emitted > total_chunks) {
    err = "se generaron más tramas que total_chunks";
    return false;
  }
  return true;
}

bool count_frames(const std::vector<std::string_view>& parts,
                  std::string_view job_id, std::string_view transfer_id,
                  std::size_t max_frame_bytes, std::uint32_t& total_out,
                  std::string& err) {
  std::uint32_t count = 0;
  std::uint32_t emitted = 0;
  // total_chunks se rellena con 0 durante el conteo; el tamaño de cabecera se
  // calcula con reserva fija, así que el recuento no cambia.
  if (!iterate_frames(parts, job_id, transfer_id, 0, max_frame_bytes, 0,
                      [&](Frame&&) {
                        ++count;
                        return true;
                      },
                      emitted, err)) {
    return false;
  }
  total_out = count;
  return true;
}

}  // namespace tds
