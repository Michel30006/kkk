#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace tds {

// Decodificador de estado mínimo para clientes.
struct JobMeta {
  std::string job_id;
  std::string transfer_id;
  std::string status;
  std::uint64_t size_bytes{0};
  std::string sha256;
  int encoding_version{1};
  std::uint32_t total_chunks{0};
  std::string error_code;
};

struct ReassemblyStats {
  std::uint32_t received{0};
  std::uint32_t duplicates{0};
  std::uint32_t out_of_order{0};
  std::uint32_t rejected{0};
  std::uint32_t expected{0};
};

enum class AddOutcome { kAccepted, kDuplicate, kRejected };

class Reassembler {
 public:
  explicit Reassembler(JobMeta meta);

  AddOutcome add(const std::string& wire, std::string& err);

  bool complete() const;
  std::vector<std::uint32_t> missing() const;

  // Decodifica, ordena y verifica tamaño y SHA-256. No es válido hasta
  // complete().
  bool reconstruct(std::string& bytes_out, std::string& err) const;

  const ReassemblyStats& stats() const { return stats_; }
  const JobMeta& meta() const { return meta_; }

 private:
  JobMeta meta_;
  ReassemblyStats stats_;
  std::vector<std::string> payloads_;  // por índice
  std::vector<bool> present_;
};

}  // namespace tds
