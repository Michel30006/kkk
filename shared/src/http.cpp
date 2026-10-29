#include "tds/http.hpp"

#include <curl/curl.h>

#include <cstdio>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <vector>

#include "tds/sha256.hpp"

namespace tds {
namespace {

void global_init() {
  static std::once_flag once;
  std::call_once(once, [] {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
      throw std::runtime_error("curl_global_init falló");
  });
}

struct WriteBuffer {
  std::string* out;
  std::size_t limit;
  bool overflow{false};
};

std::size_t write_string(char* ptr, std::size_t size, std::size_t nmemb,
                         void* userdata) {
  auto* buf = static_cast<WriteBuffer*>(userdata);
  const auto total = size * nmemb;
  if (buf->out->size() + total > buf->limit) {
    buf->overflow = true;
    return 0;  // aborta la transferencia
  }
  buf->out->append(ptr, total);
  return total;
}

std::size_t capture_location(char* ptr, std::size_t size, std::size_t nmemb,
                             void* userdata) {
  auto* location = static_cast<std::string*>(userdata);
  const auto total = size * nmemb;
  std::string line(ptr, total);
  const auto colon = line.find(':');
  if (colon != std::string::npos) {
    auto name = line.substr(0, colon);
    for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (name == "location") {
      auto value = line.substr(colon + 1);
      while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' '))
        value.pop_back();
      std::size_t start = 0;
      while (start < value.size() && value[start] == ' ') ++start;
      *location = value.substr(start);
    }
  }
  return total;
}

struct CurlHandle {
  CURL* handle{curl_easy_init()};
  CurlHandle() = default;
  ~CurlHandle() {
    if (handle) curl_easy_cleanup(handle);
  }
  CurlHandle(const CurlHandle&) = delete;
  CurlHandle& operator=(const CurlHandle&) = delete;
};

void set_common(CURL* h, const std::string& url, int timeout_ms,
                const std::string& user_agent) {
  curl_easy_setopt(h, CURLOPT_URL, url.c_str());
  curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 0L);
  curl_easy_setopt(h, CURLOPT_TIMEOUT_MS, static_cast<long>(timeout_ms));
  curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT_MS,
                   static_cast<long>(timeout_ms));
  curl_easy_setopt(h, CURLOPT_USERAGENT, user_agent.c_str());
  curl_easy_setopt(h, CURLOPT_PROTOCOLS_STR, "http,https");
  curl_easy_setopt(h, CURLOPT_ACCEPT_ENCODING, "");
}

HttpResponse request(std::string_view url, std::string_view body,
                     bool is_post, const HttpOptions& options) {
  global_init();
  if (url.size() > 4096) {
    HttpResponse r;
    r.error = "URL demasiado larga";
    return r;
  }
  CurlHandle c;
  HttpResponse response;
  if (c.handle == nullptr) {
    response.error = "no se pudo crear el cliente HTTP";
    return response;
  }
  set_common(c.handle, std::string(url), options.timeout_ms, options.user_agent);
  WriteBuffer buffer{&response.body, 8ull * 1024 * 1024, false};
  curl_easy_setopt(c.handle, CURLOPT_WRITEFUNCTION, write_string);
  curl_easy_setopt(c.handle, CURLOPT_WRITEDATA, &buffer);
  curl_slist* headers = nullptr;
  for (const auto& h : options.headers)
    headers = curl_slist_append(headers, h.c_str());
  if (headers) curl_easy_setopt(c.handle, CURLOPT_HTTPHEADER, headers);
  if (is_post) {
    curl_easy_setopt(c.handle, CURLOPT_POST, 1L);
    curl_easy_setopt(c.handle, CURLOPT_POSTFIELDS, body.data());
    curl_easy_setopt(c.handle, CURLOPT_POSTFIELDSIZE_LARGE,
                     static_cast<curl_off_t>(body.size()));
  }
  const auto rc = curl_easy_perform(c.handle);
  long status = 0;
  curl_easy_getinfo(c.handle, CURLINFO_RESPONSE_CODE, &status);
  response.status = status;
  if (rc != CURLE_OK) {
    response.error = buffer.overflow ? "respuesta HTTP demasiado grande"
                                     : curl_easy_strerror(rc);
  }
  if (headers) curl_slist_free_all(headers);
  return response;
}

