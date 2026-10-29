#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace tds {

class Transport {
 public:
  virtual ~Transport() = default;

  virtual std::string name() const = 0;
  virtual bool connect(std::string& err) = 0;
  // Envía `body` al destinatario. `ok` significa que el servidor aceptó el
  // mensaje (o que la simulación lo registró), NO que el teléfono haya
  // reconstruido el fichero.
  virtual bool send_text(std::string_view to, std::string_view body,
                         std::string& message_id, std::string& err) = 0;
  // Espera el acuse de ToDus (tdack) si el transporte lo soporta.
  virtual bool wait_ack(std::string_view message_id, int timeout_ms,
                        std::string& err) {
    (void)message_id;
    (void)timeout_ms;
    err.clear();
    return true;
  }
  virtual void close() {}
};

// Transporte de prueba: escribe cada mensaje en `outbox/<seq>-<to>.txt`.
// Permite la prueba integral local sin cuenta de ToDus.
class MockTransport : public Transport {
 public:
  MockTransport(std::string outbox_dir, std::string from)
      : outbox_dir_(std::move(outbox_dir)), from_(std::move(from)) {}

  std::string name() const override { return "mock"; }
  bool connect(std::string& err) override;
  bool send_text(std::string_view to, std::string_view body,
                 std::string& message_id, std::string& err) override;

  std::uint64_t sequence() const { return sequence_; }

 private:
  std::string outbox_dir_;
  std::string from_;
  // Prefijo único por proceso: si B se reinicia a mitad de una entrega, los
  // ficheros nuevos no pisan los ya escritos (el receptor tolera duplicados).
  std::string run_id_;
  std::uint64_t sequence_{0};
};

}  // namespace tds
