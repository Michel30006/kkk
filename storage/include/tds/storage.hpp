#pragma once

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace pqxx {
class connection;
}

namespace tds {

struct JobRecord {
  std::string job_id;
  std::string status;
  std::string source_url;
  std::string recipient;
  std::string sha256;
  std::string transfer_id;
  std::string error_code;
  std::string error_detail;
  std::string client_token_hash;
  std::string claimed_by;
  std::int64_t size_bytes{0};
  int encoding_version{1};
  std::uint32_t total_chunks{0};
  int chunk_limit_bytes{0};
  int part_bytes{65536};
  int data_parts{0};
  int attempts{0};
  std::int64_t created_at{0};
  std::int64_t updated_at{0};
  std::int64_t completed_at{0};
  std::int64_t cleaned_at{0};
  std::int64_t claim_expires_at{0};
};

struct DataPart {
  int index{0};
  std::string payload;
  std::int64_t payload_bytes{0};
};

struct CreateJobInput {
  std::string job_id;
  std::string source_url;
  std::string recipient;
  std::string client_token_hash;
  std::string transfer_id;
  int encoding_version{1};
  int part_bytes{65536};
};

class JobStore {
 public:
  virtual ~JobStore() = default;

  virtual bool migrate(const std::string& migrations_dir, std::string& err) = 0;
  virtual bool create_job(const CreateJobInput& input, std::string& err) = 0;
  virtual bool get_job(std::string_view job_id, JobRecord& out,
                       std::string& err) = 0;

  // Transición guardada: sólo aplica si `from` coincide.
  virtual bool transition(std::string_view job_id, std::string_view from,
                          std::string_view to, std::string_view error_code,
                          std::string_view error_detail, std::string& err) = 0;
  virtual bool set_status(std::string_view job_id, std::string_view to,
                          std::string_view error_code,
                          std::string_view error_detail, std::string& err) = 0;

  // Reserva atómica (SKIP LOCKED). `statuses` son los estados aceptables.
  virtual bool claim_job(std::string_view worker,
                         const std::vector<std::string>& statuses,
                         std::string_view next_status, JobRecord& out,
                         std::string& err) = 0;
  virtual bool renew_lease(std::string_view job_id, std::string_view worker,
                           std::string& err) = 0;

  virtual bool append_parts(std::string_view job_id,
                            const std::vector<DataPart>& parts,
                            std::string& err) = 0;
  virtual bool finish_encoding(std::string_view job_id, std::int64_t size,
                               std::string_view sha256, int data_parts,
                               std::string& err) = 0;
  virtual bool load_parts(std::string_view job_id, int from_index, int limit,
                          std::vector<DataPart>& out, std::string& err) = 0;
  virtual bool set_chunk_plan(std::string_view job_id,
                              std::uint32_t total_chunks, int chunk_limit,
                              std::string& err) = 0;

  virtual bool mark_chunk_sent(std::string_view job_id, std::uint32_t index,
                               std::string_view message_id, std::string& err) = 0;
  virtual bool mark_chunk_acked(std::string_view job_id, std::uint32_t index,
                                std::string& err) = 0;
  virtual bool record_chunk_error(std::string_view job_id,
                                  std::uint32_t index, std::string_view error,
                                  std::string& err) = 0;
  virtual bool delivered_count(std::string_view job_id, std::uint32_t& out,
                               std::string& err) = 0;
  // Índices con envío registrado (para reanudar sin repetir tramas).
  virtual bool sent_chunks(std::string_view job_id,
                           std::vector<std::uint32_t>& out,
                           std::string& err) = 0;

  // Confirmación del cliente: exige SHA-256 correcto y tamaño correcto.
  virtual bool confirm_reconstruction(std::string_view job_id,
                                      std::string_view sha256,
                                      std::uint64_t size, std::string& err) = 0;

  // Borra payloads y deliveries de un trabajo terminado; conserva metadatos.
  virtual bool cleanup_job_data(std::string_view job_id, std::string& err) = 0;

