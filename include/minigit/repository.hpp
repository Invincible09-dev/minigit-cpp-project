#pragma once
#include "minigit/store.hpp"
#include <map>
#include <vector>

namespace minigit {
using Snapshot = std::map<std::string, std::string>;
struct Change {
    char kind;
    std::string path;
};
struct Status {
    std::vector<Change> staged, unstaged;
    std::vector<std::string> untracked;
};
// Facade: commands need no knowledge of object serialization or ref layout.
class Repository {
  public:
    static Repository init(const fs::path &root);
    static Repository discover(const fs::path &from = fs::current_path());
    explicit Repository(fs::path root);
    const fs::path &root() const { return root_; }
    std::string currentBranch() const;
    std::string head() const;
    Snapshot index() const;
    Snapshot snapshot(const std::string &revision = "HEAD") const;
    std::string resolve(const std::string &revision) const;
    void add(const std::vector<fs::path> &paths);
    void remove(const std::vector<fs::path> &paths);
    std::string commit(const std::string &message,
                       const std::string &author = "MiniGit User <user@local>",
                       std::int64_t timestamp = -1);
    Status status() const;
    void branch(const std::string &name);
    std::vector<std::string> branches() const;
    void checkout(const std::string &name);
    std::vector<std::pair<std::string, Commit>> log() const;
    std::string diff(bool staged = false) const;
    std::string diff(const std::string &before, const std::string &after) const;
    std::unique_ptr<Object> object(const std::string &oid) const { return store_.read(oid); }

  private:
    fs::path root_, meta_;
    ObjectStore store_;
    void saveIndex(const Snapshot &entries) const;
    Snapshot working() const;
    std::string relativePath(const fs::path &path) const;
    std::string writeTree(const Snapshot &entries) const;
    void readTree(const std::string &oid, const std::string &prefix, Snapshot &out,
                  std::size_t depth = 0) const;
    std::string blob(const std::string &oid) const;
    Commit readCommit(const std::string &oid) const;
    std::string diffSnapshots(const Snapshot &a, const Snapshot &b, bool workingTree) const;
};
} // namespace minigit
