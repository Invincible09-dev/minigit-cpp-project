#pragma once
#include <string>
#include <string_view>

namespace minigit {
std::string sha1(std::string_view bytes);
bool validOid(std::string_view oid);
std::string oidToRaw(std::string_view oid);
std::string rawToOid(std::string_view raw);
} // namespace minigit
