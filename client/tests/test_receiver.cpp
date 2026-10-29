#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "receiver_core.hpp"
#include "tds/encoding.hpp"
#include "tds/framing.hpp"
#include "tds/sha256.hpp"
#include "tds/util.hpp"

namespace {

std::vector<std::uint8_t> random_bytes(std::size_t n, unsigned seed = 7) {
  std::mt19937 rng(seed);
  std::uniform_int_distribution<int> dist(0, 255);
  std::vector<std::uint8_t> out(n);
  for (auto& b : out) b = static_cast<std::uint8_t>(dist(rng));
  return out;
}

struct Fixture {
  std::string dir;
  tds::JobMeta meta;
  std::vector<std::string> wires;
  std::string bytes;
};

Fixture make_fixture() {
  Fixture f;
  const auto data = random_bytes(9000);
  f.bytes.assign(reinterpret_cast<const char*>(data.data()), data.size());
  const auto encoded = tds::encode(data);
  f.meta.job_id = "job-1";
  f.meta.transfer_id = "tr-1";
  f.meta.size_bytes = data.size();
  f.meta.sha256 = tds::sha256_hex(f.bytes);
  f.meta.encoding_version = 1;

  std::uint32_t total = 0;
  std::string err;
  const std::vector<std::string_view> parts{encoded};
  tds::count_frames(parts, f.meta.job_id, f.meta.transfer_id, 2048, total, err);
  std::uint32_t emitted = 0;
  tds::iterate_frames(parts, f.meta.job_id, f.meta.transfer_id, total, 2048, 0,
                      [&](tds::Frame&& frame) {
                        f.wires.push_back(tds::serialize_frame(frame));
                        return true;
                      },
                      emitted, err);

  f.dir = "/tmp/opencode/tds-frames-" + tds::random_hex(6);
  std::filesystem::create_directories(f.dir);
  int i = 0;
  for (const auto& wire : f.wires) {
    char name[64];
    std::snprintf(name, sizeof name, "%08d.txt", i++);
    std::ofstream out(std::filesystem::path(f.dir) / name, std::ios::binary);
    out << wire;
  }
  return f;
}

}  // namespace

TEST(ReceiverCore, ParsesMetadataAndRejectsIncomplete) {
  tds::JobMeta meta;
  std::string err;
  ASSERT_TRUE(tds::parse_job_meta_json(
      R"({"job_id":"j1","transfer_id":"t1","status":"ready_for_delivery",
          "size_bytes":123,"sha256":")" +
          std::string(64, 'a') +
          R"(","encoding_version":1,"total_chunks":5})",
      meta, err))
      << err;
  EXPECT_EQ(meta.job_id, "j1");
  EXPECT_EQ(meta.size_bytes, 123u);
  EXPECT_EQ(meta.total_chunks, 5u);

  EXPECT_FALSE(tds::parse_job_meta_json(
      R"({"job_id":"j1","transfer_id":"t1","size_bytes":0,"sha256":")" +
          std::string(64, 'a') + R"("})",
      meta, err));
  EXPECT_FALSE(tds::parse_job_meta_json(
      R"({"job_id":"","transfer_id":"t1","size_bytes":1,"sha256":")" +
          std::string(64, 'a') + R"("})",
      meta, err));
  EXPECT_FALSE(tds::parse_job_meta_json("no json", meta, err));
}

TEST(ReceiverCore, LoadsDirectoryAndReconstructsVerifiedFile) {
  auto fixture = make_fixture();
  std::vector<std::string> wires;
  std::string err;
  ASSERT_TRUE(tds::load_frames_dir(fixture.dir, wires, err)) << err;
  ASSERT_EQ(wires.size(), fixture.wires.size());

  const auto out = "/tmp/opencode/out-" + tds::random_hex(6) + ".bin";
  const auto result = tds::reassemble_frames(wires, fixture.meta, out);
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(result.size, fixture.bytes.size());

  std::ifstream in(out, std::ios::binary);
  const std::string got((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
  EXPECT_EQ(got, fixture.bytes);
  EXPECT_EQ(tds::sha256_hex(got), fixture.meta.sha256);

  std::filesystem::remove(out);
  std::filesystem::remove_all(fixture.dir);
}

TEST(ReceiverCore, FailsOnMissingFrameAndBadHash) {
  auto fixture = make_fixture();
  auto wires = fixture.wires;

  wires.erase(wires.begin() + 1);
  const auto out = "/tmp/opencode/out-" + tds::random_hex(6) + ".bin";
  auto result = tds::reassemble_frames(wires, fixture.meta, out);
  EXPECT_FALSE(result.ok);
  EXPECT_NE(result.error.find("faltan"), std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(out));

  auto wrong = fixture.meta;
  wrong.sha256 = std::string(64, '0');
  result = tds::reassemble_frames(fixture.wires, wrong, out);
  EXPECT_FALSE(result.ok);
  EXPECT_NE(result.error.find("SHA-256"), std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(out));

  // Una trama corrupta se rechaza y no se escribe salida.
  auto corrupt = fixture.wires;
  corrupt[0].back() = corrupt[0].back() == '!' ? '#' : '!';
  result = tds::reassemble_frames(corrupt, fixture.meta, out);
  EXPECT_FALSE(result.ok);
  EXPECT_FALSE(std::filesystem::exists(out));

  std::filesystem::remove_all(fixture.dir);
}
