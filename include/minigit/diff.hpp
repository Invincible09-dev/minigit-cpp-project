#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace minigit {
enum class EditKind { Equal, Insert, Delete };
struct Edit {
    EditKind kind;
    std::string line;
};
class Diff {
  public:
    static std::vector<Edit> lcs(std::string_view before, std::string_view after);
    static std::string format(const std::string &path, std::string_view before,
                              std::string_view after, bool oldExists = true, bool newExists = true);
};
} // namespace minigit
