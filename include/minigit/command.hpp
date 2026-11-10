#pragma once
#include "minigit/io.hpp"
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

namespace minigit {
using Arguments = std::vector<std::string>;
struct CommandContext {
    fs::path cwd;
    std::ostream &out;
};
class Command {
  public:
    virtual ~Command() = default;
    virtual void execute(const Arguments &args, CommandContext &context) = 0;
};
class CommandFactory {
  public:
    static std::unique_ptr<Command> create(const std::string &name);
};
class Application {
  public:
    int run(const Arguments &args, const fs::path &cwd, std::ostream &out, std::ostream &err) const;
};
} // namespace minigit
