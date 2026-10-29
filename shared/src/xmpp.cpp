#include "tds/xmpp.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>

#include "tds/codec.hpp"
#include "tds/http.hpp"
#include "tds/log.hpp"
#include "tds/protobuf_auth.hpp"
#include "tds/util.hpp"

namespace tds {

struct XmppClient::Stanza {
  std::string raw;
};

namespace {

constexpr const char* kNsClient = "jabber:client";constexpr const char* kNsStream = "http://etherx.jabber.org/streams";
constexpr const char* kNsSasl = "urn:ietf:params:xml:ns:xmpp-sasl";
constexpr const char* kNsBind = "urn:ietf:params:xml:ns:xmpp-bind";
constexpr const char* kNsSession = "urn:ietf:params:xml:ns:xmpp-session";

std::int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

bool is_space(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string lower(std::string_view text) {
  std::string out(text);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

// Normaliza nombres abreviados del servidor de ToDus.
std::string normalize_element(std::string_view local) {
  const auto name = lower(local);
  if (name == "m") return "message";
  if (name == "p") return "presence";
  if (name == "b") return "body";
  if (name == "es") return "mechanisms";
  if (name == "e") return "mechanism";
  if (name == "b1" || name == "bind") return "bind";
  if (name == "s1") return "session";
  if (name == "ok") return "success";
  return name;
}

std::string normalize_attribute(std::string_view name) {
  const auto key = lower(name);
  if (key == "t") return "type";
  if (key == "i") return "id";
  if (key == "f") return "from";
  if (key == "o") return "to";
  if (key == "mi") return "mi";
  return key;
}

// Devuelve la posición justo después del '>' que cierra la etiqueta que empieza
// en `pos`, respetando comillas.
std::size_t tag_end(std::string_view text, std::size_t pos) {
  bool in_single = false;
  bool in_double = false;
  for (std::size_t i = pos + 1; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '\'' && !in_double) in_single = !in_single;
    else if (c == '"' && !in_single) in_double = !in_double;
    else if (c == '>' && !in_single && !in_double) return i + 1;
  }
  return std::string_view::npos;
}

}  // namespace

std::string xml_escape(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 8);
  for (const char c : text) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&apos;"; break;
      default: out.push_back(c);
    }
  }
  return out;
}

std::string xml_unescape(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (std::size_t i = 0; i < text.size();) {
    if (text[i] == '&') {
      const auto semi = text.find(';', i);
      if (semi != std::string_view::npos && semi - i <= 10) {
        const auto entity = text.substr(i + 1, semi - i - 1);
        if (entity == "amp") out.push_back('&');
        else if (entity == "lt") out.push_back('<');
        else if (entity == "gt") out.push_back('>');
        else if (entity == "quot") out.push_back('"');
        else if (entity == "apos") out.push_back('\'');
        else if (!entity.empty() && entity[0] == '#') {
          // Referencia numérica: decimal (&#233;) o hexadecimal (&#xE9;).
          bool hex = entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X');
          std::string digits(entity.substr(hex ? 2 : 1));
          std::uint32_t value = 0;
          bool valid = !digits.empty() && digits.size() <= 8;
          for (const char d : digits) {
            std::uint32_t v = 0;
            if (d >= '0' && d <= '9') v = static_cast<std::uint32_t>(d - '0');
            else if (hex && d >= 'a' && d <= 'f')
              v = static_cast<std::uint32_t>(d - 'a' + 10);
            else if (hex && d >= 'A' && d <= 'F')
              v = static_cast<std::uint32_t>(d - 'A' + 10);
            else {
              valid = false;
              break;
            }
            value = value * (hex ? 16u : 10u) + v;
          }
          if (valid && value > 0 && value <= 0x10FFFF &&
              !(value >= 0xD800 && value <= 0xDFFF)) {
            // UTF-8: la tabla v1 usa símbolos de 1 y 2 bytes.
            if (value < 0x80) {
              out.push_back(static_cast<char>(value));
            } else if (value < 0x800) {
              out.push_back(static_cast<char>(0xC0 | (value >> 6)));
              out.push_back(static_cast<char>(0x80 | (value & 0x3F)));
            } else if (value < 0x10000) {
              out.push_back(static_cast<char>(0xE0 | (value >> 12)));
              out.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
              out.push_back(static_cast<char>(0x80 | (value & 0x3F)));
            } else {
              out.push_back(static_cast<char>(0xF0 | (value >> 18)));
              out.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3F)));
              out.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
              out.push_back(static_cast<char>(0x80 | (value & 0x3F)));
            }
          }
        } else {
          out.append(text.substr(i, semi - i + 1));
        }
        i = semi + 1;
        continue;
      }
    }
    out.push_back(text[i++]);
  }
  return out;
}

