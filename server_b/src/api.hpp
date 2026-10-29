#pragma once

#include "tds/config.hpp"
#include "tds/storage.hpp"
#include "worker.hpp"

namespace tds {

void register_server_b_routes(JobStore& store, const Config& config,
                              ServerBStats& stats, DeliveryWorker& worker,
                              Transport& transport);

}  // namespace tds
