#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace tds {

// Valida un identificador de trabajo / transferencia.
bool is_valid_id(std::string_view id);

std::string random_hex(std::size_t bytes);
std::string new_uuid();
std::string now_iso8601();
std::string now_iso8601(std::int64_t unix_seconds);
std::int64_t unix_now();

std::string trim(std::string_view text);
std::string to_lower(std::string_view text);
std::vector<std::string> split(std::string_view text, char separator);
bool starts_with(std::string_view text, std::string_view prefix);
bool ends_with(std::string_view text, std::string_view suffix);

std::string_view basename_of(std::string_view path);

// Convierte un destinatario (teléfono cubano de 8 dígitos, usuario de 10
// dígitos que empieza por 53, o JID ya formado) en JID de ToDus.
// Devuelve vacío si no es un destinatario válido.
std::string todus_jid(std::string_view recipient, std::string_view domain);

}  // namespace tds
