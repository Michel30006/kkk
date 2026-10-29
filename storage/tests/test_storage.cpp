#include <gtest/gtest.h>

#include <pqxx/pqxx>

#include <cstdlib>
#include <string>

#include "tds/storage.hpp"
#include "tds/sha256.hpp"
#include "tds/util.hpp"

namespace {

std::string test_dsn() {
  const char* env = std::getenv("TEST_DATABASE_URL");
  return env != nullptr ? env : "";
}

class StorageTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto dsn = test_dsn();
    if (dsn.empty()) GTEST_SKIP() << "TEST_DATABASE_URL no definida";
    std::string err;
    store_ = tds::make_store(dsn, TDS_MIGRATIONS_DIR, err);
    ASSERT_TRUE(store_ != nullptr) << err;
    // Estado limpio por prueba (no se toca schema_migrations).
    pqxx::connection conn(dsn);
    pqxx::work tx(conn);
    tx.exec("TRUNCATE jobs CASCADE");
    tx.commit();
    jobs_.clear();
  }

  tds::CreateJobInput make_input(const std::string& id) {
    tds::CreateJobInput input;
    input.job_id = id;
    input.source_url = "https://example.com/video.mp4";
    input.recipient = "5300000000";
    input.client_token_hash = tds::sha256_hex(id + "-token");
    input.transfer_id = tds::new_uuid();
    input.encoding_version = 1;
    input.part_bytes = 65536;
    return input;
  }

  std::unique_ptr<tds::JobStore> store_;
  std::vector<std::string> jobs_;
};

}  // namespace

TEST_F(StorageTest, CreateGetAndClaim) {
  const std::string id = tds::new_uuid();
  std::string err;
  ASSERT_TRUE(store_->create_job(make_input(id), err)) << err;

  tds::JobRecord job;
  ASSERT_TRUE(store_->get_job(id, job, err)) << err;
  EXPECT_EQ(job.status, "queued");
  EXPECT_EQ(job.recipient, "5300000000");
  EXPECT_EQ(job.encoding_version, 1);

  EXPECT_FALSE(store_->get_job(tds::new_uuid(), job, err));

  tds::JobRecord claimed;
  ASSERT_TRUE(store_->claim_job("worker-a", {"queued"}, "downloading", claimed, err))
      << err;
  EXPECT_EQ(claimed.job_id, id);
  EXPECT_EQ(claimed.status, "downloading");

  // Nadie más puede reclamarlo mientras el lease esté vivo.
  tds::JobRecord other;
  EXPECT_FALSE(store_->claim_job("worker-b", {"queued"}, "downloading", other, err));
}

TEST_F(StorageTest, EncodingFlowThenDeliveryAndConfirmation) {
  const std::string id = tds::new_uuid();
  std::string err;
  ASSERT_TRUE(store_->create_job(make_input(id), err)) << err;
  tds::JobRecord job;
  ASSERT_TRUE(store_->claim_job("a", {"queued"}, "downloading", job, err)) << err;
  ASSERT_TRUE(store_->transition(id, "downloading", "encoding", "", "", err)) << err;

  std::vector<tds::DataPart> parts;
  parts.push_back({0, "abc", 3});
  parts.push_back({1, "def", 3});
  ASSERT_TRUE(store_->append_parts(id, parts, err)) << err;
  // Idempotente.
  ASSERT_TRUE(store_->append_parts(id, parts, err)) << err;

  ASSERT_TRUE(store_->finish_encoding(id, 6, tds::sha256_hex("abcdef"), 2, err))
      << err;
  ASSERT_TRUE(store_->get_job(id, job, err));
  EXPECT_EQ(job.status, "ready_for_delivery");
  EXPECT_EQ(job.size_bytes, 6);
  EXPECT_EQ(job.data_parts, 2);
  EXPECT_EQ(job.sha256, tds::sha256_hex("abcdef"));

  std::vector<tds::DataPart> loaded;
  ASSERT_TRUE(store_->load_parts(id, 0, 10, loaded, err)) << err;
  ASSERT_EQ(loaded.size(), 2u);
  EXPECT_EQ(loaded[0].payload, "abc");
  EXPECT_EQ(loaded[1].index, 1);

  tds::JobRecord sending;
  ASSERT_TRUE(store_->claim_job("b", {"ready_for_delivery"}, "sending", sending, err))
      << err;
  EXPECT_EQ(sending.status, "sending");

  ASSERT_TRUE(store_->set_chunk_plan(id, 2, 8192, err)) << err;
  ASSERT_TRUE(store_->mark_chunk_sent(id, 0, "msg-0", err)) << err;
  ASSERT_TRUE(store_->mark_chunk_sent(id, 1, "msg-1", err)) << err;
  std::uint32_t delivered = 0;
  ASSERT_TRUE(store_->delivered_count(id, delivered, err)) << err;
  EXPECT_EQ(delivered, 2u);
  // La lista de índices enviados es la que permite reanudar tras un reinicio
  // sin repetir tramas (el worker B la consulta al reclamar de nuevo).
  std::vector<std::uint32_t> sent;
  ASSERT_TRUE(store_->sent_chunks(id, sent, err)) << err;
  ASSERT_EQ(sent.size(), 2u);
  EXPECT_EQ(sent[0], 0u);
  EXPECT_EQ(sent[1], 1u);
  ASSERT_TRUE(store_->mark_chunk_acked(id, 0, err)) << err;

  ASSERT_TRUE(store_->transition(id, "sending", "awaiting_client_confirmation", "", "", err))
      << err;

  // Confirmación con hash incorrecto.
  EXPECT_FALSE(store_->confirm_reconstruction(id, std::string(64, '0'), 6, err));
  EXPECT_FALSE(store_->confirm_reconstruction(id, tds::sha256_hex("abcdef"), 5, err));

  ASSERT_TRUE(store_->confirm_reconstruction(id, tds::sha256_hex("abcdef"), 6, err))
      << err;
  ASSERT_TRUE(store_->get_job(id, job, err));
  EXPECT_EQ(job.status, "completed");

  // Los datos aún existen hasta la limpieza.
  ASSERT_TRUE(store_->load_parts(id, 0, 10, loaded, err)) << err;
  EXPECT_EQ(loaded.size(), 2u);

  ASSERT_TRUE(store_->cleanup_job_data(id, err)) << err;
  ASSERT_TRUE(store_->get_job(id, job, err));
  EXPECT_EQ(job.status, "cleaned");
  ASSERT_TRUE(store_->load_parts(id, 0, 10, loaded, err)) << err;
  EXPECT_TRUE(loaded.empty());

  std::vector<std::string> events;
  ASSERT_TRUE(store_->list_events(id, events, err)) << err;
  EXPECT_FALSE(events.empty());
}

