// Cliente XMPP mínimo para ToDus, en C++ con OpenSSL.
//
// Secuencia verificada en la investigación (investigacion/apk/xmpp):
//   1. TLS directo a im.todus.cu:5222 (no STARTTLS).
//   2. <stream:stream> con la declaración XML sólo la primera vez.
//   3. SASL PLAIN con authcid = <53+teléfono> y contraseña = JWT de la API.
//      El éxito llega abreviado como <ok/>.
//   4. Se reabre el stream (sin declaración XML) y se hace bind clásico
//      (im.todus.cu no responde al bind de SASL2).
//   5. El texto va SIEMPRE dentro de <body>; el servidor acusa con
//      <tdack mi='...'>.
//
// Los nombres de stanza/atributo abreviados del servidor (<m>, <b>, t/i/f/o)
// se normalizan al leer.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

namespace tds {

struct XmppConfig {
  std::string host{"im.todus.cu"};
  std::string domain{"im.todus.cu"};
  int port{5222};
  std::string username;          // 53 + teléfono (sin '+')
  std::string token;             // JWT ya obtenido (preferido)
  std::string password;          // alternativa: se canjea por un JWT
  std::string auth_url{"https://auth.todus.cu/v2/auth/token"};
  std::string client_version{"2.1.2"};
  std::string resource{"tds"};
  bool verify_tls{true};
  int io_timeout_ms{20000};
};

struct XmppMessage {
  std::string id;
  std::string from;
  std::string to;
  std::string type;
  std::string body;
  std::string raw;
};

class XmppClient {
 public:
  explicit XmppClient(XmppConfig config);
  ~XmppClient();
  XmppClient(const XmppClient&) = delete;
  XmppClient& operator=(const XmppClient&) = delete;

  // Obtiene token si hace falta, conecta, autentica y hace bind.
  bool connect(std::string& err);
  bool ensure_connected(std::string& err);
  void close();
  bool is_connected() const;

  bool send_message(std::string_view to, std::string_view body,
                    std::string& message_id, std::string& err);
  // Espera el <tdack> del servidor para ese id.
  bool wait_ack(std::string_view message_id, int timeout_ms, std::string& err);

  const std::string& jid() const { return jid_; }
  void set_message_callback(std::function<void(const XmppMessage&)> callback);

 private:
  struct Stanza;
  bool fetch_token(std::string& err);
  bool open_socket(std::string& err);
  bool tls_handshake(std::string& err);
  bool start_stream(bool with_declaration, std::string& err);
  bool wait_features(std::string& features_raw, std::string& err);
  bool authenticate(std::string& err);
  bool bind_resource(std::string& err);
  bool send_raw(std::string_view data, std::string& err);
  bool read_some(std::string& err, int timeout_ms);
  void reader_loop();
  void handle_stanza(const Stanza& stanza);
  void fail_connection(const std::string& reason);

  XmppConfig config_;
  std::string jid_;
  void* ssl_ctx_{nullptr};
  void* ssl_{nullptr};
  int fd_{-1};

  mutable std::mutex write_mutex_;
  mutable std::mutex state_mutex_;
  std::condition_variable state_cv_;
  std::unordered_set<std::string> acked_;
  std::atomic<bool> connected_{false};
  std::atomic<bool> stop_{false};
  std::thread reader_;
  std::string buffer_;
  std::uint64_t next_id_{0};
  std::function<void(const XmppMessage&)> message_callback_;
  std::int64_t last_activity_{0};
};

// Expuesto para pruebas: extrae stanzas completas del stream.
std::vector<std::string> extract_xmpp_stanzas(std::string& buffer, int& depth);
// Utilidades XML.
std::string xml_escape(std::string_view text);
std::string xml_unescape(std::string_view text);
std::string xml_local_name(std::string_view tag);
std::string xml_attribute(std::string_view tag, std::string_view name);

}  // namespace tds
