#pragma once

#include <spdlog/spdlog.h>

#include <string>

namespace tds {

// Inicializa spdlog con el nombre del servicio. Nivel por LOG_LEVEL.
void init_logging(const std::string& service_name);

// Enmascara secretos para logs (tokens, contraseñas).
std::string redact(std::string_view secret);

}  // namespace tds