std::string xml_local_name(std::string_view tag) {
  std::size_t begin = 0;
  while (begin < tag.size() && is_space(tag[begin])) ++begin;
  if (begin < tag.size() && tag[begin] == '<') ++begin;
  if (begin < tag.size() && tag[begin] == '/') ++begin;
  std::size_t end = begin;
  while (end < tag.size() && !is_space(tag[end]) && tag[end] != '>' &&
         tag[end] != '/')
    ++end;
  auto name = tag.substr(begin, end - begin);
  const auto colon = name.rfind(':');
  if (colon != std::string_view::npos) name = name.substr(colon + 1);
  return normalize_element(name);
}

std::string xml_attribute(std::string_view tag, std::string_view name) {
  const auto wanted = normalize_attribute(name);
  std::size_t i = 0;
  // Salta el nombre del elemento.
  while (i < tag.size() && !is_space(tag[i]) && tag[i] != '>') ++i;
  while (i < tag.size()) {
    while (i < tag.size() && is_space(tag[i])) ++i;
    if (i >= tag.size() || tag[i] == '>' || tag[i] == '/') break;
    const std::size_t key_start = i;
    while (i < tag.size() && tag[i] != '=' && !is_space(tag[i]) && tag[i] != '>')
      ++i;
    const auto key = tag.substr(key_start, i - key_start);
    while (i < tag.size() && is_space(tag[i])) ++i;
    if (i >= tag.size() || tag[i] != '=') continue;
    ++i;
    while (i < tag.size() && is_space(tag[i])) ++i;
    if (i >= tag.size()) break;
    const char quote = tag[i];
    if (quote != '"' && quote != '\'') continue;
    ++i;
    const std::size_t value_start = i;
    while (i < tag.size() && tag[i] != quote) ++i;
    if (normalize_attribute(key) == wanted)
      return xml_unescape(tag.substr(value_start, i - value_start));
    if (i < tag.size()) ++i;
  }
  return {};
}

std::vector<std::string> extract_xmpp_stanzas(std::string& buffer, int& depth) {
  std::vector<std::string> out;
  std::size_t pos = 0;
  while (pos < buffer.size()) {
    const auto lt = buffer.find('<', pos);
    if (lt == std::string::npos) {
      pos = buffer.size();
      break;
    }
    // Declaración XML, comentarios y CDATA.
    if (buffer.compare(lt, 2, "<?") == 0) {
      const auto end = buffer.find("?>", lt);
      if (end == std::string::npos) break;
      pos = end + 2;
      continue;
    }
    if (buffer.compare(lt, 4, "<!--") == 0) {
      const auto end = buffer.find("-->", lt);
      if (end == std::string::npos) break;
      pos = end + 3;
      continue;
    }
    if (buffer.compare(lt, 9, "<![CDATA[") == 0) {
      const auto end = buffer.find("]]>", lt);
      if (end == std::string::npos) break;
      pos = end + 3;
      continue;
    }
    const auto end = tag_end(buffer, lt);
    if (end == std::string::npos) break;
    const auto tag = std::string_view(buffer).substr(lt, end - lt);
    const bool closing = tag.size() > 1 && tag[1] == '/';
    const bool self_closing = tag.size() > 1 && tag[tag.size() - 2] == '/';
    const auto name = xml_local_name(tag);

    if (depth == 0) {
      if (!closing && name == "stream") {
        depth = 1;
        pos = end;
        continue;
      }
      // Cualquier otra cosa antes del stream se descarta.
      pos = end;
      continue;
    }
    if (closing && name == "stream") {
      depth = 0;
      pos = end;
      continue;
    }
    if (closing) {
      pos = end;
      continue;
    }
    if (self_closing) {
      out.emplace_back(tag);
      pos = end;
      continue;
    }
    // Busca el cierre de este elemento con anidamiento.
    int nested = 0;
    std::size_t scan = end;
    std::size_t close_end = std::string::npos;
    while (scan < buffer.size()) {
      const auto next = buffer.find('<', scan);
      if (next == std::string::npos) break;
      if (buffer.compare(next, 4, "<!--") == 0) {
        const auto c = buffer.find("-->", next);
        if (c == std::string::npos) break;
        scan = c + 3;
        continue;
      }
      if (buffer.compare(next, 9, "<![CDATA[") == 0) {
        const auto c = buffer.find("]]>", next);
        if (c == std::string::npos) break;
        scan = c + 3;
        continue;
      }
      const auto e = tag_end(buffer, next);
      if (e == std::string::npos) break;
      const auto inner = std::string_view(buffer).substr(next, e - next);
      const bool inner_closing = inner.size() > 1 && inner[1] == '/';
      const bool inner_self = inner.size() > 1 && inner[inner.size() - 2] == '/';
      if (inner_closing) {
        if (nested == 0) {
          close_end = e;
          break;
        }
        --nested;
      } else if (!inner_self) {
        ++nested;
      }
      scan = e;
    }
    if (close_end == std::string::npos) break;
    out.emplace_back(buffer.substr(lt, close_end - lt));
    pos = close_end;
  }
  buffer.erase(0, pos);
  return out;
}

