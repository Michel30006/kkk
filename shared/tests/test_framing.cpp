#include <gtest/gtest.h>

#include <random>
#include <string>
#include <vector>

#include "tds/encoding.hpp"
#include "tds/framing.hpp"
#include "tds/reassembler.hpp"
#include "tds/sha256.hpp"

namespace {

std::vector<std::uint8_t> random_bytes(std::size_t n, unsigned seed = 99) {
  std::mt19937 rng(seed);
  std::uniform_int_distribution<int> dist(0, 255);
  std::vector<std::uint8_t> out(n);
  for (auto& b : out) b = static_cast<std::uint8_t>(dist(rng));
  return out;
}

std::vector<std::string> split_parts(const std::string& encoded, std::size_t parts) {
  std::vector<std::string> out;
  const auto step = encoded.size() / parts;
  std::size_t pos = 0;
  for (std::size_t i = 0; i + 1 < parts; ++i) {
    // Corta en frontera de símbolo (1 o 2 bytes).
    std::size_t end = pos + step;
    while (end < encoded.size() &&
           (static_cast<unsigned char>(encoded[end]) & 0xC0) == 0x80)
      ++end;
    out.emplace_back(encoded.substr(pos, end - pos));
    pos = end;
  }
  out.emplace_back(encoded.substr(pos));
  return out;
}

}  // namespace

TEST(Framing, RoundTripWithinStanzaLimit) {
  const auto data = random_bytes(40 * 1024);
  const auto encoded = tds::encode(data);
  const std::string job = "job-123";
  const std::string transfer = "transfer-456";
  const std::size_t limit = 8192;

  std::uint32_t total = 0;
  std::string err;
  const std::vector<std::string_view> parts{encoded};
  ASSERT_TRUE(tds::count_frames(parts, job, transfer, limit, total, err)) << err;
  ASSERT_GT(total, 1u);

  std::vector<std::string> wires;
  std::uint32_t emitted = 0;
  ASSERT_TRUE(tds::iterate_frames(parts, job, transfer, total, limit, 0,
                                  [&](tds::Frame&& frame) {
                                    wires.push_back(tds::serialize_frame(frame));
                                    return true;
                                  },
                                  emitted, err))
      << err;
  EXPECT_EQ(emitted, total);
  ASSERT_EQ(wires.size(), total);

  std::string rebuilt;
  for (std::size_t i = 0; i < wires.size(); ++i) {
    EXPECT_LE(wires[i].size(), limit) << "trama " << i;
    tds::Frame frame;
    ASSERT_TRUE(tds::parse_frame(wires[i], frame, err)) << err;
    EXPECT_EQ(frame.job_id, job);
    EXPECT_EQ(frame.transfer_id, transfer);
    EXPECT_EQ(frame.chunk_index, i);
    EXPECT_EQ(frame.total_chunks, total);
    EXPECT_EQ(frame.encoding_version, 1);
    rebuilt += frame.payload;
  }
  std::string decoded;
  ASSERT_TRUE(tds::decode_text(rebuilt, decoded, err)) << err;
  EXPECT_EQ(decoded.size(), data.size());
  EXPECT_EQ(tds::sha256_hex(decoded),
            tds::sha256_hex(std::string_view(
                reinterpret_cast<const char*>(data.data()), data.size())));
}

TEST(Framing, MultiPartAndOddBoundaries) {
  const auto data = random_bytes(50 * 1024, 7);
  const auto encoded = tds::encode(data);
  const auto parts_owned = split_parts(encoded, 7);
  std::vector<std::string_view> parts;
  for (const auto& p : parts_owned) parts.push_back(p);

  std::uint32_t total = 0;
  std::string err;
  ASSERT_TRUE(tds::count_frames(parts, "j1", "t1", 4096, total, err)) << err;
  std::string rebuilt;
  std::uint32_t emitted = 0;
  ASSERT_TRUE(tds::iterate_frames(parts, "j1", "t1", total, 4096, 0,
                                  [&](tds::Frame&& f) {
                                    rebuilt += f.payload;
                                    return true;
                                  },
                                  emitted, err))
      << err;
  EXPECT_EQ(emitted, total);
  std::string decoded;
  ASSERT_TRUE(tds::decode_text(rebuilt, decoded, err)) << err;
  EXPECT_EQ(decoded, std::string(reinterpret_cast<const char*>(data.data()), data.size()));
}

