#include "tds/storage.hpp"

#include <pqxx/pqxx>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>

#include "tds/log.hpp"

namespace tds {
namespace {

constexpr const char* kJobColumns =
    "job_id, status, coalesce(source_url,''), recipient, coalesce(sha256,''), "
    "transfer_id, coalesce(error_code,''), coalesce(error_detail,''), "
    "client_token_hash, coalesce(claimed_by,''), coalesce(size_bytes,0), "
    "encoding_version, total_chunks, chunk_limit_bytes, part_bytes, data_parts, "
    "attempts, "
    "extract(epoch from created_at)::bigint, "
    "extract(epoch from updated_at)::bigint, "
    "coalesce(extract(epoch from completed_at)::bigint,0), "
    "coalesce(extract(epoch from cleaned_at)::bigint,0), "
    "coalesce(extract(epoch from claim_expires_at)::bigint,0)";

// libpqxx 7.x devuelve `pqxx::row` y 8.x `pqxx::row_ref`.
#if defined(PQXX_VERSION_MAJOR) && PQXX_VERSION_MAJOR >= 8
using JobRow = pqxx::row_ref;
#else
using JobRow = pqxx::row;
#endif

JobRecord row_to_job(const JobRow& row) {
  JobRecord job;
  job.job_id = row[0].as<std::string>();
  job.status = row[1].as<std::string>();
  // Las columnas con alias colisionan; se leen por posición.
  job.source_url = row[2].as<std::string>();
  job.recipient = row[3].as<std::string>();
  job.sha256 = row[4].as<std::string>();
  job.transfer_id = row[5].as<std::string>();
  job.error_code = row[6].as<std::string>();
  job.error_detail = row[7].as<std::string>();
  job.client_token_hash = row[8].as<std::string>();
  job.claimed_by = row[9].as<std::string>();
  job.size_bytes = row[10].as<std::int64_t>();
  job.encoding_version = row[11].as<int>();
  job.total_chunks = static_cast<std::uint32_t>(row[12].as<int>());
  job.chunk_limit_bytes = row[13].as<int>();
  job.part_bytes = row[14].as<int>();
  job.data_parts = row[15].as<int>();
  job.attempts = row[16].as<int>();
  job.created_at = row[17].as<std::int64_t>();
  job.updated_at = row[18].as<std::int64_t>();
  job.completed_at = row[19].as<std::int64_t>();
  job.cleaned_at = row[20].as<std::int64_t>();
  job.claim_expires_at = row[21].as<std::int64_t>();
  return job;
}

std::string safe_error(const std::exception& e) {
  std::string text = e.what();
  if (text.size() > 500) text.resize(500);
  return text;
}

std::string join_statuses(const std::vector<std::string>& statuses) {
  std::string out = "{";
  for (std::size_t i = 0; i < statuses.size(); ++i) {
    if (i) out.push_back(',');
    out.push_back('"');
    out += statuses[i];
    out.push_back('"');
  }
  out.push_back('}');
  return out;
}

}  // namespace

class PgStore::Lease {
 public:
  explicit Lease(PgStore& store) : store_(store), conn_(store.borrow()) {}
  ~Lease() { store_.give_back(std::move(conn_)); }
  Lease(const Lease&) = delete;
  Lease& operator=(const Lease&) = delete;
  pqxx::connection& get() { return *conn_; }

 private:
  PgStore& store_;
  std::unique_ptr<pqxx::connection> conn_;
};

PgStore::PgStore(std::string dsn, int pool_size)
    : dsn_(std::move(dsn)), pool_size_(std::max(1, pool_size)) {}

PgStore::~PgStore() {
  std::lock_guard<std::mutex> lock(mutex_);
  closed_ = true;
  idle_.clear();
  cv_.notify_all();
}

std::unique_ptr<pqxx::connection> PgStore::borrow() {
  {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [&] {
      return !idle_.empty() || created_ < static_cast<std::size_t>(pool_size_) ||
             closed_;
    });
    if (closed_) throw std::runtime_error("pool de base de datos cerrado");
    if (!idle_.empty()) {
      auto conn = std::move(idle_.back());
      idle_.pop_back();
      return conn;
    }
    ++created_;
  }
  try {
    return std::make_unique<pqxx::connection>(dsn_);
  } catch (...) {
    std::lock_guard<std::mutex> lock(mutex_);
    --created_;
    cv_.notify_one();
    throw;
  }
}