XmppClient::XmppClient(XmppConfig config) : config_(std::move(config)) {}

XmppClient::~XmppClient() { close(); }

bool XmppClient::is_connected() const { return connected_.load(); }

void XmppClient::set_message_callback(
    std::function<void(const XmppMessage&)> callback) {
  message_callback_ = std::move(callback);
}

bool XmppClient::fetch_token(std::string& err) {
  if (config_.token.empty() && config_.auth_url.empty()) {
    err = "no hay token ni endpoint de autenticación";
    return false;
  }
  if (!config_.token.empty()) return true;
  const auto body = encode_auth_token_request(config_.username,
                                              config_.password,
                                              config_.client_version);
  HttpOptions options;
  options.timeout_ms = config_.io_timeout_ms;
  options.headers.push_back("Content-Type: application/x-protobuf");
  const auto response = http_post(config_.auth_url, body, options);
  if (!response.ok()) {
    err = "auth/token falló: " +
          (response.error.empty() ? std::to_string(response.status) : response.error);
    return false;
  }
  std::string token;
  std::string parse_err;
  if (!decode_auth_token_response(response.body, token, parse_err)) {
    err = "respuesta de auth/token inválida: " + parse_err;
    return false;
  }
  config_.token = token;
  return true;
}

bool XmppClient::open_socket(std::string& err) {
  struct addrinfo hints {};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo* result = nullptr;
  const auto rc = getaddrinfo(config_.host.c_str(),
                              std::to_string(config_.port).c_str(), &hints,
                              &result);
  if (rc != 0 || result == nullptr) {
    err = "no se pudo resolver " + config_.host;
    return false;
  }
  int fd = -1;
  std::string last_error;
  for (auto* it = result; it != nullptr; it = it->ai_next) {
    fd = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
    if (fd < 0) continue;
    struct timeval tv {};
    tv.tv_sec = config_.io_timeout_ms / 1000;
    tv.tv_usec = (config_.io_timeout_ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    if (::connect(fd, it->ai_addr, it->ai_addrlen) == 0) break;
    last_error = std::strerror(errno);
    ::close(fd);
    fd = -1;
  }
  freeaddrinfo(result);
  if (fd < 0) {
    err = "conexión TCP falló: " + last_error;
    return false;
  }
  fd_ = fd;
  return true;
}

bool XmppClient::tls_handshake(std::string& err) {
  SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
  if (ctx == nullptr) {
    err = "no se pudo crear el contexto TLS";
    return false;
  }
  if (config_.verify_tls) {
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
    if (SSL_CTX_set_default_verify_paths(ctx) != 1) {
      err = "no se pudieron cargar las CA del sistema";
      SSL_CTX_free(ctx);
      return false;
    }
  } else {
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
  }
  SSL* ssl = SSL_new(ctx);
  if (ssl == nullptr) {
    err = "no se pudo crear SSL";
    SSL_CTX_free(ctx);
    return false;
  }
  SSL_set_fd(ssl, fd_);
  SSL_set_tlsext_host_name(ssl, config_.host.c_str());
  if (SSL_connect(ssl) != 1) {
    err = "handshake TLS falló";
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    return false;
  }
  if (config_.verify_tls) {
    const auto verify = SSL_get_verify_result(ssl);
    if (verify != X509_V_OK) {
      err = std::string("certificado TLS inválido: ") +
            X509_verify_cert_error_string(verify);
      SSL_free(ssl);
      SSL_CTX_free(ctx);
      return false;
    }
    X509* cert = SSL_get_peer_certificate(ssl);
    const bool host_ok =
        cert != nullptr && X509_check_host(cert, config_.host.c_str(),
                                           config_.host.size(), 0, nullptr) == 1;
    if (cert) X509_free(cert);
    if (!host_ok) {
      err = "el certificado no coincide con el host";
      SSL_free(ssl);
      SSL_CTX_free(ctx);
      return false;
    }
  }
  ssl_ctx_ = ctx;
  ssl_ = ssl;
  return true;
}

bool XmppClient::send_raw(std::string_view data, std::string& err) {
  std::lock_guard<std::mutex> lock(write_mutex_);
  if (ssl_ == nullptr) {
    err = "sin conexión TLS";
    return false;
  }
  std::size_t sent = 0;
  while (sent < data.size()) {
    const int n = SSL_write(static_cast<SSL*>(ssl_), data.data() + sent,
                            static_cast<int>(data.size() - sent));
    if (n <= 0) {
      err = "fallo escribiendo en el socket TLS";
      return false;
    }
    sent += static_cast<std::size_t>(n);
  }
  last_activity_ = now_ms();
  return true;
}

bool XmppClient::read_some(std::string& err, int timeout_ms) {
  if (ssl_ == nullptr) {
    err = "sin conexión TLS";
    return false;
  }
  struct timeval tv {};
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  char chunk[8192];
  const int n = SSL_read(static_cast<SSL*>(ssl_), chunk, sizeof chunk);
  if (n > 0) {
    buffer_.append(chunk, static_cast<std::size_t>(n));
    last_activity_ = now_ms();
    return true;
  }
  const int ssl_error = SSL_get_error(static_cast<SSL*>(ssl_), n);
  if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE) {
    err = "timeout";
    return false;
  }
  err = "el servidor cerró la conexión";
  return false;
}

