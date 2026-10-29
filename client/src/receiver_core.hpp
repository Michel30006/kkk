// Núcleo del receptor, independiente de la red para poder probarlo.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "tds/reassembler.hpp"

namespace tds {

struct ReceiverResult {
  bool ok{false};
  std::string error;
  std::string out_path;
  std::string sha256;
  std::uint64_t size{0};
};

// Metadatos públicos que el receptor necesita para verificar.
bool parse_job_meta_json(const std::string& json_text, JobMeta& out,
                         std::string& err);

// Lee un directorio donde cada fichero contiene exactamente una trama.
// Devuelve los contenidos ordenados por nombre (el emisor numera en secuencia).
bool load_frames_dir(const std::string& dir, std::vector<std::string>& out,
                     std::string& err);

// Alimenta el reassembler con las tramas, comprueba tamaño y SHA-256 y escribe
// el fichero sólo si la verificación es correcta.
ReceiverResult reassemble_frames(const std::vector<std::string>& wires,
                                 const JobMeta& meta,
                                 const std::string& out_path);

// Escribe bytes de forma atómica (tmp + rename).
bool write_file_atomic(const std::string& path, const std::string& bytes,
                       std::string& err);

}  // namespace tds
