// Benchmark sencillo sin dependencias: codificación, decodificación y plan de
// tramas. Uso: tds_bench [bytes]
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "tds/encoding.hpp"
#include "tds/framing.hpp"
#include "tds/reassembler.hpp"
#include "tds/sha256.hpp"

namespace {

double seconds_since(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
      .count();
}

}  // namespace

int main(int argc, char** argv) {
  std::size_t size = 25ull * 1024 * 1024;
  if (argc > 1) size = static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10));
  std::mt19937 rng(1234);
  std::uniform_int_distribution<int> dist(0, 255);
  std::vector<std::uint8_t> data(size);
  for (auto& b : data) b = static_cast<std::uint8_t>(dist(rng));

  auto start = std::chrono::steady_clock::now();
  const std::string encoded = tds::encode(data);
  const double encode_s = seconds_since(start);

  start = std::chrono::steady_clock::now();
  std::string decoded;
  std::string err;
  if (!tds::decode_text(encoded, decoded, err)) {
    std::fprintf(stderr, "decodificación falló: %s\n", err.c_str());
    return 1;
  }
  const double decode_s = seconds_since(start);

  start = std::chrono::steady_clock::now();
  const auto digest = tds::sha256_hex(decoded);
  const double hash_s = seconds_since(start);

  std::uint32_t total = 0;
  const std::vector<std::string_view> parts{encoded};
  start = std::chrono::steady_clock::now();
  if (!tds::count_frames(parts, "bench-job", "bench-transfer", 8192, total,
                         err)) {
    std::fprintf(stderr, "plan de tramas falló: %s\n", err.c_str());
    return 1;
  }
  const double plan_s = seconds_since(start);

  const double mb = static_cast<double>(size) / (1024.0 * 1024.0);
  std::printf("entrada:            %.2f MiB\n", mb);
  std::printf("codificado:         %.2f MiB (factor %.3f)\n",
              encoded.size() / (1024.0 * 1024.0),
              static_cast<double>(encoded.size()) / static_cast<double>(size));
  std::printf("encode:             %.0f MiB/s (%.1f Msimbolos/s)\n",
              mb / encode_s, static_cast<double>(size) / encode_s / 1e6);
  std::printf("decode:             %.0f MiB/s\n", mb / decode_s);
  std::printf("sha256:             %.0f MiB/s\n", mb / hash_s);
  std::printf("tramas (8192 B):    %u en %.0f ms (%.1f tramas/s)\n", total,
              plan_s * 1000.0, static_cast<double>(total) / plan_s);
  std::printf("sha256(resultado):  %s\n", digest.substr(0, 16).c_str());
  if (decoded.size() != data.size()) {
    std::fprintf(stderr, "tamaño reconstruido incorrecto\n");
    return 1;
  }
  return 0;
}
