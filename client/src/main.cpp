// Receptor de Ing Inversa.
//
// Modos:
//   * offline: lee un directorio con una trama por fichero (salida del
//     transporte mock o de un volcado de mensajes).
//   * XMPP: se conecta a ToDus con una cuenta propia y escucha las tramas
//     TDS1 dirigidas a ella.
//
// En ambos casos verifica tamaño y SHA-256 antes de escribir el fichero y,
// opcionalmente, confirma la reconstrucción al Servidor A.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "receiver_core.hpp"
#include "tds/config.hpp"
#include "tds/framing.hpp"
#include "tds/http.hpp"
#include "tds/log.hpp"
#include "tds/reassembler.hpp"
#include "tds/util.hpp"
#include "tds/xmpp.hpp"

namespace {

struct Options {
  std::string frames_dir;
  std::string out_path;
  std::string job_id;
  std::string transfer_id;
  std::string sha256;
  std::uint64_t size{0};
  int encoding_version{1};

  std::string server_a_url;
  std::string client_token;
  bool confirm{false};

  std::string xmpp_user;
  std::string xmpp_token;
  std::string xmpp_password;
  std::string xmpp_host{"im.todus.cu"};
  std::string xmpp_resource{"tds-receiver"};
  int xmpp_port{5222};
  bool xmpp_verify_tls{true};
  int wait_seconds{900};
};

void usage() {
  std::cout <<
      "Uso:\n"
      "  tds_receiver --frames DIR [opciones]\n"
      "  tds_receiver --xmpp-user 53XXXXXXXX --xmpp-token JWT "
      "--job-id ID --server-a URL --client-token TOKEN [opciones]\n"
      "\n"
      "Metadatos (si no se obtienen con --server-a):\n"
      "  --job-id ID --transfer-id ID --size BYTES --sha256 HEX "
      "[--version N]\n"
      "\n"
      "Opciones:\n"
      "  --out FILE            fichero de salida (por defecto <job_id>.bin)\n"
      "  --server-a URL        base del Servidor A para metadatos/confirmación\n"
      "  --client-token TOKEN  token del cliente devuelto al crear el trabajo\n"
      "  --confirm             confirma al Servidor A tras verificar\n"
      "  --wait SEGUNDOS       espera máxima en modo XMPP (por defecto 900)\n"
      "  --xmpp-host HOST      por defecto im.todus.cu\n"
      "  --xmpp-port N         por defecto 5222\n"
      "  --insecure-tls        no verifica el certificado (sólo pruebas)\n"
      "  --help                esta ayuda\n";
}

bool take_value(int argc, char** argv, int& i, std::string& out,
                std::string& err) {
  if (i + 1 >= argc) {
    err = std::string("falta el valor de ") + argv[i];
    return false;
  }
  out = argv[++i];
  return true;
}

bool parse_args(int argc, char** argv, Options& options, std::string& err) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      usage();
      std::exit(0);
    }
    if (arg == "--frames") {
      if (!take_value(argc, argv, i, options.frames_dir, err)) return false;
    } else if (arg == "--out") {
      if (!take_value(argc, argv, i, options.out_path, err)) return false;
    } else if (arg == "--job-id") {
      if (!take_value(argc, argv, i, options.job_id, err)) return false;
    } else if (arg == "--transfer-id") {
      if (!take_value(argc, argv, i, options.transfer_id, err)) return false;
    } else if (arg == "--sha256") {
      if (!take_value(argc, argv, i, options.sha256, err)) return false;
    } else if (arg == "--size") {
      std::string value;
      if (!take_value(argc, argv, i, value, err)) return false;
      try {
        options.size = std::stoull(value);
      } catch (...) {
        err = "--size no es numérico";
        return false;
      }
    } else if (arg == "--version") {
      std::string value;
      if (!take_value(argc, argv, i, value, err)) return false;
      options.encoding_version = std::atoi(value.c_str());
    } else if (arg == "--server-a") {
      if (!take_value(argc, argv, i, options.server_a_url, err)) return false;
    } else if (arg == "--client-token") {
      if (!take_value(argc, argv, i, options.client_token, err)) return false;
    } else if (arg == "--confirm") {
      options.confirm = true;
    } else if (arg == "--xmpp-user") {
      if (!take_value(argc, argv, i, options.xmpp_user, err)) return false;
    } else if (arg == "--xmpp-token") {
      if (!take_value(argc, argv, i, options.xmpp_token, err)) return false;
    } else if (arg == "--xmpp-password") {
      if (!take_value(argc, argv, i, options.xmpp_password, err)) return false;
    } else if (arg == "--xmpp-host") {
      if (!take_value(argc, argv, i, options.xmpp_host, err)) return false;
    } else if (arg == "--xmpp-port") {
      std::string value;
      if (!take_value(argc, argv, i, value, err)) return false;
      options.xmpp_port = std::atoi(value.c_str());
    } else if (arg == "--xmpp-resource") {
      if (!take_value(argc, argv, i, options.xmpp_resource, err)) return false;
    } else if (arg == "--wait") {
      std::string value;
      if (!take_value(argc, argv, i, value, err)) return false;
      options.wait_seconds = std::atoi(value.c_str());
    } else if (arg == "--insecure-tls") {
      options.xmpp_verify_tls = false;
    } else {
      err = "opción desconocida: " + arg;
      return false;
    }
  }

  if (options.frames_dir.empty() && options.xmpp_user.empty()) {
    err = "hace falta --frames DIR o --xmpp-user";
    return false;
  }
  if (!options.server_a_url.empty() && options.job_id.empty()) {
    err = "--server-a requiere --job-id";
    return false;
  }
  if (!options.server_a_url.empty() && options.client_token.empty()) {
    err = "--server-a requiere --client-token para leer los metadatos";
    return false;
  }
  if (options.confirm && (options.server_a_url.empty() ||
                          options.client_token.empty())) {
    err = "--confirm requiere --server-a y --client-token";
    return false;
  }
  if (options.encoding_version < 1 || options.encoding_version > 65535) {
    err = "--version fuera de rango";
    return false;
  }
  return true;
}

