#include "tds/transport.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>

#include "tds/util.hpp"

namespace tds {

bool MockTransport::connect(std::string& err) {
  std::error_code ec;
  std::filesystem::create_directories(outbox_dir_, ec);
  if (ec) {
    err = "no se puede crear el outbox: " + ec.message();
    return false;
  }
  if (run_id_.empty()) run_id_ = random_hex(4);
  return true;
}

bool MockTransport::send_text(std::string_view to, std::string_view body,
                              std::string& message_id, std::string& err) {
  ++sequence_;
  if (run_id_.empty()) run_id_ = random_hex(4);
  char name[96];
  std::snprintf(name, sizeof name, "%s-%08llu-%s.txt", run_id_.c_str(),
                static_cast<unsigned long long>(sequence_), std::string(to).c_str());
  const auto path = std::filesystem::path(outbox_dir_) / name;
  const auto tmp = path.string() + ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
      err = "no se puede escribir en el outbox";
      return false;
    }
    out.write(body.data(), static_cast<std::streamsize>(body.size()));
    out.flush();
    if (!out) {
      err = "fallo escribiendo el mensaje simulado";
      return false;
    }
  }
  std::error_code ec;
  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    err = "no se puede publicar el mensaje simulado: " + ec.message();
    return false;
  }
  message_id = "mock-" + std::to_string(sequence_);
  return true;
}

}  // namespace tds
