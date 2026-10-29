// SHA-256 con OpenSSL EVP (API moderna, sin deprecadas).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace tds {

class Sha256 {
 public:
  Sha256();
  ~Sha256();
  Sha256(const Sha256&) = delete;
  Sha256& operator=(const Sha256&) = delete;

  void update(const void* data, std::size_t size);
  void update(std::string_view data) { update(data.data(), data.size()); }
  // Devuelve el digest y deja el objeto inservible hasta reset().
  std::string final_hex();
  void reset();

 private:
  void* ctx_{nullptr};
};

std::string sha256_hex(std::string_view data);

// Hash de un fichero leído por bloques. Devuelve false y rellena `err` si no
// se puede leer.
bool sha256_file(const std::string& path, std::string& hex_out,
                 std::uint64_t& size_out, std::string& err);

}  // namespace tds
