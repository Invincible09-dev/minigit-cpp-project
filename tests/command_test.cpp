#include "minigit/command.hpp"
#include "support.hpp"
#include <sstream>

namespace minigit {
class CliTest : public Sandbox {
  protected:
    std::ostringstream out, err;
    int run(const Arguments &args, const fs::path &cwd = {}) {
        out.str("");
        out.clear();
        err.str("");
        err.clear();
        return Application{}.run(args, cwd.empty() ? dir : cwd, out, err);
    }
};
TEST_F(CliTest, HelpAndUnknownCommand) {
    EXPECT_EQ(run({}), 0);
    EXPECT_NE(out.str().find("MiniGit"), std::string::npos);
    EXPECT_EQ(run({"--help"}), 0);
    EXPECT_EQ(run({"nonsense"}), 1);
    EXPECT_NE(err.str().find("Unknown command"), std::string::npos);
}
TEST_F(CliTest, FactoryCreatesPolymorphicCommands) {
    for (const auto &name : {"init", "add", "rm", "commit", "status", "log", "branch", "checkout",
                             "diff", "cat-file", "hash-object", "help"})
        EXPECT_NE(CommandFactory::create(name), nullptr);
    EXPECT_THROW(CommandFactory::create("missing"), Error);
}
TEST_F(CliTest, CompleteWorkflow) {
    ASSERT_EQ(run({"init"}), 0);
    put("file.txt", "hello\n");
    EXPECT_EQ(run({"status"}), 0);
    EXPECT_NE(out.str().find("Untracked"), std::string::npos);
    EXPECT_EQ(run({"add", "file.txt"}), 0);
    EXPECT_EQ(run({"diff", "--staged"}), 0);
    EXPECT_NE(out.str().find("+hello"), std::string::npos);
    EXPECT_EQ(run({"commit", "-m", "first", "--author", "Tushar <t@example.com>"}), 0);
    EXPECT_EQ(run({"branch", "dev"}), 0);
    EXPECT_EQ(run({"checkout", "dev"}), 0);
    put("file.txt", "changed\n");
    EXPECT_EQ(run({"add", "."}), 0);
    EXPECT_EQ(run({"commit", "-m", "second"}), 0);
    EXPECT_EQ(run({"diff", "main", "dev"}), 0);
    EXPECT_NE(out.str().find("+changed"), std::string::npos);
    EXPECT_EQ(run({"log"}), 0);
    EXPECT_NE(out.str().find("first"), std::string::npos);
    EXPECT_EQ(run({"checkout", "main"}), 0);
    EXPECT_EQ(get("file.txt"), "hello\n");
    EXPECT_EQ(run({"status"}), 0);
    EXPECT_NE(out.str().find("Working tree clean"), std::string::npos);
}
TEST_F(CliTest, AddFromSubdirectory) {
    ASSERT_EQ(run({"init"}), 0);
    put("src/a", "a");
    put("outside", "x");
    EXPECT_EQ(run({"add", "a"}, dir / "src"), 0);
    auto entries = Repository(dir).index();
    ASSERT_EQ(entries.size(), 1);
    EXPECT_TRUE(entries.count("src/a"));
}
TEST_F(CliTest, InvalidArgumentsReturnErrors) {
    ASSERT_EQ(run({"init"}), 0);
    for (const auto &args : std::vector<Arguments>{{"add"},
                                                   {"rm"},
                                                   {"commit"},
                                                   {"commit", "-m"},
                                                   {"status", "extra"},
                                                   {"checkout"},
                                                   {"diff", "--bad"},
                                                   {"cat-file", "-z", "x"}})
        EXPECT_EQ(run(args), 1);
}
TEST_F(CliTest, HashAndInspectObjects) {
    put("x", "hello\n");
    EXPECT_EQ(run({"hash-object", "x"}), 0);
    EXPECT_EQ(out.str(), "ce013625030ba8dba906f756967f9e9ca394464a\n");
    ASSERT_EQ(run({"init"}), 0);
    ASSERT_EQ(run({"add", "x"}), 0);
    auto oid = Repository(dir).index().at("x");
    EXPECT_EQ(run({"cat-file", "-t", oid}), 0);
    EXPECT_EQ(out.str(), "blob\n");
    EXPECT_EQ(run({"cat-file", "-p", oid}), 0);
    EXPECT_EQ(out.str(), "hello\n");
}
} // namespace minigit
