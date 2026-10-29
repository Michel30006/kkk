// Trama de transporte por ToDus.
//
//   TDS1|<job_id>|<transfer_id>|<chunk_index>|<total_chunks>|<enc_ver>|<crc32>|<payload>
//
// Los identificadores se limitan a [A-Za-z0-9_-] y el payload usa la tabla v1,
// que no contiene '|'. Así el marco es inequívoco sin escapes.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace tds {

inline constexpr std::string_view kFrameMagic = "TDS1";

struct Frame {
  std::string job_id;
  std::string transfer_id;
  std::uint32_t chunk_index{0};
  std::uint32_t total_chunks{0};
  std::uint16_t encoding_version{1};
  std::uint32_t payload_crc32{0};
  std::string payload;  // texto codificado (UTF-8)
  std::uint64_t payload_bytes{0};  // bytes de vídeo que representa

  std::size_t wire_size() const;  // bytes UTF-8 de la trama completa
};

std::string serialize_frame(const Frame& frame);
bool parse_frame(std::string_view wire, Frame& out, std::string& err);

// Reserva de cabecera para calcular el presupuesto de payload sin depender de
// los dígitos de los índices. Se comprueba el tamaño exacto al serializar.
std::size_t frame_header_reserve(std::string_view job_id,
                                 std::string_view transfer_id);
std::size_t payload_budget(std::size_t max_frame_bytes,
                           std::string_view job_id,
                           std::string_view transfer_id);

// Recorre el texto codificado (repartido en `parts`) y emite tramas
// consecutivas empezando en `first_index`. `total_chunks` debe conocerse de
// antemano (ver count_frames). El sink puede devolver false para detenerse.
// Devuelve false sólo ante un error de datos.
using FrameSink = std::function<bool(Frame&&)>;
bool iterate_frames(const std::vector<std::string_view>& parts,
                    std::string_view job_id, std::string_view transfer_id,
                    std::uint32_t total_chunks, std::size_t max_frame_bytes,
                    std::uint32_t first_index, const FrameSink& sink,
                    std::uint32_t& emitted, std::string& err);

// Mismo recorrido, sin construir las tramas, para saber cuántas saldrán.
bool count_frames(const std::vector<std::string_view>& parts,
                  std::string_view job_id, std::string_view transfer_id,
                  std::size_t max_frame_bytes, std::uint32_t& total_out,
                  std::string& err);

}  // namespace tds