  // Marca expirados los trabajos abandonados y libera sus datos.
  // `client_confirm_hours` se aplica a ready_for_delivery y
  // awaiting_client_confirmation; el resto usa `stale_minutes`.
  virtual bool expire_stale(int stale_minutes, int client_confirm_hours,
                            int retention_hours, int& expired_count,
                            std::string& err) = 0;
  virtual bool purge_cleaned(int retention_hours, int& purged,
                             std::string& err) = 0;

  virtual bool add_event(std::string_view job_id, std::string_view stage,
                         std::string_view detail, std::string& err) = 0;
  virtual bool list_events(std::string_view job_id,
                           std::vector<std::string>& out, std::string& err) = 0;

  virtual bool ping(std::string& err) = 0;

  // Duración del lease de reclamación (segundos).
  virtual void set_lease_seconds(int) {}
};

class PgStore : public JobStore {
 public:
  PgStore(std::string dsn, int pool_size = 4);
  ~PgStore() override;

  bool migrate(const std::string& migrations_dir, std::string& err) override;
  bool create_job(const CreateJobInput& input, std::string& err) override;
  bool get_job(std::string_view job_id, JobRecord& out,
               std::string& err) override;
  bool transition(std::string_view job_id, std::string_view from,
                  std::string_view to, std::string_view error_code,
                  std::string_view error_detail, std::string& err) override;
  bool set_status(std::string_view job_id, std::string_view to,
                  std::string_view error_code, std::string_view error_detail,
                  std::string& err) override;
  bool claim_job(std::string_view worker,
                 const std::vector<std::string>& statuses,
                 std::string_view next_status, JobRecord& out,
                 std::string& err) override;
  bool renew_lease(std::string_view job_id, std::string_view worker,
                   std::string& err) override;
  bool append_parts(std::string_view job_id,
                    const std::vector<DataPart>& parts,
                    std::string& err) override;
  bool finish_encoding(std::string_view job_id, std::int64_t size,
                       std::string_view sha256, int data_parts,
                       std::string& err) override;
  bool load_parts(std::string_view job_id, int from_index, int limit,
                  std::vector<DataPart>& out, std::string& err) override;
  bool set_chunk_plan(std::string_view job_id, std::uint32_t total_chunks,
                      int chunk_limit, std::string& err) override;
  bool mark_chunk_sent(std::string_view job_id, std::uint32_t index,
                       std::string_view message_id, std::string& err) override;
  bool mark_chunk_acked(std::string_view job_id, std::uint32_t index,
                        std::string& err) override;
  bool record_chunk_error(std::string_view job_id, std::uint32_t index,
                          std::string_view error, std::string& err) override;
  bool delivered_count(std::string_view job_id, std::uint32_t& out,
                       std::string& err) override;
  bool sent_chunks(std::string_view job_id, std::vector<std::uint32_t>& out,
                   std::string& err) override;
  bool confirm_reconstruction(std::string_view job_id, std::string_view sha256,
                              std::uint64_t size, std::string& err) override;
  bool cleanup_job_data(std::string_view job_id, std::string& err) override;
  bool expire_stale(int stale_minutes, int client_confirm_hours,
                    int retention_hours, int& expired_count,
                    std::string& err) override;
  bool purge_cleaned(int retention_hours, int& purged, std::string& err) override;
  bool add_event(std::string_view job_id, std::string_view stage,
                 std::string_view detail, std::string& err) override;
  bool list_events(std::string_view job_id, std::vector<std::string>& out,
                   std::string& err) override;
  bool ping(std::string& err) override;

  void set_lease_seconds(int seconds) override { lease_seconds_ = seconds; }

 private:
  class Lease;
  std::unique_ptr<pqxx::connection> borrow();
  void give_back(std::unique_ptr<pqxx::connection> conn);

  std::string dsn_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::vector<std::unique_ptr<pqxx::connection>> idle_;
  std::size_t created_{0};
  int pool_size_{4};
  int lease_seconds_{180};
  bool closed_{false};
};

// Crea el esquema si hace falta (idempotente) y devuelve el store listo.
std::unique_ptr<JobStore> make_store(const std::string& database_url,
                                     const std::string& migrations_dir,
                                     std::string& err);

}  // namespace tds
