// Codificación/decodificación mínima de protobuf para los dos mensajes que
// necesita la autenticación de ToDus:
//
//   ProtoUserAuthTokenRequest  { string username = 1; string password = 2;
//                                string version = 3; }
//   ProtoUserAuthTokenResponse { string tokenString = 2; }
//
// No se añade la dependencia de protobuf: el subconjunto es trivial y así se
// mantiene una compilación pequeña y reproducible.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace tds {

std::string encode_auth_token_request(std::string_view username,
                                      std::string_view password,
                                      std::string_view version);

// Devuelve false si el buffer no es protobuf válido del subconjunto esperado.
bool decode_auth_token_response(std::string_view body, std::string& token_out,
                                std::string& err);

}  // namespace tds