bool XmppClient::start_stream(bool with_declaration, std::string& err) {
  std::string header;
  if (with_declaration) header += "<?xml version='1.0'?>";
  header += "<stream:stream to='" + xml_escape(config_.domain) +
            "' xmlns='" + kNsClient + "' xmlns:stream='" + kNsStream +
            "' version='1.0'>";
  buffer_.clear();
  return send_raw(header, err);
}

bool XmppClient::wait_features(std::string& features_raw, std::string& err) {
  const auto deadline = now_ms() + config_.io_timeout_ms;
  int depth = 1;
  while (now_ms() < deadline) {
    for (auto& stanza : extract_xmpp_stanzas(buffer_, depth)) {
      if (xml_local_name(stanza) == "features") {
        features_raw = stanza;
        return true;
      }
    }
    std::string read_err;
    if (!read_some(read_err, 1000) && read_err != "timeout") {
      err = read_err;
      return false;
    }
  }
  err = "tiempo agotado esperando <stream:features>";
  return false;
}

bool XmppClient::authenticate(std::string& err) {
  const std::string raw = std::string(1, '\0') + config_.username +
                          std::string(1, '\0') + config_.token;
  const auto payload = base64_encode(raw);
  if (!send_raw(std::string("<auth xmlns='") + kNsSasl +
                    "' mechanism='PLAIN'>" + payload + "</auth>",
                err))
    return false;

  const auto deadline = now_ms() + config_.io_timeout_ms;
  int depth = 1;
  while (now_ms() < deadline) {
    for (auto& stanza : extract_xmpp_stanzas(buffer_, depth)) {
      const auto name = xml_local_name(stanza);
      if (name == "success") return true;
      if (name == "failure") {
        err = "SASL PLAIN rechazado: " + stanza.substr(0, 200);
        return false;
      }
    }
    std::string read_err;
    if (!read_some(read_err, 1000) && read_err != "timeout") {
      err = read_err;
      return false;
    }
  }
  err = "tiempo agotado esperando el resultado SASL";
  return false;
}

