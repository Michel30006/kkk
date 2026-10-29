#include "todus_transport.hpp"

#include "tds/log.hpp"
#include "tds/xmpp.hpp"

namespace tds {
namespace {

// Envuelve XmppClient adaptándolo a la interfaz Transport.
class TodusTransport final : public Transport {
 public:
  explicit TodusTransport(XmppConfig config) : client_(std::move(config)) {}

  std::string name() const override { return "todus"; }

  bool connect(std::string& err) override { return client_.connect(err); }

  bool send_text(std::string_view to, std::string_view body,
                 std::string& message_id, std::string& err) override {
    if (!client_.ensure_connected(err)) return false;
    if (!client_.send_message(to, body, message_id, err)) {
      // Un fallo de escritura deja la conexión inservible: se cierra para que
      // el siguiente intento reconecte.
      client_.close();
      return false;
    }
    return true;
  }

  bool wait_ack(std::string_view message_id, int timeout_ms,
                std::string& err) override {
    return client_.wait_ack(message_id, timeout_ms, err);
  }

  void close() override { client_.close(); }

  const std::string& jid() const { return client_.jid(); }

  // El servidor puede entregar a una sola sesión por cuenta: avisamos si
  // llegan mensajes ajenos para no perderlos en silencio.
  void set_inbound_callback(std::function<void(const XmppMessage&)> cb) {
    client_.set_message_callback(std::move(cb));
  }

 private:
  XmppClient client_;
};

}  // namespace

std::unique_ptr<Transport> make_transport(const Config& config,
                                          std::string& err) {
  if (config.transport == "mock") {
    return std::make_unique<MockTransport>(config.mock_outbox,
                                           "server_b@mock");
  }
  if (config.transport != "todus") {
    err = "TRANSPORT desconocido: " + config.transport;
    return nullptr;
  }
  XmppConfig xmpp;
  xmpp.host = config.todus_domain;
  xmpp.domain = config.todus_domain;
  xmpp.port = config.todus_port;
  xmpp.username = config.todus_username;
  xmpp.token = config.todus_token;
  xmpp.password = config.todus_password;
  xmpp.auth_url = config.todus_auth_url;
  xmpp.resource = config.todus_resource;
  xmpp.verify_tls = config.todus_verify_tls;
  xmpp.io_timeout_ms = config.ack_timeout_ms + 5000;
  return std::make_unique<TodusTransport>(xmpp);
}

std::string describe_transport(const Config& config) {
  if (config.transport == "mock") return "mock (" + config.mock_outbox + ")";
  return "todus xmpp " + config.todus_domain + ":" +
         std::to_string(config.todus_port) + " usuario=" +
         (config.todus_username.empty() ? "<vacío>" : config.todus_username);
}

}  // namespace tds
