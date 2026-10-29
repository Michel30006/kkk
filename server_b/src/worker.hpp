#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>

#include "tds/config.hpp"
#include "tds/storage.hpp"
#include "tds/transport.hpp"

namespace tds {

struct ServerBStats {
  std::atomic<std::uint64_t> jobs_delivered{0};
  std::atomic<std::uint64_t> jobs_failed{0};
  std::atomic<std::uint64_t> frames_sent{0};
  std::atomic<std::uint64_t> bytes_sent{0};
  std::atomic<std::int64_t> started_at{0};
};

class DeliveryWorker {
 public:
  DeliveryWorker(JobStore& store, const Config& config, Transport& transport,
                 ServerBStats& stats);

  // Bucle principal: reclama trabajos listos y los entrega. `wake()` acorta la
  // espera entre sondeos.
  void run(const std::atomic<bool>& stop, int worker_index);
  void wake();

  // Entrega un trabajo ya reclamado (expuesto para pruebas).
  bool deliver(const JobRecord& job);

 private:
  bool send_all(const JobRecord& job, const std::string& jid);
  void renew_if_needed(const JobRecord& job, std::int64_t& last_renew_ms);

  JobStore& store_;
  Config config_;
  Transport& transport_;
  ServerBStats& stats_;

  std::mutex wake_mutex_;
  std::condition_variable wake_cv_;
  bool wake_flag_{false};
};

}  // namespace tds
