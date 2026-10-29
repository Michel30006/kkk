#include "worker.hpp"

#include <chrono>
#include <set>
#include <thread>
#include <unordered_set>
#include <vector>

#include "tds/framing.hpp"
#include "tds/log.hpp"
#include "tds/util.hpp"

namespace tds {
namespace {

std::int64_t steady_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

}  // namespace

DeliveryWorker::DeliveryWorker(JobStore& store, const Config& config,
                               Transport& transport, ServerBStats& stats)
    : store_(store), config_(config), transport_(transport), stats_(stats) {}

void DeliveryWorker::wake() {
  {
    std::lock_guard<std::mutex> lock(wake_mutex_);
    wake_flag_ = true;
  }
  wake_cv_.notify_all();
}

void DeliveryWorker::run(const std::atomic<bool>& stop, int worker_index) {
  const std::string worker =
      "b-" + std::to_string(worker_index) + "-" + random_hex(4);
  spdlog::info("worker B {} iniciado", worker);
  while (!stop.load()) {
    JobRecord job;
    std::string err;
    // "sending" se incluye para reanudar entregas interrumpidas; las tramas ya
    // enviadas se omiten gracias a chunk_deliveries.
    if (store_.claim_job(worker, {"ready_for_delivery", "sending"}, "sending",
                         job, err)) {
      spdlog::info("job {} reclamado para entrega (intento {})", job.job_id,
                   job.attempts);
      deliver(job);
      continue;
    }
    std::unique_lock<std::mutex> lock(wake_mutex_);
    wake_cv_.wait_for(lock, std::chrono::milliseconds(config_.worker_poll_ms),
                      [&] { return wake_flag_ || stop.load(); });
    wake_flag_ = false;
  }
  spdlog::info("worker B {} detenido", worker);
}

bool DeliveryWorker::deliver(const JobRecord& job) {
  const auto jid = todus_jid(job.recipient, config_.todus_domain);
  if (jid.empty()) {
    spdlog::error("job {}: destinatario inválido '{}'", job.job_id,
                  job.recipient);
    std::string err;
    store_.set_status(job.job_id, "failed_delivery", "invalid_recipient",
                      "destinatario no válido", err);
    stats_.jobs_failed.fetch_add(1);
    return false;
  }

  std::string err;
  if (!transport_.connect(err)) {
    spdlog::error("job {}: no se pudo conectar al transporte: {}", job.job_id,
                  err);
    store_.set_status(job.job_id, "failed_delivery", "transport_unavailable",
                      err, err);
    stats_.jobs_failed.fetch_add(1);
    return false;
  }

  if (!send_all(job, jid)) {
    stats_.jobs_failed.fetch_add(1);
    return false;
  }

  if (!store_.transition(job.job_id, "sending",
                         "awaiting_client_confirmation", "", "", err)) {
    // Si otro worker ya lo dejó en awaiting, no es un fallo real.
    JobRecord current;
    if (store_.get_job(job.job_id, current, err) &&
        current.status == "awaiting_client_confirmation") {
      spdlog::warn("job {} ya estaba pendiente de confirmación", job.job_id);
      return true;
    }
    spdlog::error("job {}: no se pudo marcar awaiting: {}", job.job_id, err);
    store_.set_status(job.job_id, "failed_delivery", "state_error", err, err);
    return false;
  }

  stats_.jobs_delivered.fetch_add(1);
  spdlog::info("job {} enviado por completo a {}; esperando confirmación del "
               "cliente",
               job.job_id, jid);
  return true;
}

void DeliveryWorker::renew_if_needed(const JobRecord& job,
                                     std::int64_t& last_renew_ms) {
  const auto now = steady_ms();
  const auto interval = std::max(1, config_.job_lease_seconds / 3) * 1000LL;
  if (now - last_renew_ms < interval) return;
  std::string err;
  if (!store_.renew_lease(job.job_id, job.claimed_by, err))
    spdlog::warn("job {}: no se pudo renovar el lease: {}", job.job_id, err);
  last_renew_ms = now;
}

bool DeliveryWorker::send_all(const JobRecord& job, const std::string& jid) {
  // Carga todas las partes del trabajo. El límite de vídeo (25 MiB) mantiene
  // este buffer pequeño incluso con la expansión de la tabla (≈1,66 B/B).
  std::vector<std::string> owned_parts;
  std::string err;
  int from_index = 0;
  while (true) {
    std::vector<DataPart> batch;
    if (!store_.load_parts(job.job_id, from_index, 256, batch, err)) {
      spdlog::error("job {}: no se pudieron leer las partes: {}", job.job_id,
                    err);
      store_.set_status(job.job_id, "failed_delivery", "load_error", err, err);
      return false;
    }
    if (batch.empty()) break;
    for (auto& part : batch) {
      from_index = part.index + 1;
      owned_parts.push_back(std::move(part.payload));
    }
  }
  if (owned_parts.empty()) {
    spdlog::error("job {}: no hay datos codificados", job.job_id);
    store_.set_status(job.job_id, "failed_delivery", "no_data", "", err);
    return false;
  }

  std::vector<std::string_view> parts;
  parts.reserve(owned_parts.size());
  for (const auto& part : owned_parts) parts.push_back(part);

  std::uint32_t total = 0;
  if (!count_frames(parts, job.job_id, job.transfer_id,
                    config_.max_stanza_bytes, total, err)) {
    spdlog::error("job {}: recuento de tramas falló: {}", job.job_id, err);
    store_.set_status(job.job_id, "failed_delivery", "framing_error", err, err);
    return false;
  }
  if (total == 0) {
    spdlog::error("job {}: el plan de tramas quedó vacío", job.job_id);
    store_.set_status(job.job_id, "failed_delivery", "framing_error",
                      "plan vacío", err);
    return false;
  }
  if (!store_.set_chunk_plan(job.job_id, total,
                             static_cast<int>(config_.max_stanza_bytes), err)) {
    spdlog::error("job {}: no se pudo guardar el plan: {}", job.job_id, err);
    store_.set_status(job.job_id, "failed_delivery", "db_error", err, err);
    return false;
  }

  std::vector<std::uint32_t> sent_list;
  if (!store_.sent_chunks(job.job_id, sent_list, err)) {
    spdlog::error("job {}: no se pudo leer el progreso: {}", job.job_id, err);
    store_.set_status(job.job_id, "failed_delivery", "db_error", err, err);
    return false;
  }
  const std::set<std::uint32_t> already_sent(sent_list.begin(),
                                             sent_list.end());
  if (!already_sent.empty())
    spdlog::info("job {}: reanudando, {} de {} tramas ya enviadas", job.job_id,
                 already_sent.size(), total);

  std::int64_t last_renew = steady_ms();
  std::uint32_t emitted = 0;
  std::uint64_t sent_now = 0;
  bool failed = false;
  std::string failure_detail;

  const bool iterated = iterate_frames(
      parts, job.job_id, job.transfer_id, total, config_.max_stanza_bytes, 0,
      [&](Frame&& frame) {
        const auto index = frame.chunk_index;
        if (already_sent.count(index)) {
          // Sólo se renueva el lease de vez en cuando al saltar.
          renew_if_needed(job, last_renew);
          return true;
        }
        const std::string wire = serialize_frame(frame);
        if (wire.size() > config_.max_stanza_bytes) {
          failed = true;
          failure_detail = "trama mayor que MAX_STANZA_BYTES";
          return false;
        }

        std::string message_id;
        std::string send_err;
        bool ok = false;
        for (int attempt = 1; attempt <= std::max(1, config_.max_send_retries);
             ++attempt) {
          renew_if_needed(job, last_renew);
          if (transport_.send_text(jid, wire, message_id, send_err) &&
              transport_.wait_ack(message_id, config_.ack_timeout_ms,
                                  send_err)) {
            ok = true;
            break;
          }
          spdlog::warn("job {}: trama {}/{} falló (intento {}): {}",
                       job.job_id, index + 1, total, attempt, send_err);
          std::this_thread::sleep_for(std::chrono::milliseconds(
              200 * attempt));
        }
        if (!ok) {
          failed = true;
          failure_detail = "trama " + std::to_string(index) + ": " + send_err;
          return false;
        }
        std::string mark_err;
        if (!store_.mark_chunk_sent(job.job_id, index, message_id, mark_err)) {
          spdlog::warn("job {}: no se pudo registrar la trama {}: {}",
                       job.job_id, index, mark_err);
        }
        ++sent_now;
        stats_.frames_sent.fetch_add(1);
        stats_.bytes_sent.fetch_add(wire.size());
        if (sent_now % 100 == 0)
          spdlog::info("job {}: {}/{} tramas", job.job_id, index + 1, total);
        if (config_.send_delay_ms > 0)
          std::this_thread::sleep_for(
              std::chrono::milliseconds(config_.send_delay_ms));
        return true;
      },
      emitted, err);

  if (!iterated && failure_detail.empty()) failure_detail = err;
  if (failed || !iterated) {
    std::string ignored;
    store_.record_chunk_error(job.job_id, emitted, failure_detail, ignored);
    store_.set_status(job.job_id, "failed_delivery", "send_failed",
                      failure_detail, ignored);
    spdlog::error("job {}: entrega fallida: {}", job.job_id, failure_detail);
    return false;
  }
  if (emitted != total) {
    std::string ignored;
    store_.set_status(job.job_id, "failed_delivery", "framing_error",
                      "número de tramas distinto del plan", ignored);
    return false;
  }
  return true;
}

}  // namespace tds
