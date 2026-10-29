#include <gtest/gtest.h>

#include <fstream>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "tds/codec.hpp"
#include "tds/crc32.hpp"
#include "tds/encoding.hpp"
#include "tds/sha256.hpp"

namespace {

std::vector<std::uint8_t> random_bytes(std::size_t n, unsigned seed = 1234) {
  std::mt19937 rng(seed);
  std::uniform_int_distribution<int> dist(0, 255);
  std::vector<std::uint8_t> out(n);
  for (auto& b : out) b = static_cast<std::uint8_t>(dist(rng));
  return out;
}

bool read_file(const std::string& path, std::string& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return true;
}

}  // namespace

TEST(Encoding, TableHas256UniquePrintableSymbols) {
  std::string err;
  ASSERT_TRUE(tds::validate_table(err)) << err;
  std::set<std::string> seen;
  for (int i = 0; i < 256; ++i) {
    const auto symbol = tds::symbol_for(static_cast<std::uint8_t>(i));
    EXPECT_FALSE(symbol.empty());
    EXPECT_LE(symbol.size(), 2u);
    EXPECT_TRUE(seen.insert(std::string(symbol)).second);
  }
}

TEST(Encoding, TableFileMatchesCompiledTable) {
  const std::string path = std::string(TDS_RESOURCE_DIR) + "/table_v1.txt";
  std::string text;
  ASSERT_TRUE(read_file(path, text)) << path;
  ASSERT_EQ(text.size(), 424u);  // 88*1 + 168*2
  std::string concat;
  for (int i = 0; i < 256; ++i) concat.append(tds::symbol_for(static_cast<std::uint8_t>(i)));
  EXPECT_EQ(concat, text);
  EXPECT_EQ(tds::sha256_hex(text), tds::table_sha256());
}

TEST(Encoding, RoundTripEveryByteIndividually) {
  for (int i = 0; i < 256; ++i) {
    const auto b = static_cast<std::uint8_t>(i);
    const std::string encoded = tds::encode(std::span<const std::uint8_t>(&b, 1));
    std::string decoded;
    std::string err;
    ASSERT_TRUE(tds::decode_text(encoded, decoded, err)) << "byte " << i << ": " << err;
    ASSERT_EQ(decoded.size(), 1u);
    EXPECT_EQ(static_cast<std::uint8_t>(decoded[0]), b);
  }
}

TEST(Encoding, RoundTripAllValuesAndRandomData) {
  std::vector<std::uint8_t> all(256);
  for (int i = 0; i < 256; ++i) all[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i);
  for (const auto& data : {all, random_bytes(1), random_bytes(4096),
                           random_bytes(1024 * 1024 + 7)}) {
    const std::string encoded = tds::encode(data);
    std::string decoded;
    std::string err;
    ASSERT_TRUE(tds::decode_text(encoded, decoded, err)) << err;
    ASSERT_EQ(decoded.size(), data.size());
    EXPECT_EQ(tds::sha256_hex(decoded), tds::sha256_hex(
        std::string_view(reinterpret_cast<const char*>(data.data()), data.size())));
  }
}

TEST(Encoding, RejectsForeignSymbolsAndInvalidUtf8) {
  std::string out;
  std::string err;
  EXPECT_FALSE(tds::decode_text("hola mundo", out, err));  // espacio no está en la tabla
  EXPECT_FALSE(tds::decode_text("\x01", out, err));
  EXPECT_FALSE(tds::decode_text("\xC3", out, err));  // truncado
  EXPECT_FALSE(tds::decode_text("\xFF", out, err));
}

TEST(Encoding, MeasureCountsBytes) {
  std::vector<std::uint8_t> data = random_bytes(1000);
  const std::string encoded = tds::encode(data);
  std::uint64_t decoded = 0, utf8 = 0;
  std::string err;
  ASSERT_TRUE(tds::measure_encoded(encoded, decoded, utf8, err));
  EXPECT_EQ(decoded, data.size());
  EXPECT_EQ(utf8, encoded.size());
}

TEST(Hash, Sha256KnownVectors) {
  EXPECT_EQ(tds::sha256_hex(""),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  EXPECT_EQ(tds::sha256_hex("abc"),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  EXPECT_EQ(tds::sha256_hex("The quick brown fox jumps over the lazy dog"),
            "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");
}

TEST(Hash, Sha256IncrementalMatchesOneShot) {
  const std::string data(100000, 'x');
  tds::Sha256 h;
  std::size_t pos = 0;
  while (pos < data.size()) {
    const auto n = std::min<std::size_t>(777, data.size() - pos);
    h.update(data.data() + pos, n);
    pos += n;
  }
  EXPECT_EQ(h.final_hex(), tds::sha256_hex(data));
}

TEST(Checksum, Crc32KnownVector) {
  EXPECT_EQ(tds::crc32("123456789"), 0xCBF43926u);
  EXPECT_EQ(tds::crc32(""), 0u);
}

TEST(Codec, HexAndBase64RoundTrip) {
  const std::string raw = std::string("\x00\x01\xFE\xFF hello", 10);
  EXPECT_EQ(tds::to_hex(raw), "0001feff2068656c6c6f");
  std::string back;
  ASSERT_TRUE(tds::from_hex(tds::to_hex(raw), back));
  EXPECT_EQ(back, raw);

  for (const std::string& sample : {std::string(), std::string("a"), std::string("ab"),
                                    std::string("abc"), std::string("abcd"),
                                    std::string(300, 'z')}) {
    const auto b64 = tds::base64_encode(sample);
    std::string decoded;
    ASSERT_TRUE(tds::base64_decode(b64, decoded));
    EXPECT_EQ(decoded, sample);
  }
  EXPECT_EQ(tds::base64_encode("user"), "dXNlcg==");
}
