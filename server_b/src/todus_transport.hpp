#pragma once

#include <memory>
#include <string>

#include "tds/config.hpp"
#include "tds/transport.hpp"

namespace tds {

// Crea el transporte según TRANSPORT. Con "todus" devuelve un transporte que
// habla XMPP contra im.todus.cu; con "mock" escribe en MOCK_OUTBOX.
std::unique_ptr<Transport> make_transport(const Config& config,
                                          std::string& err);

// Descripción del transporte sin secretos.
std::string describe_transport(const Config& config);

}  // namespace tds
