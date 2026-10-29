// Codificación directa byte -> carácter con una tabla fija de 256 símbolos.
// Ver docs/tabla-codificacion.md. No se generan cadenas binarias ni hex.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace tds {

inline constexpr int kEncodingVersion = 1;
inline constexpr std::size_t kSymbolCount = 256;

// SHA-256 de los 256 símbolos concatenados en UTF-8 (tabla v1).
std::string_view table_sha256();

// UTF-8 del símbolo asociado al byte `b`.
std::string_view symbol_for(std::uint8_t b);

// Devuelve el byte asociado a un code point, si pertenece a la tabla.
bool decode_symbol(char32_t cp, std::uint8_t& out);

// Añade a `out` los símbolos correspondientes a `bytes`.
void encode_append(std::span<const std::uint8_t> bytes, std::string& out);
std::string encode(std::span<const std::uint8_t> bytes);

// Decodifica texto UTF-8. Falla ante secuencias inválidas o símbolos ajenos.
bool decode_text(std::string_view utf8, std::string& bytes_out, std::string& err);

// Número de bytes decodificados y de bytes UTF-8 consumidos. Falla si el texto
// contiene algo que no sea de la tabla.
bool measure_encoded(std::string_view utf8, std::uint64_t& decoded_bytes,
                     std::uint64_t& utf8_bytes, std::string& err);

// Comprueba invariantes de la tabla (256 únicos, imprimibles, reversibles).
bool validate_table(std::string& err);

}  // namespace tds