struct DownloadSink {
  FILE* file{nullptr};
  Sha256* hash{nullptr};
  std::uint64_t written{0};
  std::uint64_t limit{0};
  bool too_large{false};
  bool io_error{false};
};

std::size_t write_file(char* ptr, std::size_t size, std::size_t nmemb,
                       void* userdata) {
  auto* sink = static_cast<DownloadSink*>(userdata);
  const auto total = size * nmemb;
  if (sink->written + total > sink->limit) {
    sink->too_large = true;
    return 0;
  }
  if (std::fwrite(ptr, 1, total, sink->file) != total) {
    sink->io_error = true;
    return 0;
  }
  sink->hash->update(ptr, total);
  sink->written += total;
  return total;
}

}  // namespace

HttpResponse http_get(std::string_view url, const HttpOptions& options) {
  return request(url, {}, false, options);
}

HttpResponse http_post(std::string_view url, std::string_view body,
                       const HttpOptions& options) {
  return request(url, body, true, options);
}

DownloadResult download_to_file(std::string_view url,
                                const DownloadOptions& options) {
  global_init();
  DownloadResult result;
  CurlHandle c;
  if (c.handle == nullptr) {
    result.error = "no se pudo crear el cliente HTTP";
    return result;
  }
  FILE* file = std::fopen(options.temp_path.c_str(), "wb");
  if (file == nullptr) {
    result.error = "no se puede crear el fichero temporal";
    return result;
  }
  Sha256 hash;
  DownloadSink sink{file, &hash, 0, options.max_bytes, false, false};
  set_common(c.handle, std::string(url),
             options.max_seconds * 1000, options.user_agent);
  curl_easy_setopt(c.handle, CURLOPT_CONNECTTIMEOUT,
                   static_cast<long>(options.connect_timeout_seconds));
  curl_easy_setopt(c.handle, CURLOPT_LOW_SPEED_LIMIT, 1L);
  curl_easy_setopt(c.handle, CURLOPT_LOW_SPEED_TIME, 30L);
  curl_easy_setopt(c.handle, CURLOPT_WRITEFUNCTION, write_file);
  curl_easy_setopt(c.handle, CURLOPT_WRITEDATA, &sink);
  curl_easy_setopt(c.handle, CURLOPT_HEADERFUNCTION, capture_location);
  curl_easy_setopt(c.handle, CURLOPT_HEADERDATA, &result.location);
  curl_slist* resolve = nullptr;
  if (!options.resolve_entry.empty()) {
    resolve = curl_slist_append(resolve, options.resolve_entry.c_str());
    curl_easy_setopt(c.handle, CURLOPT_RESOLVE, resolve);
  }
  char content_type[CURL_ERROR_SIZE] = {0};
  curl_easy_setopt(c.handle, CURLOPT_ERRORBUFFER, content_type);
  const auto rc = curl_easy_perform(c.handle);
  if (resolve) curl_slist_free_all(resolve);
  long status = 0;
  curl_easy_getinfo(c.handle, CURLINFO_RESPONSE_CODE, &status);
  char* type = nullptr;
  curl_easy_getinfo(c.handle, CURLINFO_CONTENT_TYPE, &type);
  if (type) result.content_type = type;
  const bool closed = (std::fclose(file) == 0);
  if (sink.too_large) {
    result.error = "el fichero supera MAX_VIDEO_BYTES";
    std::remove(options.temp_path.c_str());
    return result;
  }
  if (sink.io_error || !closed) {
    result.error = "error escribiendo el fichero temporal";
    std::remove(options.temp_path.c_str());
    return result;
  }
  if (rc != CURLE_OK) {
    result.error = (content_type[0] != '\0') ? content_type : curl_easy_strerror(rc);
    std::remove(options.temp_path.c_str());
    return result;
  }
  result.status = status;
  if (status >= 300 && status < 400) {
    result.ok = false;
    if (result.location.empty()) result.error = "redirección sin Location";
    std::remove(options.temp_path.c_str());
    return result;
  }
  if (status < 200 || status >= 300) {
    result.error = "HTTP " + std::to_string(status);
    std::remove(options.temp_path.c_str());
    return result;
  }
  result.size = sink.written;
  result.sha256 = hash.final_hex();
  result.ok = true;
  return result;
}

}  // namespace tds
