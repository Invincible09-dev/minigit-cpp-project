#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace minigit {
class Object {
  public:
    virtual ~Object() = default;
    virtual std::string type() const = 0;
    virtual std::string serialize() const = 0;
    std::string encoded() const;
    std::string id() const;
};
class Blob final : public Object {
  public:
    explicit Blob(std::string bytes) : data(std::move(bytes)) {}
    std::string type() const override { return "blob"; }
    std::string serialize() const override { return data; }
    std::string data;
};
struct TreeEntry {
    std::string name;
    std::string oid;
    bool directory = false;
};
class Tree final : public Object {
  public:
    explicit Tree(std::vector<TreeEntry> entries = {});
    std::string type() const override { return "tree"; }
    std::string serialize() const override;
    const std::vector<TreeEntry> &entries() const { return entries_; }

  private:
    std::vector<TreeEntry> entries_;
};
class Commit final : public Object {
  public:
    Commit(std::string tree, std::string parent, std::string author, std::int64_t timestamp,
           std::string message);
    std::string type() const override { return "commit"; }
    std::string serialize() const override;
    std::string tree, parent, author, message;
    std::int64_t timestamp;
};
class ObjectFactory {
  public:
    static std::unique_ptr<Object> decode(std::string_view encoded);
};
} // namespace minigit
