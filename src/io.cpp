#include "minigit/io.hpp"
#include <atomic>
#include <chrono>
#include <fstream>
#include <iterator>
#include <limits>
#ifdef _WIN32
#include <windows.h>
#endif

namespace minigit {
bool pathExists(const fs::path &path) {
    return fs::exists(fs::symlink_status(path));
}
void checkNoSymlink(const fs::path &path) {
    fs::path part;
    for (const auto &element : fs::absolute(path).lexically_normal()) {
        part /= element;
        if (fs::is_symlink(fs::symlink_status(part)))
            throw Error("Symlinks are unsupported: " + part.string());
    }
}
std::string readFile(const fs::path &path) {
    checkNoSymlink(path);
    if (!fs::is_regular_file(path))
        throw Error("Not a regular file: " + path.string());
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw Error("Cannot read: " + path.string());
    std::string data{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (in.bad())
        throw Error("Read failed: " + path.string());
    return data;
}
void atomicWrite(const fs::path &path, std::string_view data) {
    checkNoSymlink(path);
    fs::create_directories(path.parent_path());
    static std::atomic<unsigned long> sequence{0};
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    auto temp = path;
    temp += ".tmp-" + std::to_string(tick) + "-" + std::to_string(sequence++);
    try {
        if (pathExists(temp))
            throw Error("Temporary file collision");
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out)
            throw Error("Cannot write: " + temp.string());
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        out.close();
        if (!out)
            throw Error("Write failed: " + temp.string());
#ifdef _WIN32
        if (!MoveFileExW(temp.c_str(), path.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw Error("Cannot replace: " + path.string());
#else
        fs::rename(temp, path);
#endif
    } catch (...) {
        std::error_code ec;
        fs::remove(temp, ec);
        throw;
    }
}
void validatePath(std::string_view path) {
    if (path.empty() || path.front() == '/' || path.back() == '/' || path.find('\\') != path.npos ||
        path.find(':') != path.npos)
        throw Error("Invalid repository path");
    for (unsigned char c : path)
        if (c < 32 || c == 127)
            throw Error("Control characters in path");
    std::size_t pos = 0, components = 0;
    while (pos < path.size()) {
        if (++components > 257)
            throw Error("Path exceeds 256 directory levels");
        auto end = path.find('/', pos);
        if (end == path.npos)
            end = path.size();
        auto part = path.substr(pos, end - pos);
        std::string lower(part);
        for (auto &c : lower)
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
        if (part.empty() || part == "." || part == ".." || lower == ".minigit" || lower == ".git" ||
            part.back() == '.' || part.back() == ' ')
            throw Error("Unsafe repository path: " + std::string(path));
        pos = end + 1;
    }
}
void appendU32(std::string &out, std::uint32_t value) {
    for (int i = 3; i >= 0; --i)
        out += static_cast<char>(value >> (8 * i));
}
std::uint32_t readU32(std::string_view in, std::size_t &pos) {
    if (pos > in.size() || in.size() - pos < 4)
        throw Error("Truncated index");
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i)
        value = (value << 8) | static_cast<unsigned char>(in[pos++]);
    return value;
}
RepoLock::RepoLock(const fs::path &metadata) : path_(metadata / "LOCK") {
    checkNoSymlink(path_);
    if (!fs::create_directory(path_))
        throw Error("Repository is locked; another writer may be active");
}
RepoLock::~RepoLock() {
    std::error_code ec;
    fs::remove(path_, ec);
}
} // namespace minigit
