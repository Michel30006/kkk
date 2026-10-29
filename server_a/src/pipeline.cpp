#include "pipeline.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>
#include <vector>

#include "downloader.hpp"
#include "tds/codec.hpp"
#include "tds/encoding.hpp"
#include "tds/http.hpp"
#include "tds/log.hpp"
#include "tds/sha256.hpp"
#include "tds/util.hpp"

namespace tds {
namespace {

class TempFile {
 public:
  explicit TempFile(std::string path) : path_(std::move(path)) {}
  ~TempFile() {
    std::error_code ec;
    std::filesystem::remove(path_, ec);
  }
  const std::string& path() const { return path_; }

 private:
  std::string path_;
};

}  // namespace

EncodingPipeline::EncodingPipeline(JobStore& store, const Config& config,
                                   ServerAStats& stats)
    : store_(store), config_(config), stats_(stats) {}

void EncodingPipeline::run(const std::atomic<bool>& stop, int worker_index) {
  const std::string worker = "a-" + std::to_string(worker_index) + "-" + random_hex(4);
  spdlog::info("worker A {} iniciado", worker);
  while (!stop.load()) {
    JobRecord job;
    std::string err;
    if (store_.claim_job(worker, {"queued"}, "downloading", job, err)) {
      spdlog::info("job {} reclamado para descarga", job.job_id);
      process_job(job);
      continue;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(config_.worker_poll_ms));
  }
  spdlog::info("worker A {} detenido", worker);
}

bool EncodingPipeline::process_job(const JobRecord& job) {
  UrlPolicy policy;
  policy.allow_http = config_.allow_http;
  policy.allow_private = config_.allow_private_networks;
  if (!config_.allowed_host_suffixes.empty())
    policy.allowed_suffixes = split(config_.allowed_host_suffixes, ',');

  DownloadLimits limits;
  limits.max_bytes = config_.max_video_bytes;
  limits.connect_timeout_seconds = config_.download_connect_timeout_seconds;
  limits.max_seconds = config_.download_max_seconds;
  limits.max_redirects = config_.max_redirects;

  const TempFile temp("/tmp/tds-download-" + job.job_id + "-" + random_hex(4) + ".bin");
  auto outcome = download_guarded(job.source_url, policy, limits, temp.path());
  if (!outcome.ok) {
    spdlog::warn("job {} fallo de descarga: {}", job.job_id, outcome.error);
    std::string err;
    store_.transition(job.job_id, "downloading", "failed_download", "download_error",
                      outcome.error, err);
    stats_.jobs_failed.fetch_add(1);
    return false;
  }

  std::string err;
  if (!store_.transition(job.job_id, "downloading", "encoding", "", "", err)) {
    spdlog::error("job {} no pudo pasar a encoding: {}", job.job_id, err);
    return false;
  }

  if (!encode_file(job, outcome.path, outcome.size, outcome.sha256, err)) {
    spdlog::error("job {} fallo de codificación: {}", job.job_id, err);
    store_.set_status(job.job_id, "failed_encoding", "encoding_error", err, err);
    stats_.jobs_failed.fetch_add(1);
    return false;
  }
  stats_.jobs_encoded.fetch_add(1);
  stats_.bytes_encoded.fetch_add(outcome.size);
  spdlog::info("job {} listo para entrega ({} bytes)", job.job_id, outcome.size);
  notify_server_b(job.job_id);
  return true;
}

bool EncodingPipeline::encode_file(const JobRecord& job, const std::string& path,
                                   std::uint64_t expected_size,
                                   const std::string& expected_sha,
                                   std::string& err) {
  FILE* file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) {
    err = "no se puede leer el fichero descargado";
    return false;
  }
  struct Closer {
    FILE* f;
    ~Closer() {
      if (f) std::fclose(f);
    }
  } closer{file};

  const std::size_t block_size =
      static_cast<std::size_t>(std::max(4096, config_.encode_part_bytes));
  std::vector<std::uint8_t> block(block_size);
  Sha256 hash;
  std::uint64_t total = 0;
  int part_index = 0;
  std::vector<DataPart> batch;
  batch.reserve(8);

  auto flush = [&]() -> bool {
    if (batch.empty()) return true;
    if (!store_.append_parts(job.job_id, batch, err)) return false;
    batch.clear();
    return true;
  };

  while (true) {
    const auto n = std::fread(block.data(), 1, block.size(), file);
    if (n == 0) break;
    hash.update(block.data(), n);
    total += n;
    DataPart part;
    part.index = part_index++;
    part.payload = encode(std::span<const std::uint8_t>(block.data(), n));
    part.payload_bytes = static_cast<std::int64_t>(n);
    batch.push_back(std::move(part));
    if (batch.size() >= 8 && !flush()) return false;
    if (total > config_.max_video_bytes) {
      err = "el fichero supera MAX_VIDEO_BYTES";
      return false;
    }
  }
  if (std::ferror(file)) {
    err = "error leyendo el fichero descargado";
    return false;
  }
  const auto digest = hash.final_hex();
  if (total != expected_size || digest != expected_sha) {
    err = "el fichero cambió entre descarga y codificación";
    return false;
  }
  if (!flush()) return false;
  return store_.finish_encoding(job.job_id, static_cast<std::int64_t>(total),
                                digest, part_index, err);
}

void EncodingPipeline::notify_server_b(const std::string& job_id) {
  if (config_.server_b_url.empty()) return;
  HttpOptions options;
  options.timeout_ms = 5000;
  options.headers.push_back("Content-Type: application/json");
  options.headers.push_back("X-Internal-Token: " + config_.internal_token);
  const auto url = config_.server_b_url + "/jobs/" + job_id + "/deliver";
  const auto response = http_post(url, "{}", options);
  if (!response.ok()) {
    spdlog::info(
        "aviso a B no confirmado para {} ({}); B lo recogerá por sondeo",
        job_id, response.error.empty() ? std::to_string(response.status) : response.error);
  } else {
    spdlog::info("B avisado del job {}", job_id);
  }
}

}  // namespace tds
