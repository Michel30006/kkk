#include "receiver_core.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "tds/util.hpp"

namespace tds {

bool parse_job_meta_json(const std::string& json_text, JobMeta& out,
                         std::string& err) {
  nlohmann::json body;
  try {
    body = nlohmann::json::parse(json_text);
  } catch (...) {
    err = "respuesta de metadatos no es JSON válido";
    return false;
  }
  if (!body.is_object()) {
    err = "los metadatos no son un objeto JSON";
    return false;
  }
  out.job_id = body.value("job_id", "");
  out.transfer_id = body.value("transfer_id", "");
  out.status = body.value("status", "");
  out.sha256 = body.value("sha256", "");
  out.size_bytes = body.value("size_bytes", static_cast<std::uint64_t>(0));
  out.encoding_version = body.value("encoding_version", 1);
  out.total_chunks = body.value("total_chunks", static_cast<std::uint32_t>(0));
  out.error_code = body.value("error_code", "");

  if (out.job_id.empty() || !is_valid_id(out.job_id)) {
    err = "metadatos sin job_id válido";
    return false;
  }
  if (out.transfer_id.empty() || !is_valid_id(out.transfer_id)) {
    err = "metadatos sin transfer_id válido";
    return false;
  }
  if (out.size_bytes == 0) {
    err = "metadatos sin size_bytes (¿el trabajo aún no está codificado?)";
    return false;
  }
  if (out.sha256.size() != 64) {
    err = "metadatos sin SHA-256 válido (¿el trabajo aún no está codificado?)";
    return false;
  }
  if (out.encoding_version < 1) {
    err = "versión de codificación inválida en los metadatos";
    return false;
  }
  return true;
}

bool load_frames_dir(const std::string& dir, std::vector<std::string>& out,
                     std::string& err) {
  namespace fs = std::filesystem;
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) {
    err = "no es un directorio: " + dir;
    return false;
  }
  std::vector<fs::path> files;
  for (const auto& entry : fs::directory_iterator(dir, ec)) {
    if (!entry.is_regular_file(ec)) continue;
    if (entry.path().extension() == ".tmp") continue;
    files.push_back(entry.path());
  }
  if (ec) {
    err = "no se pudo leer el directorio: " + ec.message();
    return false;
  }
  if (files.empty()) {
    err = "el directorio de tramas está vacío";
    return false;
  }
  std::sort(files.begin(), files.end());
  out.clear();
  out.reserve(files.size());
  for (const auto& path : files) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
      err = "no se pudo abrir " + path.string();
      return false;
    }
    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    if (text.empty()) {
      err = "fichero de trama vacío: " + path.string();
      return false;
    }
    out.push_back(std::move(text));
  }
  return true;
}

bool write_file_atomic(const std::string& path, const std::string& bytes,
                       std::string& err) {
  const auto tmp = path + ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
      err = "no se puede crear " + tmp;
      return false;
    }
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.flush();
    if (!out) {
      err = "fallo escribiendo " + tmp;
      std::remove(tmp.c_str());
      return false;
    }
  }
  std::error_code ec;
  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    err = "no se pudo publicar " + path + ": " + ec.message();
    std::remove(tmp.c_str());
    return false;
  }
  return true;
}

ReceiverResult reassemble_frames(const std::vector<std::string>& wires,
                                 const JobMeta& meta,
                                 const std::string& out_path) {
  ReceiverResult result;
  Reassembler reassembler(meta);
  std::string err;
  for (const auto& wire : wires) {
    const auto outcome = reassembler.add(wire, err);
    if (outcome == AddOutcome::kRejected) {
      result.error = "trama rechazada: " + err;
      return result;
    }
  }
  if (!reassembler.complete()) {
    const auto gaps = reassembler.missing();
    result.error = "faltan " + std::to_string(gaps.size()) + " tramas";
    if (!gaps.empty())
      result.error += " (primera: " + std::to_string(gaps.front()) + ")";
    return result;
  }
  std::string bytes;
  if (!reassembler.reconstruct(bytes, err)) {
    result.error = err;
    return result;
  }
  if (!write_file_atomic(out_path, bytes, err)) {
    result.error = err;
    return result;
  }
  result.ok = true;
  result.out_path = out_path;
  result.sha256 = meta.sha256;
  result.size = static_cast<std::uint64_t>(bytes.size());
  return result;
}

}  // namespace tds
