#include "minigit/command.hpp"
#include "minigit/repository.hpp"
#include <functional>
#include <map>
#include <ostream>

namespace minigit {
namespace {
void require(bool condition, const std::string &usage) {
    if (!condition)
        throw Error("Usage: minigit " + usage);
}
Repository repo(const CommandContext &c) {
    return Repository::discover(c.cwd);
}
std::vector<fs::path> paths(const Arguments &args, const CommandContext &c) {
    std::vector<fs::path> out;
    for (const auto &arg : args)
        out.push_back(fs::absolute(c.cwd / arg).lexically_normal());
    return out;
}
class InitCommand final : public Command {
    void execute(const Arguments &a, CommandContext &c) override {
        require(a.size() <= 1, "init [directory]");
        auto r = Repository::init(a.empty() ? c.cwd : c.cwd / a[0]);
        c.out << "Initialized MiniGit repository in " << (r.root() / ".minigit").string() << '\n';
    }
};
class AddCommand final : public Command {
    void execute(const Arguments &a, CommandContext &c) override {
        require(!a.empty(), "add <path>...");
        repo(c).add(paths(a, c));
    }
};
class RemoveCommand final : public Command {
    void execute(const Arguments &a, CommandContext &c) override {
        require(!a.empty(), "rm <path>...");
        repo(c).remove(paths(a, c));
    }
};
class CommitCommand final : public Command {
    void execute(const Arguments &a, CommandContext &c) override {
        std::string message, author = "MiniGit User <user@local>";
        bool messageSeen = false, authorSeen = false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (a[i] == "-m" && !messageSeen && i + 1 < a.size()) {
                message = a[++i];
                messageSeen = true;
            } else if (a[i] == "--author" && !authorSeen && i + 1 < a.size()) {
                author = a[++i];
                authorSeen = true;
            } else
                throw Error("Usage: minigit commit -m <message> [--author <identity>]");
        }
        require(messageSeen, "commit -m <message> [--author <identity>]");
        auto r = repo(c);
        auto oid = r.commit(message, author);
        c.out << '[' << r.currentBranch() << ' ' << oid.substr(0, 7) << "] " << message << '\n';
    }
};
class StatusCommand final : public Command {
    void execute(const Arguments &a, CommandContext &c) override {
        require(a.empty(), "status");
        auto r = repo(c);
        auto s = r.status();
        c.out << "On branch " << r.currentBranch() << '\n';
        if (r.head().empty())
            c.out << "No commits yet\n";
        if (!s.staged.empty()) {
            c.out << "Changes staged for commit:\n";
            for (const auto &change : s.staged)
                c.out << "  " << change.kind << "  " << change.path << '\n';
        }
        if (!s.unstaged.empty()) {
            c.out << "Changes not staged:\n";
            for (const auto &change : s.unstaged)
                c.out << "  " << change.kind << "  " << change.path << '\n';
        }
        if (!s.untracked.empty()) {
            c.out << "Untracked files:\n";
            for (const auto &path : s.untracked)
                c.out << "  ?  " << path << '\n';
        }
        if (s.staged.empty() && s.unstaged.empty() && s.untracked.empty())
            c.out << "Working tree clean\n";
    }
};
class LogCommand final : public Command {
    void execute(const Arguments &a, CommandContext &c) override {
        require(a.empty(), "log");
        for (const auto &[oid, value] : repo(c).log())
            c.out << "commit " << oid << "\nAuthor: " << value.author
                  << "\nDate: " << value.timestamp << " +0000\n\n    " << value.message << "\n\n";
    }
};
class BranchCommand final : public Command {
    void execute(const Arguments &a, CommandContext &c) override {
        require(a.size() <= 1, "branch [name]");
        auto r = repo(c);
        if (!a.empty())
            r.branch(a[0]);
        else
            for (const auto &name : r.branches())
                c.out << (name == r.currentBranch() ? "* " : "  ") << name << '\n';
    }
};
class CheckoutCommand final : public Command {
    void execute(const Arguments &a, CommandContext &c) override {
        require(a.size() == 1, "checkout <branch>");
        repo(c).checkout(a[0]);
        c.out << "Switched to branch '" << a[0] << "'\n";
    }
};
class DiffCommand final : public Command {
    void execute(const Arguments &a, CommandContext &c) override {
        auto r = repo(c);
        if (a.empty())
            c.out << r.diff();
        else if (a.size() == 1 && a[0] == "--staged")
            c.out << r.diff(true);
        else if (a.size() == 2)
            c.out << r.diff(a[0], a[1]);
        else
            throw Error("Usage: minigit diff [--staged | <revision> <revision>]");
    }
};
class CatFileCommand final : public Command {
    void execute(const Arguments &a, CommandContext &c) override {
        require(a.size() == 2 && (a[0] == "-t" || a[0] == "-p"),
                "cat-file <-t|-p> <full-object-id>");
        auto value = repo(c).object(a[1]);
        if (a[0] == "-t")
            c.out << value->type() << '\n';
        else if (auto tree = dynamic_cast<Tree *>(value.get())) {
            for (const auto &e : tree->entries())
                c.out << (e.directory ? "040000 tree " : "100644 blob ") << e.oid << '\t' << e.name
                      << '\n';
        } else {
            auto data = value->serialize();
            c.out.write(data.data(), static_cast<std::streamsize>(data.size()));
        }
    }
};
class HashObjectCommand final : public Command {
    void execute(const Arguments &a, CommandContext &c) override {
        require(a.size() == 1, "hash-object <file>");
        c.out << Blob(readFile(c.cwd / a[0])).id() << '\n';
    }
};
class HelpCommand final : public Command {
    void execute(const Arguments &a, CommandContext &c) override {
        require(a.empty(), "help");
        c.out << "MiniGit - C++17 educational version control\n\n"
              << "  init [directory]                 Create a repository\n"
              << "  add <path>...                    Stage files, directories and deletions\n"
              << "  rm <path>...                     Stage removal; KEEP working files\n"
              << "  commit -m <message> [--author <identity>]\n"
              << "  status                           Show staged, unstaged and untracked files\n"
              << "  log                              Show current branch history\n"
              << "  branch [name]                    List or create branches\n"
              << "  checkout <branch>                Switch branch (clean tracked files required)\n"
              << "  diff                             Compare index with working tree\n"
              << "  diff --staged                    Compare HEAD with index\n"
              << "  diff <revision> <revision>       Compare branches or full commit IDs\n"
              << "  cat-file <-t|-p> <object-id>      Inspect an object\n"
              << "  hash-object <file>               Compute a blob ID without storing it\n";
    }
};
} // namespace
std::unique_ptr<Command> CommandFactory::create(const std::string &name) {
    using Creator = std::function<std::unique_ptr<Command>()>;
    static const std::map<std::string, Creator> commands{
        {"init", [] { return std::make_unique<InitCommand>(); }},
        {"add", [] { return std::make_unique<AddCommand>(); }},
        {"rm", [] { return std::make_unique<RemoveCommand>(); }},
        {"commit", [] { return std::make_unique<CommitCommand>(); }},
        {"status", [] { return std::make_unique<StatusCommand>(); }},
        {"log", [] { return std::make_unique<LogCommand>(); }},
        {"branch", [] { return std::make_unique<BranchCommand>(); }},
        {"checkout", [] { return std::make_unique<CheckoutCommand>(); }},
        {"diff", [] { return std::make_unique<DiffCommand>(); }},
        {"cat-file", [] { return std::make_unique<CatFileCommand>(); }},
        {"hash-object", [] { return std::make_unique<HashObjectCommand>(); }},
        {"help", [] { return std::make_unique<HelpCommand>(); }}};
    auto it = commands.find(name);
    if (it == commands.end())
        throw Error("Unknown command: " + name + " (use minigit help)");
    return it->second();
}
int Application::run(const Arguments &args, const fs::path &cwd, std::ostream &out,
                     std::ostream &err) const {
    try {
        auto name = args.empty() ? "help" : args[0];
        if (name == "--help" || name == "-h")
            name = "help";
        auto command = CommandFactory::create(name);
        CommandContext context{fs::absolute(cwd), out};
        command->execute(args.empty() ? Arguments{} : Arguments(args.begin() + 1, args.end()),
                         context);
        return 0;
    } catch (const std::exception &e) {
        err << "minigit: " << e.what() << '\n';
        return 1;
    }
}
} // namespace minigit
