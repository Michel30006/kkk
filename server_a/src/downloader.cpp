#include "downloader.hpp"

#include <utility>

#include "tds/http.hpp"
#include "tds/log.hpp"

namespace tds {
namespace {

std::string resolve_entry_for(const UrlCheck& check) {
  const bool v6 = check.pinned_ip.find(':') != std::string::npos;
  return check.host + ":" + std::to_string(check.port) + ":" +
         (v6 ? "[" + check.pinned_ip + "]" : check.pinned_ip);
}

}  // namespace

DownloadOutcome download_guarded(std::string url, const UrlPolicy& policy,
                                 const DownloadLimits& limits,
                                 const std::string& temp_path) {
  DownloadOutcome outcome;
  for (int hop = 0; hop <= limits.max_redirects; ++hop) {
    const auto check = validate_url(url, policy);
    if (!check.ok) {
      outcome.error = "URL rechazada: " + check.error;
      return outcome;
    }
    DownloadOptions options;
    options.max_bytes = limits.max_bytes;
    options.connect_timeout_seconds = limits.connect_timeout_seconds;
    options.max_seconds = limits.max_seconds;
    options.temp_path = temp_path;
    options.resolve_entry = resolve_entry_for(check);
    const auto result = download_to_file(url, options);
    outcome.hops = hop;
    if (result.ok) {
      outcome.ok = true;
      outcome.path = temp_path;
      outcome.sha256 = result.sha256;
      outcome.size = result.size;
      outcome.content_type = result.content_type;
      spdlog::info("descarga OK size={} sha256={} content_type={}", result.size,
                   result.sha256, result.content_type);
      return outcome;
    }
    const bool redirect = result.status >= 300 && result.status < 400;
    if (redirect && !result.location.empty() && hop < limits.max_redirects) {
      const auto next = resolve_location(url, result.location);
      if (next.empty()) {
        outcome.error = "redirección con Location no resoluble";
        return outcome;
      }
      spdlog::info("redirección {} -> host nuevo, validando de nuevo", hop + 1);
      url = next;
      continue;
    }
    outcome.error = result.error.empty()
                        ? ("HTTP " + std::to_string(result.status))
                        : result.error;
    return outcome;
  }
  outcome.error = "demasiadas redirecciones";
  return outcome;
}

}  // namespace tds