TEST(Framing, RejectsCorruption) {
  const auto data = random_bytes(2048, 5);
  const auto encoded = tds::encode(data);
  std::uint32_t total = 0;
  std::string err;
  const std::vector<std::string_view> parts{encoded};
  ASSERT_TRUE(tds::count_frames(parts, "j", "t", 8192, total, err));

  tds::Frame frame;
  std::uint32_t emitted = 0;
  std::string wire;
  bool captured = false;
  ASSERT_TRUE(tds::iterate_frames(parts, "j", "t", total, 8192, 0,
                                  [&](tds::Frame&& f) {
                                    if (captured) return false;
                                    captured = true;
                                    wire = tds::serialize_frame(f);
                                    return true;
                                  },
                                  emitted, err))
      << err;

  tds::Frame parsed;
  err.clear();
  ASSERT_TRUE(tds::parse_frame(wire, parsed, err)) << err;
  auto tampered = wire;
  tampered.back() = tampered.back() == '!' ? '#' : '!';
  EXPECT_FALSE(tds::parse_frame(tampered, parsed, err));
  EXPECT_FALSE(tds::parse_frame("XXXX|j|t|0|1|1|00000000|!", parsed, err));
  EXPECT_FALSE(tds::parse_frame(std::string_view(wire).substr(0, wire.size() / 2), parsed, err));
}

TEST(Framing, RejectsBadIndexes) {
  const auto data = random_bytes(256, 3);
  const auto encoded = tds::encode(data);
  const std::string wire = "TDS1|j|t|2|2|1|00000000|" + encoded;
  tds::Frame frame;
  std::string err;
  EXPECT_FALSE(tds::parse_frame(wire, frame, err));  // índice fuera de rango
}

TEST(Reassembler, ReconstructsInOrderAndOutOfOrder) {
  const auto data = random_bytes(30 * 1024, 11);
  const auto encoded = tds::encode(data);
  tds::JobMeta meta;
  meta.job_id = "job";
  meta.transfer_id = "transfer";
  meta.size_bytes = data.size();
  meta.sha256 = tds::sha256_hex(std::string_view(
      reinterpret_cast<const char*>(data.data()), data.size()));
  meta.encoding_version = 1;

  const std::vector<std::string_view> parts{encoded};
  std::uint32_t total = 0;
  std::string err;
  ASSERT_TRUE(tds::count_frames(parts, meta.job_id, meta.transfer_id, 2048, total, err));
  std::vector<std::string> wires;
  std::uint32_t emitted = 0;
  ASSERT_TRUE(tds::iterate_frames(parts, meta.job_id, meta.transfer_id, total, 2048, 0,
                                  [&](tds::Frame&& f) {
                                    wires.push_back(tds::serialize_frame(f));
                                    return true;
                                  },
                                  emitted, err));

  tds::Reassembler in_order(meta);
  for (const auto& w : wires) EXPECT_EQ(in_order.add(w, err), tds::AddOutcome::kAccepted);
  ASSERT_TRUE(in_order.complete());
  std::string bytes;
  ASSERT_TRUE(in_order.reconstruct(bytes, err)) << err;
  EXPECT_EQ(bytes, std::string(reinterpret_cast<const char*>(data.data()), data.size()));

  tds::Reassembler shuffled(meta);
  for (std::size_t i = wires.size(); i-- > 0;) {
    EXPECT_EQ(shuffled.add(wires[i], err), tds::AddOutcome::kAccepted);
    EXPECT_EQ(shuffled.add(wires[i], err), tds::AddOutcome::kDuplicate);
  }
  EXPECT_TRUE(shuffled.complete());
  ASSERT_TRUE(shuffled.reconstruct(bytes, err)) << err;

  tds::Reassembler missing(meta);
  for (std::size_t i = 1; i < wires.size(); ++i) missing.add(wires[i], err);
  EXPECT_FALSE(missing.complete());
  EXPECT_EQ(missing.missing().size(), 1u);
  EXPECT_FALSE(missing.reconstruct(bytes, err));
}

TEST(Reassembler, DetectsWrongHashAndForeignJob) {
  const auto data = random_bytes(4096, 13);
  const auto encoded = tds::encode(data);
  tds::JobMeta meta;
  meta.job_id = "job";
  meta.transfer_id = "tr";
  meta.size_bytes = data.size();
  meta.sha256 = std::string(64, '0');  // hash deliberadamente erróneo
  meta.encoding_version = 1;

  std::uint32_t total = 0;
  std::string err;
  const std::vector<std::string_view> parts{encoded};
  ASSERT_TRUE(tds::count_frames(parts, "job", "tr", 8192, total, err));
  tds::Reassembler r(meta);
  std::uint32_t emitted = 0;
  ASSERT_TRUE(tds::iterate_frames(parts, "job", "tr", total, 8192, 0,
                                  [&](tds::Frame&& f) {
                                    r.add(tds::serialize_frame(f), err);
                                    return true;
                                  },
                                  emitted, err));
  ASSERT_TRUE(r.complete());
  std::string bytes;
  EXPECT_FALSE(r.reconstruct(bytes, err));

  // Trama de otro trabajo.
  const std::string foreign = "TDS1|otro|tr|0|1|1|00000000|" + encoded.substr(0, 4);
  std::string parse_err;
  tds::Reassembler r2(meta);
  EXPECT_EQ(r2.add(foreign, parse_err), tds::AddOutcome::kRejected);
}