bool fetch_metadata(const Options& options, tds::JobMeta& meta,
                    std::string& err) {
  tds::HttpOptions http;
  http.timeout_ms = 15000;
  http.headers.push_back("X-Client-Token: " + options.client_token);
  const auto url = options.server_a_url + "/jobs/" + options.job_id;
  const auto response = tds::http_get(url, http);
  if (!response.ok()) {
    err = "no se pudieron obtener metadatos: " +
          (response.error.empty() ? "HTTP " + std::to_string(response.status)
                                  : response.error);
    return false;
  }
  return tds::parse_job_meta_json(response.body, meta, err);
}

bool confirm_job(const Options& options, const tds::JobMeta& meta,
                 std::string& err) {
  tds::HttpOptions http;
  http.timeout_ms = 15000;
  http.headers.push_back("Content-Type: application/json");
  http.headers.push_back("X-Client-Token: " + options.client_token);
  nlohmann::json body{{"sha256", meta.sha256}, {"size", meta.size_bytes}};
  const auto url = options.server_a_url + "/jobs/" + meta.job_id + "/confirm";
  const auto response = tds::http_post(url, body.dump(), http);
  if (!response.ok()) {
    err = "confirmación rechazada: " +
          (response.error.empty() ? "HTTP " + std::to_string(response.status)
                                  : response.error);
    return false;
  }
  return true;
}