bool XmppClient::bind_resource(std::string& err) {
  ++next_id_;
  const auto iq_id = "bind_" + std::to_string(next_id_);
  const std::string iq =
      std::string("<iq xmlns='") + kNsClient + "' type='set' id='" + iq_id +
      "'><bind xmlns='" + kNsBind + "'><resource>" +
      xml_escape(config_.resource) + "</resource></bind></iq>";
  if (!send_raw(iq, err)) return false;

  const auto deadline = now_ms() + config_.io_timeout_ms;
  int depth = 1;
  while (now_ms() < deadline) {
    for (auto& stanza : extract_xmpp_stanzas(buffer_, depth)) {
      if (xml_local_name(stanza) != "iq") continue;
      if (xml_attribute(stanza, "id") != iq_id) continue;
      if (xml_attribute(stanza, "type") == "error") {
        err = "el servidor rechazó el bind";
        return false;
      }
      const auto jid_pos = stanza.find("jid>");
      if (jid_pos != std::string::npos) {
        const auto start = jid_pos + 4;
        const auto end = stanza.find('<', start);
        jid_ = xml_unescape(stanza.substr(start, end == std::string::npos
                                                   ? std::string::npos
                                                   : end - start));
      }
      if (jid_.empty())
        jid_ = config_.username + "@" + config_.domain + "/" + config_.resource;
      return true;
    }
    std::string read_err;
    if (!read_some(read_err, 1000) && read_err != "timeout") {
      err = read_err;
      return false;
    }
  }
  err = "tiempo agotado esperando el bind";
  return false;
}

bool XmppClient::connect(std::string& err) {
  close();
  if (!fetch_token(err)) return false;
  if (!open_socket(err)) return false;
  if (!tls_handshake(err)) {
    close();
    return false;
  }
  if (!start_stream(true, err)) {
    close();
    return false;
  }
  std::string features;
  if (!wait_features(features, err)) {
    close();
    return false;
  }
  if (!authenticate(err)) {
    close();
    return false;
  }
  if (!start_stream(false, err)) {
    close();
    return false;
  }
  if (!wait_features(features, err)) {
    close();
    return false;
  }
  if (!bind_resource(err)) {
    close();
    return false;
  }
  // Sesión heredada: el servidor puede ignorarla.
  std::string ignored;
  send_raw(std::string("<session xmlns='") + kNsSession + "'/>", ignored);

  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    acked_.clear();
  }
  stop_.store(false);
  connected_.store(true);
  reader_ = std::thread([this] { reader_loop(); });
  spdlog::info("XMPP conectado como {}", jid_);
  return true;
}

bool XmppClient::ensure_connected(std::string& err) {
  if (connected_.load()) return true;
  spdlog::warn("XMPP desconectado; reintentando");
  return connect(err);
}

