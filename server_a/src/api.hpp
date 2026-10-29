#pragma once

#include "pipeline.hpp"
#include "tds/config.hpp"

namespace tds {

// Registra las rutas HTTP del Servidor A.
void register_server_a_routes(JobStore& store, const Config& config,
                              ServerAStats& stats);

}  // namespace tds
