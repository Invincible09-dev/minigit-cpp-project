#pragma once
#include "minigit/io.hpp"
#include "minigit/object.hpp"

namespace minigit {
class ObjectStore {
  public:
    explicit ObjectStore(fs::path metadata) : directory_(std::move(metadata) / "objects") {}
    std::string write(const Object &object) const;
    std::unique_ptr<Object> read(const std::string &oid) const;
    fs::path path(const std::string &oid) const;

  private:
    fs::path directory_;
};
} // namespace minigit