// Escucha XMPP y agrupa las tramas del trabajo indicado.
bool receive_xmpp(const Options& options, const tds::JobMeta& meta,
                  std::vector<std::string>& wires, std::string& err) {
  tds::XmppConfig config;
  config.host = options.xmpp_host;
  config.domain = options.xmpp_host;
  config.port = options.xmpp_port;
  config.username = options.xmpp_user;
  config.token = options.xmpp_token;
  config.password = options.xmpp_password;
  config.resource = options.xmpp_resource;
  config.verify_tls = options.xmpp_verify_tls;

  tds::XmppClient client(config);
  std::mutex mutex;
  std::condition_variable cv;
  std::map<std::uint32_t, std::string> frames;
  std::uint32_t total = meta.total_chunks;
  bool done = false;
  std::atomic<std::uint64_t> rejected{0};

  client.set_message_callback([&](const tds::XmppMessage& message) {
    if (message.body.rfind("TDS1|", 0) != 0) return;
    tds::Frame frame;
    std::string parse_err;
    if (!tds::parse_frame(message.body, frame, parse_err)) {
      rejected.fetch_add(1);
      return;
    }
    if (frame.job_id != meta.job_id) return;
    if (frame.encoding_version != meta.encoding_version) return;
    std::lock_guard<std::mutex> lock(mutex);
    if (total == 0) total = frame.total_chunks;
    if (frame.total_chunks != total) {
      rejected.fetch_add(1);
      return;
    }
    frames.emplace(frame.chunk_index, message.body);
    if (frames.size() == total && !done) {
      done = true;
      cv.notify_all();
    }
  });

  std::cout << "Conectando a ToDus como " << options.xmpp_user << "...\n";
  if (!client.connect(err)) return false;
  std::cout << "Escuchando tramas del trabajo " << meta.job_id
            << " (Ctrl-C para salir)...\n";

  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(options.wait_seconds);
  {
    std::unique_lock<std::mutex> lock(mutex);
    while (!done) {
      if (std::chrono::steady_clock::now() >= deadline) break;
      cv.wait_for(lock, std::chrono::milliseconds(500));
      if (!client.is_connected()) break;
    }
  }

  {
    std::lock_guard<std::mutex> lock(mutex);
    if (total > 0)
      std::cout << "Tramas recibidas: " << frames.size() << "/" << total << "\n";
    if (!done) {
      err = "tiempo agotado o conexión cerrada antes de completar";
      if (rejected.load() > 0)
        err += " (tramas rechazadas: " + std::to_string(rejected.load()) + ")";
      // Se conserva lo recibido para que reassemble_frames informe de huecos.
      if (frames.empty()) {
        client.close();
        return false;
      }
    }
    wires.clear();
    wires.reserve(frames.size());
    for (auto& [index, wire] : frames) wires.push_back(std::move(wire));
  }
  client.close();
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  tds::init_logging("tds_receiver");

  Options options;
  std::string err;
  if (!parse_args(argc, argv, options, err)) {
    std::cerr << "Error: " << err << "\n\n";
    usage();
    return 1;
  }

  tds::JobMeta meta;
  bool have_meta = false;
  if (!options.server_a_url.empty()) {
    if (!fetch_metadata(options, meta, err)) {
      std::cerr << "Error: " << err << "\n";
      return 2;
    }
    have_meta = true;
  } else if (!options.job_id.empty()) {
    meta.job_id = options.job_id;
    meta.transfer_id = options.transfer_id;
    meta.size_bytes = options.size;
    meta.sha256 = options.sha256;
    meta.encoding_version = options.encoding_version;
    if (meta.transfer_id.empty() || meta.size_bytes == 0 ||
        meta.sha256.size() != 64) {
      std::cerr << "Error: faltan metadatos (--transfer-id, --size y "
                   "--sha256) o use --server-a\n";
      return 1;
    }
    have_meta = true;
  }

  if (!have_meta) {
    std::cerr << "Error: sin metadatos no se puede verificar el fichero. "
                 "Use --server-a o los parámetros de metadatos.\n";
    return 1;
  }

  if (options.out_path.empty()) options.out_path = meta.job_id + ".bin";

  std::vector<std::string> wires;
  if (!options.frames_dir.empty()) {
    if (!tds::load_frames_dir(options.frames_dir, wires, err)) {
      std::cerr << "Error: " << err << "\n";
      return 2;
    }
    std::cout << "Tramas leídas de " << options.frames_dir << ": "
              << wires.size() << "\n";
  } else {
    if (!receive_xmpp(options, meta, wires, err)) {
      std::cerr << "Error: " << err << "\n";
      return 2;
    }
    if (wires.empty()) {
      std::cerr << "Error: " << err << "\n";
      return 2;
    }
  }

  const auto result = tds::reassemble_frames(wires, meta, options.out_path);
  if (!result.ok) {
    std::cerr << "RECONSTRUCCIÓN FALLIDA: " << result.error << "\n";
    return 3;
  }

  std::cout << "RECONSTRUCCIÓN VERIFICADA\n"
            << "  fichero: " << result.out_path << "\n"
            << "  tamaño:  " << result.size << " bytes\n"
            << "  sha256:  " << result.sha256 << "\n";

  if (options.confirm) {
    if (!confirm_job(options, meta, err)) {
      std::cerr << "Error al confirmar: " << err << "\n";
      return 4;
    }
    std::cout << "Confirmación enviada al Servidor A: trabajo completado.\n";
  } else {
    std::cout << "No se confirmó al Servidor A (use --confirm para hacerlo).\n";
  }
  return 0;
}
