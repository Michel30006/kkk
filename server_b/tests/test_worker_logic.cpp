#include <gtest/gtest.h>

#include <pqxx/pqxx>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "tds/encoding.hpp"
#include "tds/framing.hpp"
#include "tds/reassembler.hpp"
#include "tds/sha256.hpp"
#include "tds/storage.hpp"
#include "tds/transport.hpp"
#include "tds/util.hpp"
#include "worker.hpp"

namespace {

std::string test_dsn() {
  const char* env = std::getenv("TEST_DATABASE_URL");
  return env != nullptr ? env : "";
}

std::vector<std::uint8_t> random_bytes(std::size_t n, unsigned seed) {
  std::mt19937 rng(seed);
  std::uniform_int_distribution<int> dist(0, 255);
  std::vector<std::uint8_t> out(n);
  for (auto& b : out) b = static_cast<std::uint8_t>(dist(rng));
  return out;
}

std::vector<std::string> read_outbox(const std::string& dir) {
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::directory_iterator(dir))
    if (entry.is_regular_file()) files.push_back(entry.path());
  std::sort(files.begin(), files.end());
  std::vector<std::string> out;
  for (const auto& path : files) {
    std::ifstream in(path, std::ios::binary);
    out.emplace_back(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
  }
  return out;
}

class DeliveryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto dsn = test_dsn();
    if (dsn.empty()) GTEST_SKIP() << "TEST_DATABASE_URL no definida";
    std::string err;
    store_ = tds::make_store(dsn, TDS_MIGRATIONS_DIR, err);
    ASSERT_TRUE(store_ != nullptr) << err;
    pqxx::connection conn(dsn);
    pqxx::work tx(conn);
    tx.exec("TRUNCATE jobs CASCADE");
    tx.commit();
    outbox_ = "/tmp/opencode/tds-outbox-" + tds::random_hex(6);
    std::filesystem::remove_all(outbox_);
  }

  void TearDown() override {
    if (!outbox_.empty()) std::filesystem::remove_all(outbox_);
  }

  std::unique_ptr<tds::JobStore> store_;
  std::string outbox_;
};

}  // namespace

TEST_F(DeliveryTest, DeliversFramesAndClientReconstructs) {
  const auto data = random_bytes(12000, 42);
  const std::string bytes(reinterpret_cast<const char*>(data.data()),
                          data.size());
  const std::string encoded = tds::encode(data);

  tds::CreateJobInput input;
  input.job_id = tds::new_uuid();
  input.source_url = "https://example.com/v.mp4";
  input.recipient = "5300000000";
  input.client_token_hash = tds::sha256_hex("token");
  input.transfer_id = tds::new_uuid();
  std::string err;
  ASSERT_TRUE(store_->create_job(input, err)) << err;

  tds::JobRecord job;
  ASSERT_TRUE(store_->claim_job("a", {"queued"}, "downloading", job, err))
      << err;
  ASSERT_TRUE(store_->transition(input.job_id, "downloading", "encoding", "",
                                 "", err))
      << err;
  std::vector<tds::DataPart> parts;
  // Se parte el texto codificado en trozos de bytes (frontera de símbolo).
  std::size_t pos = 0;
  int index = 0;
  while (pos < encoded.size()) {
    std::size_t end = std::min(pos + 1000, encoded.size());
    while (end < encoded.size() &&
           (static_cast<unsigned char>(encoded[end]) & 0xC0) == 0x80)
      ++end;
    parts.push_back({index++, encoded.substr(pos, end - pos),
                     static_cast<std::int64_t>(end - pos)});
    pos = end;
  }
  ASSERT_TRUE(store_->append_parts(input.job_id, parts, err)) << err;
  ASSERT_TRUE(store_->finish_encoding(input.job_id, data.size(),
                                      tds::sha256_hex(bytes), index, err))
      << err;

  tds::JobRecord claimed;
  ASSERT_TRUE(store_->claim_job("b", {"ready_for_delivery"}, "sending",
                                claimed, err))
      << err;

  tds::Config config;
  config.transport = "mock";
  config.mock_outbox = outbox_;
  config.max_stanza_bytes = 2048;
  config.send_delay_ms = 0;
  config.ack_timeout_ms = 200;
  config.max_send_retries = 2;
  config.todus_domain = "im.todus.cu";
  tds::MockTransport transport(outbox_, "server_b@mock");
  tds::ServerBStats stats;
  tds::DeliveryWorker worker(*store_, config, transport, stats);
  ASSERT_TRUE(worker.deliver(claimed));

  tds::JobRecord after;
  ASSERT_TRUE(store_->get_job(input.job_id, after, err)) << err;
  EXPECT_EQ(after.status, "awaiting_client_confirmation");
  EXPECT_GT(after.total_chunks, 1u);
  EXPECT_EQ(stats.jobs_delivered.load(), 1u);

  // El receptor reconstruye desde los mensajes del outbox.
  const auto wires = read_outbox(outbox_);
  ASSERT_EQ(wires.size(), after.total_chunks);
  tds::JobMeta meta;
  meta.job_id = input.job_id;
  meta.transfer_id = input.transfer_id;
  meta.size_bytes = data.size();
  meta.sha256 = tds::sha256_hex(bytes);
  meta.encoding_version = 1;
  tds::Reassembler reassembler(meta);
  for (const auto& wire : wires)
    ASSERT_EQ(reassembler.add(wire, err), tds::AddOutcome::kAccepted) << err;
  ASSERT_TRUE(reassembler.complete());
  std::string rebuilt;
  ASSERT_TRUE(reassembler.reconstruct(rebuilt, err)) << err;
  EXPECT_EQ(rebuilt, bytes);

  // La confirmación del cliente cierra el trabajo.
  ASSERT_TRUE(store_->confirm_reconstruction(input.job_id, meta.sha256,
                                             data.size(), err))
      << err;
  ASSERT_TRUE(store_->get_job(input.job_id, after, err));
  EXPECT_EQ(after.status, "completed");
}

TEST_F(DeliveryTest, RejectsInvalidRecipient) {
  tds::CreateJobInput input;
  input.job_id = tds::new_uuid();
  input.source_url = "https://example.com/v.mp4";
  input.recipient = "no-es-un-numero";
  input.client_token_hash = tds::sha256_hex("token");
  input.transfer_id = tds::new_uuid();
  std::string err;
  ASSERT_TRUE(store_->create_job(input, err)) << err;
  tds::JobRecord claimed;
  ASSERT_TRUE(store_->claim_job("b", {"queued"}, "sending", claimed, err))
      << err;

  tds::Config config;
  config.transport = "mock";
  config.mock_outbox = outbox_;
  tds::MockTransport transport(outbox_, "b");
  tds::ServerBStats stats;
  tds::DeliveryWorker worker(*store_, config, transport, stats);
  EXPECT_FALSE(worker.deliver(claimed));
  tds::JobRecord after;
  ASSERT_TRUE(store_->get_job(input.job_id, after, err));
  EXPECT_EQ(after.status, "failed_delivery");
  EXPECT_EQ(after.error_code, "invalid_recipient");
}