void PgStore::give_back(std::unique_ptr<pqxx::connection> conn) {
  if (!conn) return;
  std::lock_guard<std::mutex> lock(mutex_);
  if (closed_ || !conn->is_open()) {
    if (created_ > 0) --created_;
    cv_.notify_one();
    return;
  }
  idle_.push_back(std::move(conn));
  cv_.notify_one();
}

bool PgStore::migrate(const std::string& migrations_dir, std::string& err) {
  try {
    namespace fs = std::filesystem;
    std::vector<fs::path> files;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(migrations_dir, ec)) {
      if (entry.path().extension() == ".sql") files.push_back(entry.path());
    }
    if (ec || files.empty()) {
      err = "no se encontraron migraciones en " + migrations_dir;
      return false;
    }
    std::sort(files.begin(), files.end());
    Lease lease(*this);
    pqxx::work tx(lease.get());
    tx.exec(
        "CREATE TABLE IF NOT EXISTS schema_migrations ("
        "version integer PRIMARY KEY, applied_at timestamptz NOT NULL DEFAULT now())");
    for (const auto& path : files) {
      int version = 0;
      try {
        version = std::stoi(path.stem().string().substr(0, 3));
      } catch (...) {
        continue;
      }
      const auto applied =
          tx.exec_params("SELECT 1 FROM schema_migrations WHERE version=$1", version);
      if (!applied.empty()) continue;
      std::ifstream in(path, std::ios::binary);
      if (!in) {
        err = "no se puede leer " + path.string();
        return false;
      }
      const std::string sql((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
      tx.exec(sql);
      tx.exec_params("INSERT INTO schema_migrations(version) VALUES($1)", version);
    }
    tx.commit();
    return true;
  } catch (const std::exception& e) {
    err = "migración falló: " + safe_error(e);
    return false;
  }
}

bool PgStore::create_job(const CreateJobInput& input, std::string& err) {
  try {
    Lease lease(*this);
    pqxx::work tx(lease.get());
    tx.exec_params(
        "INSERT INTO jobs(job_id,status,source_url,recipient,client_token_hash,"
        "transfer_id,encoding_version,part_bytes) "
        "VALUES($1,'queued',$2,$3,$4,$5,$6,$7)",
        input.job_id, input.source_url, input.recipient, input.client_token_hash,
        input.transfer_id, input.encoding_version, input.part_bytes);
    tx.exec_params(
        "INSERT INTO job_events(job_id,stage,detail) VALUES($1,'queued',$2)",
        input.job_id, "trabajo creado");
    tx.commit();
    return true;
  } catch (const std::exception& e) {
    err = safe_error(e);
    return false;
  }
}

bool PgStore::get_job(std::string_view job_id, JobRecord& out, std::string& err) {
  try {
    Lease lease(*this);
    pqxx::work tx(lease.get());
    const auto result = tx.exec_params(
        std::string("SELECT ") + kJobColumns + " FROM jobs WHERE job_id=$1",
        std::string(job_id));
    if (result.empty()) {
      err = "trabajo no encontrado";
      return false;
    }
    out = row_to_job(result[0]);
    tx.commit();
    return true;
  } catch (const std::exception& e) {
    err = safe_error(e);
    return false;
  }
}

bool PgStore::transition(std::string_view job_id, std::string_view from,
                         std::string_view to, std::string_view error_code,
                         std::string_view error_detail, std::string& err) {
  try {
    Lease lease(*this);
    pqxx::work tx(lease.get());
    const auto result = tx.exec_params(
        "UPDATE jobs SET status=$3, error_code=nullif($4,''), "
        "error_detail=nullif($5,''), updated_at=now(), "
        "claimed_by=NULL, claim_expires_at=NULL, "
        "completed_at=CASE WHEN $3='completed' THEN now() ELSE completed_at END "
        "WHERE job_id=$1 AND status=$2",
        std::string(job_id), std::string(from), std::string(to),
        std::string(error_code), std::string(error_detail));
    if (result.affected_rows() != 1) {
      err = "transición no aplicable (estado inesperado)";
      return false;
    }
    tx.exec_params("INSERT INTO job_events(job_id,stage,detail) VALUES($1,$2,$3)",
                   std::string(job_id), std::string(to),
                   std::string(error_code));
    tx.commit();
    return true;
  } catch (const std::exception& e) {
    err = safe_error(e);
    return false;
  }
}

bool PgStore::set_status(std::string_view job_id, std::string_view to,
                         std::string_view error_code,
                         std::string_view error_detail, std::string& err) {
  try {
    Lease lease(*this);
    pqxx::work tx(lease.get());
    tx.exec_params(
        "UPDATE jobs SET status=$2, error_code=nullif($3,''), "
        "error_detail=nullif($4,''), updated_at=now(), "
        "claimed_by=NULL, claim_expires_at=NULL, "
        "completed_at=CASE WHEN $2='completed' THEN now() ELSE completed_at END "
        "WHERE job_id=$1",
        std::string(job_id), std::string(to), std::string(error_code),
        std::string(error_detail));
    tx.exec_params("INSERT INTO job_events(job_id,stage,detail) VALUES($1,$2,$3)",
                   std::string(job_id), std::string(to),
                   std::string(error_code));
    tx.commit();
    return true;
  } catch (const std::exception& e) {
    err = safe_error(e);
    return false;
  }
}

bool PgStore::claim_job(std::string_view worker,
                        const std::vector<std::string>& statuses,
                        std::string_view next_status, JobRecord& out,
                        std::string& err) {
  try {
    Lease lease(*this);
    pqxx::work tx(lease.get());
    const auto result = tx.exec_params(
        std::string(
            "UPDATE jobs SET status=$3, claimed_by=$1, "
            "claim_expires_at=now() + make_interval(secs => $4::int), "
            "attempts=attempts+1, updated_at=now() "
            "WHERE job_id = ("
            "  SELECT job_id FROM jobs "
            "  WHERE status = ANY($2::text[]) "
            "    AND (claim_expires_at IS NULL OR claim_expires_at < now()) "
            "  ORDER BY created_at FOR UPDATE SKIP LOCKED LIMIT 1) "
            "RETURNING ") +
            kJobColumns,
        std::string(worker), join_statuses(statuses), std::string(next_status),
        lease_seconds_);
    if (result.empty()) {
      err = "no hay trabajos disponibles";
      return false;
    }
    out = row_to_job(result[0]);
    tx.exec_params("INSERT INTO job_events(job_id,stage,detail) VALUES($1,$2,$3)",
                   out.job_id, std::string(next_status), std::string("claim por ") + std::string(worker));
    tx.commit();
    return true;
  } catch (const std::exception& e) {
    err = safe_error(e);
    return false;
  }
}

bool PgStore::renew_lease(std::string_view job_id, std::string_view worker,
                          std::string& err) {
  try {
    Lease lease(*this);
    pqxx::work tx(lease.get());
    tx.exec_params(
        "UPDATE jobs SET claim_expires_at=now() + make_interval(secs => $3), "
        "updated_at=now() WHERE job_id=$1 AND claimed_by=$2",
        std::string(job_id), std::string(worker), lease_seconds_);
    tx.commit();
    return true;
  } catch (const std::exception& e) {
    err = safe_error(e);
    return false;
  }
}

bool PgStore::append_parts(std::string_view job_id,
                           const std::vector<DataPart>& parts,
                           std::string& err) {
  try {
    Lease lease(*this);
    pqxx::work tx(lease.get());
    for (const auto& part : parts) {
      tx.exec_params(
          "INSERT INTO job_data_parts(job_id,part_index,payload,payload_bytes) "
          "VALUES($1,$2,$3,$4) ON CONFLICT (job_id,part_index) DO NOTHING",
          std::string(job_id), part.index, part.payload, part.payload_bytes);
    }
    tx.commit();
    return true;
  } catch (const std::exception& e) {
    err = safe_error(e);
    return false;
  }
}

bool PgStore::finish_encoding(std::string_view job_id, std::int64_t size,
                              std::string_view sha256, int data_parts,
                              std::string& err) {
  try {
    Lease lease(*this);
    pqxx::work tx(lease.get());
    const auto result = tx.exec_params(
        "UPDATE jobs SET size_bytes=$2, sha256=$3, data_parts=$4, "
        "status='ready_for_delivery', updated_at=now(), error_code=NULL, "
        "error_detail=NULL, claimed_by=NULL, claim_expires_at=NULL "
        "WHERE job_id=$1 AND status='encoding'",
        std::string(job_id), size, std::string(sha256), data_parts);
    if (result.affected_rows() != 1) {
      err = "el trabajo no estaba en estado encoding";
      return false;
    }
    tx.exec_params("INSERT INTO job_events(job_id,stage,detail) VALUES($1,'encoding_done',$2)",
                   std::string(job_id), "tamaño=" + std::to_string(size));
    tx.commit();
    return true;
  } catch (const std::exception& e) {
    err = safe_error(e);
    return false;
  }
}

bool PgStore::load_parts(std::string_view job_id, int from_index, int limit,
                         std::vector<DataPart>& out, std::string& err) {
  try {
    Lease lease(*this);
    pqxx::work tx(lease.get());
    const auto result = tx.exec_params(
        "SELECT part_index,payload,payload_bytes FROM job_data_parts "
        "WHERE job_id=$1 AND part_index >= $2 ORDER BY part_index LIMIT $3",
        std::string(job_id), from_index, limit);
    out.clear();
    out.reserve(result.size());
    for (const auto& row : result) {
      DataPart part;
      part.index = row[0].as<int>();
      part.payload = row[1].as<std::string>();
      part.payload_bytes = row[2].as<std::int64_t>();
      out.push_back(std::move(part));
    }
    tx.commit();
    return true;
  } catch (const std::exception& e) {
    err = safe_error(e);
    return false;
  }
}

bool PgStore::set_chunk_plan(std::string_view job_id,
                             std::uint32_t total_chunks, int chunk_limit,
                             std::string& err) {
  try {
    Lease lease(*this);
    pqxx::work tx(lease.get());
    const auto result = tx.exec_params(
        "UPDATE jobs SET total_chunks=$2, chunk_limit_bytes=$3, updated_at=now() "
        "WHERE job_id=$1 AND (total_chunks=0 OR total_chunks=$2)",
        std::string(job_id), static_cast<int>(total_chunks), chunk_limit);
    if (result.affected_rows() != 1) {
      err = "el plan de tramas no coincide con el ya guardado";
      return false;
    }
    tx.commit();
    return true;
  } catch (const std::exception& e) {
    err = safe_error(e);
    return false;
  }
}

bool PgStore::mark_chunk_sent(std::string_view job_id, std::uint32_t index,
                              std::string_view message_id, std::string& err) {
  try {
    Lease lease(*this);
    pqxx::work tx(lease.get());
    tx.exec_params(
        "INSERT INTO chunk_deliveries(job_id,chunk_index,attempts,sent_at,last_error) "
        "VALUES($1,$2,1,now(),NULL) "
        "ON CONFLICT (job_id,chunk_index) DO UPDATE SET sent_at=now(), "
        "attempts=chunk_deliveries.attempts+1, last_error=NULL",
        std::string(job_id), static_cast<int>(index));
    tx.exec_params("INSERT INTO job_events(job_id,stage,detail) VALUES($1,'chunk_sent',$2)",
                   std::string(job_id), std::string(message_id));
    tx.commit();
    return true;
  } catch (const std::exception& e) {
    err = safe_error(e);
    return false;
  }
}

bool PgStore::mark_chunk_acked(std::string_view job_id, std::uint32_t index,
                               std::string& err) {
  try {
    Lease lease(*this);
    pqxx::work tx(lease.get());
    tx.exec_params(
        "UPDATE chunk_deliveries SET acked_at=now() "
        "WHERE job_id=$1 AND chunk_index=$2",
        std::string(job_id), static_cast<int>(index));
    tx.commit();
    return true;
  } catch (const std::exception& e) {
    err = safe_error(e);
    return false;
  }
}

bool PgStore::record_chunk_error(std::string_view job_id,
                                 std::uint32_t index, std::string_view error,
                                 std::string& err) {
  try {
    Lease lease(*this);
    pqxx::work tx(lease.get());
    std::string detail(error);
    if (detail.size() > 300) detail.resize(300);
    tx.exec_params(
        "INSERT INTO chunk_deliveries(job_id,chunk_index,attempts,last_error) "
        "VALUES($1,$2,1,$3) ON CONFLICT (job_id,chunk_index) DO UPDATE SET "
        "attempts=chunk_deliveries.attempts+1, last_error=$3",
        std::string(job_id), static_cast<int>(index), detail);
    tx.commit();
    return true;
  } catch (const std::exception& e) {
    err = safe_error(e);
    return false;
  }
}

bool PgStore::delivered_count(std::string_view job_id, std::uint32_t& out,
                              std::string& err) {
  try {
    Lease lease(*this);
    pqxx::work tx(lease.get());
    const auto result = tx.exec_params(
        "SELECT count(*) FROM chunk_deliveries WHERE job_id=$1 AND sent_at IS NOT NULL",
        std::string(job_id));
    out = static_cast<std::uint32_t>(result[0][0].as<std::int64_t>());
    tx.commit();
    return true;
  } catch (const std::exception& e) {
    err = safe_error(e);
    return false;
  }
}

bool PgStore::sent_chunks(std::string_view job_id,
                          std::vector<std::uint32_t>& out, std::string& err) {
  try {
    Lease lease(*this);
    pqxx::work tx(lease.get());
    const auto result = tx.exec_params(
        "SELECT chunk_index FROM chunk_deliveries "
        "WHERE job_id=$1 AND sent_at IS NOT NULL ORDER BY chunk_index",
        std::string(job_id));
    out.clear();
    out.reserve(result.size());
    for (const auto& row : result)
      out.push_back(static_cast<std::uint32_t>(row[0].as<int>()));
    tx.commit();
    return true;
  } catch (const std::exception& e) {
    err = safe_error(e);
    return false;
  }
}

bool PgStore::confirm_reconstruction(std::string_view job_id,
                                     std::string_view sha256, std::uint64_t size,
                                     std::string& err) {
  try {
    Lease lease(*this);
    pqxx::work tx(lease.get());
    const auto result = tx.exec_params(
        "UPDATE jobs SET status='completed', completed_at=now(), updated_at=now() "
        "WHERE job_id=$1 AND sha256=$2 AND size_bytes=$3 "
        "AND status IN ('sending','awaiting_client_confirmation') "
        "RETURNING job_id",
        std::string(job_id), std::string(sha256),
        static_cast<std::int64_t>(size));
    if (result.empty()) {
      err = "confirmación rechazada: hash, tamaño o estado no coinciden";
      return false;
    }
    tx.exec_params("INSERT INTO job_events(job_id,stage,detail) VALUES($1,'completed',$2)",
                   std::string(job_id), std::string(sha256));
    tx.commit();
    return true;
  } catch (const std::exception& e) {
    err = safe_error(e);
    return false;
  }
}

bool PgStore::cleanup_job_data(std::string_view job_id, std::string& err) {
  try {
    Lease lease(*this);
    pqxx::work tx(lease.get());
    tx.exec_params("DELETE FROM job_data_parts WHERE job_id=$1", std::string(job_id));
    tx.exec_params("DELETE FROM chunk_deliveries WHERE job_id=$1", std::string(job_id));
    tx.exec_params(
        "UPDATE jobs SET status=CASE WHEN status='completed' THEN 'cleaned' ELSE status END, "
        "cleaned_at=now(), updated_at=now() WHERE job_id=$1",
        std::string(job_id));
    tx.exec_params("INSERT INTO job_events(job_id,stage,detail) VALUES($1,'cleaned',$2)",
                   std::string(job_id), "datos temporales eliminados");
    tx.commit();
    return true;
  } catch (const std::exception& e) {
    err = safe_error(e);
    return false;
  }
}

bool PgStore::expire_stale(int stale_minutes, int client_confirm_hours,
                           int retention_hours, int& expired_count,
                           std::string& err) {
  try {
    Lease lease(*this);
    pqxx::work tx(lease.get());
    const auto result = tx.exec_params(
        "UPDATE jobs SET status='expired', error_code='expired', updated_at=now() "
        "WHERE ("
        "  (status IN ('created','queued','downloading','encoding','sending') "
        "   AND updated_at < now() - make_interval(mins => $1)) "
        "  OR (status IN ('ready_for_delivery','awaiting_client_confirmation') "
        "   AND updated_at < now() - make_interval(hours => $2))"
        ") RETURNING job_id",
        stale_minutes, client_confirm_hours);
    expired_count = static_cast<int>(result.size());
    for (const auto& row : result) {
      const auto id = row[0].as<std::string>();
      tx.exec_params("DELETE FROM job_data_parts WHERE job_id=$1", id);
      tx.exec_params("DELETE FROM chunk_deliveries WHERE job_id=$1", id);
      tx.exec_params("INSERT INTO job_events(job_id,stage,detail) VALUES($1,'expired',$2)",
                     id, "trabajo abandonado");
    }
    // Limpieza de datos de trabajos ya terminales que aún conserven payloads.
    tx.exec_params(
        "DELETE FROM job_data_parts WHERE job_id IN ("
        "SELECT job_id FROM jobs WHERE status IN ('completed','expired','cancelled',"
        "'failed_download','failed_encoding','failed_delivery','failed_reconstruction') "
        "AND updated_at < now() - make_interval(hours => $1))",
        retention_hours);
    tx.commit();
    return true;
  } catch (const std::exception& e) {
    err = safe_error(e);
    return false;
  }
}

bool PgStore::purge_cleaned(int retention_hours, int& purged, std::string& err) {
  try {
    Lease lease(*this);
    pqxx::work tx(lease.get());
    const auto result = tx.exec_params(
        "DELETE FROM jobs WHERE status IN ('cleaned','expired','cancelled') "
        "AND updated_at < now() - make_interval(hours => $1) RETURNING job_id",
        retention_hours);
    purged = static_cast<int>(result.size());
    tx.commit();
    return true;
  } catch (const std::exception& e) {
    err = safe_error(e);
    return false;
  }
}

bool PgStore::add_event(std::string_view job_id, std::string_view stage,
                        std::string_view detail, std::string& err) {
  try {
    Lease lease(*this);
    pqxx::work tx(lease.get());
    tx.exec_params("INSERT INTO job_events(job_id,stage,detail) VALUES($1,$2,$3)",
                   std::string(job_id), std::string(stage), std::string(detail));
    tx.commit();
    return true;
  } catch (const std::exception& e) {
    err = safe_error(e);
    return false;
  }
}

bool PgStore::list_events(std::string_view job_id,
                          std::vector<std::string>& out, std::string& err) {
  try {
    Lease lease(*this);
    pqxx::work tx(lease.get());
    const auto result = tx.exec_params(
        "SELECT to_char(at,'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"'),stage,coalesce(detail,'') "
        "FROM job_events WHERE job_id=$1 ORDER BY id",
        std::string(job_id));
    out.clear();
    for (const auto& row : result) {
      out.push_back(row[0].as<std::string>() + " " + row[1].as<std::string>() +
                    " " + row[2].as<std::string>());
    }
    tx.commit();
    return true;
  } catch (const std::exception& e) {
    err = safe_error(e);
    return false;
  }
}

bool PgStore::ping(std::string& err) {
  try {
    Lease lease(*this);
    pqxx::work tx(lease.get());
    tx.exec("SELECT 1");
    tx.commit();
    return true;
  } catch (const std::exception& e) {
    err = safe_error(e);
    return false;
  }
}

std::unique_ptr<JobStore> make_store(const std::string& database_url,
                                     const std::string& migrations_dir,
                                     std::string& err) {
  auto store = std::make_unique<PgStore>(database_url);
  if (!store->migrate(migrations_dir, err)) return nullptr;
  return store;
}

}  // namespace tds
