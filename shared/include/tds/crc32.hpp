#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace tds {

std::uint32_t crc32(std::string_view data);

}  // namespace tds
