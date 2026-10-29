#include "tds/log.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <cstdlib>
#include <string>

namespace tds {

void init_logging(const std::string& service_name) {
  auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  auto logger = std::make_shared<spdlog::logger>(service_name, sink);
  logger->set_pattern("%Y-%m-%dT%H:%M:%S.%eZ %n [%l] %v");
  const char* level = std::getenv("LOG_LEVEL");
  logger->set_level(spdlog::level::from_str(level != nullptr ? level : "info"));
  logger->flush_on(spdlog::level::info);
  spdlog::set_default_logger(logger);
}

std::string redact(std::string_view secret) {
  if (secret.empty()) return "<vacío>";
  return "<oculto:" + std::to_string(secret.size()) + ">";
}

}  // namespace tds
