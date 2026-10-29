#include "tds/sha256.hpp"

#include <openssl/evp.h>

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>

namespace tds {
namespace {

EVP_MD_CTX* ctx_of(void* p) { return static_cast<EVP_MD_CTX*>(p); }

std::string hex_of(const unsigned char* data, unsigned int len) {
  static const char* digits = "0123456789abcdef";
  std::string out;
  out.resize(static_cast<std::size_t>(len) * 2);
  for (unsigned int i = 0; i < len; ++i) {
    out[i * 2] = digits[data[i] >> 4];
    out[i * 2 + 1] = digits[data[i] & 0x0F];
  }
  return out;
}

}  // namespace

Sha256::Sha256() {
  auto* ctx = EVP_MD_CTX_new();
  if (ctx == nullptr || EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
    if (ctx) EVP_MD_CTX_free(ctx);
    throw std::runtime_error("no se pudo inicializar SHA-256");
  }
  ctx_ = ctx;
}

Sha256::~Sha256() {
  if (ctx_) EVP_MD_CTX_free(ctx_of(ctx_));
}

void Sha256::update(const void* data, std::size_t size) {
  if (ctx_ == nullptr) throw std::runtime_error("SHA-256 ya finalizado");
  if (size == 0) return;
  if (EVP_DigestUpdate(ctx_of(ctx_), data, size) != 1)
    throw std::runtime_error("fallo en SHA-256 update");
}

std::string Sha256::final_hex() {
  if (ctx_ == nullptr) throw std::runtime_error("SHA-256 ya finalizado");
  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int len = 0;
  if (EVP_DigestFinal_ex(ctx_of(ctx_), digest, &len) != 1)
    throw std::runtime_error("fallo en SHA-256 final");
  EVP_MD_CTX_free(ctx_of(ctx_));
  ctx_ = nullptr;
  return hex_of(digest, len);
}

void Sha256::reset() {
  if (ctx_ == nullptr) {
    ctx_ = EVP_MD_CTX_new();
    if (ctx_ == nullptr) throw std::runtime_error("no se pudo reiniciar SHA-256");
  }
  if (EVP_DigestInit_ex(ctx_of(ctx_), EVP_sha256(), nullptr) != 1)
    throw std::runtime_error("no se pudo reiniciar SHA-256");
}

std::string sha256_hex(std::string_view data) {
  Sha256 h;
  h.update(data);
  return h.final_hex();
}

bool sha256_file(const std::string& path, std::string& hex_out,
                 std::uint64_t& size_out, std::string& err) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) {
    err = "no se puede abrir " + path;
    return false;
  }
  Sha256 h;
  std::vector<unsigned char> buf(64 * 1024);
  size_out = 0;
  bool ok = true;
  while (true) {
    const auto n = std::fread(buf.data(), 1, buf.size(), f);
    if (n > 0) {
      h.update(buf.data(), n);
      size_out += n;
    }
    if (n < buf.size()) {
      if (std::ferror(f)) {
        err = "error leyendo " + path;
        ok = false;
      }
      break;
    }
  }
  std::fclose(f);
  if (ok) hex_out = h.final_hex();
  return ok;
}

}  // namespace tds