TEST_F(StorageTest, TransitionIsGuarded) {
  const std::string id = tds::new_uuid();
  std::string err;
  ASSERT_TRUE(store_->create_job(make_input(id), err)) << err;
  EXPECT_FALSE(store_->transition(id, "downloading", "encoding", "", "", err));
  ASSERT_TRUE(store_->transition(id, "queued", "downloading", "", "", err)) << err;
  EXPECT_FALSE(store_->transition(id, "queued", "downloading", "", "", err));
}

TEST_F(StorageTest, ChunkPlanIsImmutableOnceSet) {
  const std::string id = tds::new_uuid();
  std::string err;
  ASSERT_TRUE(store_->create_job(make_input(id), err)) << err;
  ASSERT_TRUE(store_->set_chunk_plan(id, 5, 8192, err)) << err;
  // Repetir el mismo plan es idempotente.
  EXPECT_TRUE(store_->set_chunk_plan(id, 5, 8192, err)) << err;
  // Un plan distinto no debe sobrescribirse.
  EXPECT_FALSE(store_->set_chunk_plan(id, 4, 8192, err));
}

TEST_F(StorageTest, ExpireStaleRespectsConfirmationWindow) {
  const auto dsn = test_dsn();
  const std::string id = tds::new_uuid();
  std::string err;
  ASSERT_TRUE(store_->create_job(make_input(id), err)) << err;

  // Trabajo en curso: se expira con la ventana de `stale_minutes`.
  pqxx::connection conn(dsn);
  {
    pqxx::work tx(conn);
    tx.exec_params(
        "UPDATE jobs SET status='downloading', "
        "updated_at=now() - interval '2 hours' WHERE job_id=$1", id);
    tx.commit();
  }
  int expired = 0;
  ASSERT_TRUE(store_->expire_stale(60, 24, 24, expired, err)) << err;
  EXPECT_EQ(expired, 1);
  tds::JobRecord job;
  ASSERT_TRUE(store_->get_job(id, job, err)) << err;
  EXPECT_EQ(job.status, "expired");

  // Esperando confirmación: la ventana es `client_confirm_hours`, no `stale`.
  const std::string id2 = tds::new_uuid();
  ASSERT_TRUE(store_->create_job(make_input(id2), err)) << err;
  {
    pqxx::work tx(conn);
    tx.exec_params(
        "UPDATE jobs SET status='awaiting_client_confirmation', "
        "updated_at=now() - interval '2 hours' WHERE job_id=$1", id2);
    tx.commit();
  }
  expired = 0;
  ASSERT_TRUE(store_->expire_stale(1, 24, 24, expired, err)) << err;
  EXPECT_EQ(expired, 0);
  ASSERT_TRUE(store_->get_job(id2, job, err)) << err;
  EXPECT_EQ(job.status, "awaiting_client_confirmation");

  {
    pqxx::work tx(conn);
    tx.exec_params(
        "UPDATE jobs SET updated_at=now() - interval '25 hours' WHERE job_id=$1",
        id2);
    tx.commit();
  }
  ASSERT_TRUE(store_->expire_stale(1, 24, 24, expired, err)) << err;
  EXPECT_EQ(expired, 1);
  ASSERT_TRUE(store_->get_job(id2, job, err)) << err;
  EXPECT_EQ(job.status, "expired");
}

TEST_F(StorageTest, PingWorks) {
  if (!store_) GTEST_SKIP();
  std::string err;
  EXPECT_TRUE(store_->ping(err)) << err;
}