void XmppClient::close() {
  stop_.store(true);
  connected_.store(false);
  state_cv_.notify_all();
  if (reader_.joinable()) {
    if (reader_.get_id() == std::this_thread::get_id())
      reader_.detach();
    else
      reader_.join();
  }
  if (ssl_ != nullptr) {
    SSL_shutdown(static_cast<SSL*>(ssl_));
    SSL_free(static_cast<SSL*>(ssl_));
    ssl_ = nullptr;
  }
  if (ssl_ctx_ != nullptr) {
    SSL_CTX_free(static_cast<SSL_CTX*>(ssl_ctx_));
    ssl_ctx_ = nullptr;
  }
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

bool XmppClient::send_message(std::string_view to, std::string_view body,
                              std::string& message_id, std::string& err) {
  ++next_id_;
  message_id = "tds-" + std::to_string(next_id_) + "-" + std::to_string(now_ms());
  const std::string stanza =
      std::string("<message xmlns='") + kNsClient + "' type='chat' to='" +
      xml_escape(to) + "' id='" + message_id + "'><body>" +
      xml_escape(body) + "</body></message>";
  return send_raw(stanza, err);
}

bool XmppClient::wait_ack(std::string_view message_id, int timeout_ms,
                          std::string& err) {
  std::unique_lock<std::mutex> lock(state_mutex_);
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
  const auto ok = state_cv_.wait_until(lock, deadline, [&] {
    return acked_.count(std::string(message_id)) > 0 || !connected_.load();
  });
  if (acked_.count(std::string(message_id)) > 0) {
    acked_.erase(std::string(message_id));
    return true;
  }
  err = ok ? "conexión XMPP cerrada" : "sin tdack en el tiempo esperado";
  return false;
}

void XmppClient::fail_connection(const std::string& reason) {
  if (!connected_.exchange(false)) return;
  spdlog::warn("conexión XMPP perdida: {}", reason);
  state_cv_.notify_all();
}

void XmppClient::reader_loop() {
  int depth = 1;
  while (!stop_.load()) {
    std::string err;
    if (!read_some(err, 1000)) {
      if (err != "timeout") {
        fail_connection(err);
        break;
      }
      // Keepalive: espacio en blanco cada 60 s sin actividad.
      if (now_ms() - last_activity_ > 60000) {
        std::string ignored;
        if (!send_raw(" ", ignored)) {
          fail_connection(ignored);
          break;
        }
        last_activity_ = now_ms();
      }
      continue;
    }
    for (auto& raw : extract_xmpp_stanzas(buffer_, depth)) {
      Stanza stanza;
      stanza.raw = std::move(raw);
      handle_stanza(stanza);
    }
  }
}

void XmppClient::handle_stanza(const Stanza& stanza) {
  const auto name = xml_local_name(stanza.raw);
  if (name == "message") {
    XmppMessage message;
    message.raw = stanza.raw;
    message.id = xml_attribute(stanza.raw, "id");
    message.from = xml_attribute(stanza.raw, "from");
    message.to = xml_attribute(stanza.raw, "to");
    message.type = xml_attribute(stanza.raw, "type");
    // Cuerpo: <body> o su forma abreviada <b>.
    std::size_t pos = 0;
    while ((pos = stanza.raw.find('<', pos)) != std::string::npos) {
      const auto end = tag_end(stanza.raw, pos);
      if (end == std::string::npos) break;
      const auto tag = std::string_view(stanza.raw).substr(pos, end - pos);
      const auto local = xml_local_name(tag);
      if ((local == "body" || local == "b") && tag.size() > 1 &&
          tag[1] != '/') {
        const bool self_closing = tag[tag.size() - 2] == '/';
        if (!self_closing) {
          const auto close = stanza.raw.find("</", end);
          if (close != std::string::npos) {
            message.body = xml_unescape(
                std::string_view(stanza.raw).substr(end, close - end));
          }
        }
        break;
      }
      pos = end;
    }
    if (message_callback_) message_callback_(message);
    return;
  }
  if (name == "tdack") {
    const auto mi = xml_attribute(stanza.raw, "mi");
    if (!mi.empty()) {
      {
        std::lock_guard<std::mutex> lock(state_mutex_);
        acked_.insert(mi);
      }
      state_cv_.notify_all();
    }
    return;
  }
  if (name == "iq") {
    const auto type = xml_attribute(stanza.raw, "type");
    if (type == "get") {
      const auto id = xml_attribute(stanza.raw, "id");
      const auto from = xml_attribute(stanza.raw, "from");
      if (!id.empty()) {
        std::string ignored;
        send_raw(std::string("<iq xmlns='") + kNsClient + "' type='result' id='" +
                     xml_escape(id) + "'" +
                     (from.empty() ? "" : " to='" + xml_escape(from) + "'") + "/>",
                 ignored);
      }
    }
    return;
  }
  if (name == "stream") {
    fail_connection("stream cerrado por el servidor");
  }
}

}  // namespace tds
