#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace tds {

std::string to_hex(const void* data, std::size_t size);
std::string to_hex(std::string_view data);
bool from_hex(std::string_view hex, std::string& out);

std::string base64_encode(std::string_view data);
bool base64_decode(std::string_view text, std::string& out);

}  // namespace tds
