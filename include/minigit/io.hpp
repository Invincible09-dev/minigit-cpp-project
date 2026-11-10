#pragma once
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace minigit {
namespace fs = std::filesystem;
class Error : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};
std::string readFile(const fs::path &path);
void atomicWrite(const fs::path &path, std::string_view data);
void checkNoSymlink(const fs::path &path);
bool pathExists(const fs::path &path);
void validatePath(std::string_view path);
void appendU32(std::string &out, std::uint32_t value);
std::uint32_t readU32(std::string_view in, std::size_t &pos);
class RepoLock {
  public:
    explicit RepoLock(const fs::path &metadata);
    ~RepoLock();
    RepoLock(const RepoLock &) = delete;
    RepoLock &operator=(const RepoLock &) = delete;

  private:
    fs::path path_;
};
} // namespace minigit
