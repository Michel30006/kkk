#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "tds/config.hpp"
#include "tds/storage.hpp"

namespace tds {

struct ServerAStats {
  std::atomic<std::uint64_t> jobs_encoded{0};
  std::atomic<std::uint64_t> jobs_failed{0};
  std::atomic<std::uint64_t> bytes_encoded{0};
  std::atomic<std::int64_t> started_at{0};
};

class EncodingPipeline {
 public:
  EncodingPipeline(JobStore& store, const Config& config, ServerAStats& stats);

  // Bucle de trabajo: reclama trabajos en cola y los procesa hasta que se pida
  // parar. Seguro para ejecutar varias instancias con workers distintos.
  void run(const std::atomic<bool>& stop, int worker_index);

  // Procesa un trabajo ya reclamado (expuesto para pruebas).
  bool process_job(const JobRecord& job);

 private:
  bool encode_file(const JobRecord& job, const std::string& path,
                   std::uint64_t expected_size,
                   const std::string& expected_sha, std::string& err);
  void notify_server_b(const std::string& job_id);

  JobStore& store_;
  Config config_;
  ServerAStats& stats_;
};

}  // namespace tds
