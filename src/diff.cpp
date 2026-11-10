#include "minigit/diff.hpp"
#include "minigit/io.hpp"
#include <algorithm>
#include <cstdint>
#include <sstream>

namespace minigit {
namespace {
std::vector<std::string> lines(std::string_view data) {
    std::vector<std::string> out;
    for (std::size_t pos = 0; pos < data.size();) {
        auto end = data.find('\n', pos);
        end = end == data.npos ? data.size() : end + 1;
        out.emplace_back(data.substr(pos, end - pos));
        pos = end;
    }
    return out;
}
} // namespace
std::vector<Edit> Diff::lcs(std::string_view before, std::string_view after) {
    auto a = lines(before), b = lines(after);
    const auto n = a.size(), m = b.size(), cols = m + 1;
    constexpr std::size_t maxCells = 16000000;
    if (n + 1 > maxCells / cols)
        throw Error("Diff exceeds 16 million LCS cells; split the file");
    std::vector<std::uint32_t> dp((n + 1) * cols);
    auto cell = [&](std::size_t i, std::size_t j) -> std::uint32_t & { return dp[i * cols + j]; };
    for (std::size_t i = n; i-- > 0;)
        for (std::size_t j = m; j-- > 0;)
            cell(i, j) =
                a[i] == b[j] ? 1 + cell(i + 1, j + 1) : std::max(cell(i + 1, j), cell(i, j + 1));
    std::vector<Edit> out;
    std::size_t i = 0, j = 0;
    while (i < n || j < m) {
        if (i < n && j < m && a[i] == b[j]) {
            out.push_back({EditKind::Equal, a[i]});
            ++i;
            ++j;
        } else if (i < n && (j == m || cell(i + 1, j) >= cell(i, j + 1)))
            out.push_back({EditKind::Delete, a[i++]});
        else
            out.push_back({EditKind::Insert, b[j++]});
    }
    return out;
}
std::string Diff::format(const std::string &path, std::string_view before, std::string_view after,
                         bool oldExists, bool newExists) {
    if (before == after && oldExists == newExists)
        return {};
    std::ostringstream out;
    out << "diff --minigit a/" << path << " b/" << path << '\n';
    if (!oldExists)
        out << "new file mode 100644\n";
    if (!newExists)
        out << "deleted file mode 100644\n";
    out << "--- " << (oldExists ? "a/" + path : "/dev/null") << "\n+++ "
        << (newExists ? "b/" + path : "/dev/null") << '\n';
    if (before.find('\0') != before.npos || after.find('\0') != after.npos) {
        out << "Binary files differ\n";
        return out.str();
    }
    const auto edits = lcs(before, after);
    const auto n = lines(before).size(), m = lines(after).size();
    if (!edits.empty())
        out << "@@ -" << (n ? 1 : 0) << ',' << n << " +" << (m ? 1 : 0) << ',' << m << " @@\n";
    for (const auto &e : edits) {
        out << (e.kind == EditKind::Equal ? ' ' : e.kind == EditKind::Delete ? '-' : '+') << e.line;
        if (e.line.empty() || e.line.back() != '\n')
            out << "\n\\ No newline at end of file\n";
    }
    return out.str();
}
} // namespace minigit
