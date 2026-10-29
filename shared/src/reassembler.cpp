#include "tds/reassembler.hpp"

#include <algorithm>

#include "tds/encoding.hpp"
#include "tds/framing.hpp"
#include "tds/sha256.hpp"

namespace tds {

Reassembler::Reassembler(JobMeta meta) : meta_(std::move(meta)) {
  if (meta_.total_chunks > 0) {
    payloads_.resize(meta_.total_chunks);
    present_.assign(meta_.total_chunks, false);
  }
}

AddOutcome Reassembler::add(const std::string& wire, std::string& err) {
  Frame frame;
  if (!parse_frame(wire, frame, err)) {
    ++stats_.rejected;
    return AddOutcome::kRejected;
  }
  if (!meta_.job_id.empty() && frame.job_id != meta_.job_id) {
    err = "la trama pertenece a otro job";
    ++stats_.rejected;
    return AddOutcome::kRejected;
  }
  if (!meta_.transfer_id.empty() && frame.transfer_id != meta_.transfer_id) {
    err = "la trama pertenece a otra transferencia";
    ++stats_.rejected;
    return AddOutcome::kRejected;
  }
  if (frame.encoding_version != meta_.encoding_version) {
    err = "versión de codificación distinta a la del job";
    ++stats_.rejected;
    return AddOutcome::kRejected;
  }
  if (meta_.total_chunks == 0) {
    meta_.total_chunks = frame.total_chunks;
    meta_.job_id = frame.job_id;
    meta_.transfer_id = frame.transfer_id;
    payloads_.resize(meta_.total_chunks);
    present_.assign(meta_.total_chunks, false);
  } else if (frame.total_chunks != meta_.total_chunks) {
    err = "total_chunks incoherente con las tramas previas";
    ++stats_.rejected;
    return AddOutcome::kRejected;
  }
  stats_.expected = meta_.total_chunks;
  if (frame.chunk_index >= meta_.total_chunks) {
    err = "índice fuera de rango";
    ++stats_.rejected;
    return AddOutcome::kRejected;
  }
  if (present_[frame.chunk_index]) {
    ++stats_.duplicates;
    return AddOutcome::kDuplicate;
  }
  const bool was_out_of_order =
      frame.chunk_index != stats_.received;  // ya se contaron los duplicados
  if (was_out_of_order) ++stats_.out_of_order;
  present_[frame.chunk_index] = true;
  payloads_[frame.chunk_index] = std::move(frame.payload);
  ++stats_.received;
  return AddOutcome::kAccepted;
}

bool Reassembler::complete() const {
  if (meta_.total_chunks == 0) return false;
  return stats_.received == meta_.total_chunks &&
         std::all_of(present_.begin(), present_.end(), [](bool v) { return v; });
}

std::vector<std::uint32_t> Reassembler::missing() const {
  std::vector<std::uint32_t> out;
  for (std::uint32_t i = 0; i < present_.size(); ++i)
    if (!present_[i]) out.push_back(i);
  return out;
}

bool Reassembler::reconstruct(std::string& bytes_out, std::string& err) const {
  if (!complete()) {
    const auto gaps = missing();
    err = "faltan " + std::to_string(gaps.size()) + " tramas";
    if (!gaps.empty()) err += " (primera: " + std::to_string(gaps.front()) + ")";
    return false;
  }
  bytes_out.clear();
  for (const auto& payload : payloads_) {
    std::string part;
    if (!decode_text(payload, part, err)) return false;
    bytes_out += part;
  }
  if (bytes_out.size() != meta_.size_bytes) {
    err = "tamaño reconstruido " + std::to_string(bytes_out.size()) +
          " != esperado " + std::to_string(meta_.size_bytes);
    return false;
  }
  const auto digest = sha256_hex(bytes_out);
  if (digest != meta_.sha256) {
    err = "SHA-256 reconstruido no coincide";
    return false;
  }
  return true;
}

}  // namespace tds
