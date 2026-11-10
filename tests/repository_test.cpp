#include "minigit/sha1.hpp"
#include "support.hpp"

namespace minigit {
class RepoTest : public Sandbox {
  protected:
    std::unique_ptr<Repository> r;
    void SetUp() override {
        Sandbox::SetUp();
        r = std::make_unique<Repository>(Repository::init(dir));
    }
    void save(const std::string &path, const std::string &text) {
        put(path, text);
        r->add({path});
    }
    std::string commit(const std::string &msg = "snapshot") {
        return r->commit(msg, "Tester <t@local>", 123);
    }
};
TEST_F(RepoTest, InitAndDiscover) {
    EXPECT_EQ(r->currentBranch(), "main");
    EXPECT_TRUE(r->head().empty());
    EXPECT_TRUE(r->index().empty());
    EXPECT_TRUE(r->snapshot().empty());
    fs::create_directories(dir / "a" / "b");
    EXPECT_EQ(Repository::discover(dir / "a" / "b").root(), dir);
    EXPECT_THROW(Repository::init(dir), Error);
}
TEST_F(RepoTest, StagedBytesAreIndependentOfWorkingCopy) {
    save("a.txt", "staged");
    put("a.txt", "later");
    auto id = commit();
    EXPECT_EQ(r->object(r->snapshot().at("a.txt"))->serialize(), "staged");
    EXPECT_EQ(r->head(), id);
    auto s = r->status();
    EXPECT_TRUE(s.staged.empty());
    ASSERT_EQ(s.unstaged.size(), 1);
    EXPECT_EQ(s.unstaged[0].kind, 'M');
    EXPECT_EQ(get("a.txt"), "later");
}
TEST_F(RepoTest, NestedTreesAndBinaryFilesSurviveReopen) {
    save("src/deep/a.bin", std::string("x\0y\xff", 4));
    save("a space.txt", "");
    commit();
    Repository reopened(dir);
    EXPECT_EQ(reopened.index(), r->snapshot());
    EXPECT_EQ(reopened.object(reopened.snapshot().at("src/deep/a.bin"))->serialize(),
              std::string("x\0y\xff", 4));
    auto c = reopened.object(reopened.head());
    auto value = dynamic_cast<Commit *>(c.get());
    ASSERT_NE(value, nullptr);
    auto tree = reopened.object(value->tree);
    EXPECT_NE(dynamic_cast<Tree *>(tree.get()), nullptr);
}
TEST_F(RepoTest, CommitHistoryAndNoOpRejection) {
    save("a", "one");
    auto first = commit("first");
    EXPECT_THROW(commit(), Error);
    save("a", "two");
    auto second = commit("second");
    auto log = r->log();
    ASSERT_EQ(log.size(), 2);
    EXPECT_EQ(log[0].first, second);
    EXPECT_EQ(log[0].second.parent, first);
    EXPECT_TRUE(log[1].second.parent.empty());
    EXPECT_EQ(log[1].second.message, "first");
}
TEST_F(RepoTest, EmptyInitialCommit) {
    auto id = commit();
    EXPECT_FALSE(id.empty());
    EXPECT_TRUE(r->snapshot().empty());
    EXPECT_THROW(commit(), Error);
}
TEST_F(RepoTest, AddDirectoryStagesDeletionsAndSkipsMetadata) {
    save("src/a", "a");
    save("src/b", "b");
    commit();
    fs::remove(dir / "src/a");
    put("src/c", "c");
    r->add({"src"});
    EXPECT_FALSE(r->index().count("src/a"));
    EXPECT_TRUE(r->index().count("src/c"));
    r->add({"."});
    EXPECT_EQ(r->index().size(), 2);
    commit();
    EXPECT_EQ(r->snapshot(), r->index());
}
TEST_F(RepoTest, MissingTrackedFileCanBeStaged) {
    save("a", "a");
    commit();
    fs::remove(dir / "a");
    r->add({"a"});
    ASSERT_EQ(r->status().staged.size(), 1);
    EXPECT_EQ(r->status().staged[0].kind, 'D');
    commit();
    EXPECT_TRUE(r->snapshot().empty());
}
TEST_F(RepoTest, RemoveKeepsWorkingCopy) {
    save("a", "a");
    commit();
    r->remove({"a"});
    EXPECT_TRUE(r->index().empty());
    EXPECT_EQ(get("a"), "a");
    EXPECT_EQ(r->status().untracked, (std::vector<std::string>{"a"}));
    commit();
    EXPECT_TRUE(r->snapshot().empty());
}
TEST_F(RepoTest, BatchAddFailureKeepsIndex) {
    save("old", "old");
    auto before = r->index();
    put("new", "new");
    EXPECT_THROW(r->add({"new", "missing"}), Error);
    EXPECT_EQ(r->index(), before);
}
TEST_F(RepoTest, BatchRemoveFailureKeepsIndex) {
    save("a", "a");
    auto before = r->index();
    EXPECT_THROW(r->remove({"a", "missing"}), Error);
    EXPECT_EQ(r->index(), before);
}
TEST_F(RepoTest, RejectsOutsideAndMetadataPaths) {
    EXPECT_THROW(r->add({"../outside"}), Error);
    EXPECT_THROW(r->add({".minigit/HEAD"}), Error);
    EXPECT_THROW(r->add({".git/config"}), Error);
    EXPECT_THROW(r->add({}), Error);
}
TEST_F(RepoTest, BranchesDivergeAndCheckoutRestoresFiles) {
    save("a", "main");
    auto main = commit();
    r->branch("feature");
    r->checkout("feature");
    save("a", "feature");
    save("src/b", "extra");
    auto feature = commit();
    EXPECT_NE(main, feature);
    EXPECT_EQ(r->resolve("main"), main);
    put("notes", "untracked");
    r->checkout("main");
    EXPECT_EQ(get("a"), "main");
    EXPECT_FALSE(fs::exists(dir / "src/b"));
    EXPECT_EQ(get("notes"), "untracked");
    EXPECT_EQ(r->index(), r->snapshot());
    r->checkout("feature");
    EXPECT_EQ(get("src/b"), "extra");
    EXPECT_EQ(get("a"), "feature");
    EXPECT_EQ(r->branches(), (std::vector<std::string>{"feature", "main"}));
}
TEST_F(RepoTest, CheckoutRejectsDirtyTrackedFiles) {
    save("a", "one");
    commit();
    r->branch("other");
    put("a", "unsaved");
    EXPECT_THROW(r->checkout("other"), Error);
    EXPECT_EQ(get("a"), "unsaved");
    EXPECT_EQ(r->currentBranch(), "main");
}
TEST_F(RepoTest, CheckoutRejectsStagedChanges) {
    save("a", "one");
    commit();
    r->branch("other");
    save("a", "staged");
    EXPECT_THROW(r->checkout("other"), Error);
    EXPECT_EQ(get("a"), "staged");
}
TEST_F(RepoTest, CheckoutRejectsUntrackedCollisionsBeforeAnyWrites) {
    save("a", "base");
    commit();
    r->branch("other");
    r->checkout("other");
    save("a", "changed");
    save("extra", "tracked there");
    commit();
    r->checkout("main");
    put("extra", "precious");
    EXPECT_THROW(r->checkout("other"), Error);
    EXPECT_EQ(get("extra"), "precious");
    EXPECT_EQ(get("a"), "base");
    EXPECT_EQ(r->currentBranch(), "main");
}
TEST_F(RepoTest, CheckoutRejectsUntrackedAncestor) {
    commit();
    r->branch("nested");
    r->checkout("nested");
    save("dir/file", "x");
    commit();
    r->checkout("main");
    put("dir", "untracked");
    EXPECT_THROW(r->checkout("nested"), std::exception);
    EXPECT_EQ(get("dir"), "untracked");
}
TEST_F(RepoTest, CheckoutHandlesFileDirectoryTransitions) {
    save("a", "file");
    commit();
    r->branch("nested");
    r->checkout("nested");
    fs::remove(dir / "a");
    put("a/b", "nested");
    r->add({"."});
    commit();
    r->checkout("main");
    EXPECT_EQ(get("a"), "file");
    r->checkout("nested");
    EXPECT_EQ(get("a/b"), "nested");
}
TEST_F(RepoTest, CheckoutPreflightsUntrackedEmptyDirectory) {
    save("a", "file");
    save("z", "keep");
    commit();
    r->branch("nested");
    r->checkout("nested");
    fs::remove(dir / "a");
    put("a/b", "nested");
    r->add({"."});
    commit();
    fs::create_directories(dir / "a/empty/deeper");
    EXPECT_THROW(r->checkout("main"), Error);
    EXPECT_EQ(get("a/b"), "nested");
    EXPECT_EQ(r->currentBranch(), "nested");
}
TEST_F(RepoTest, BranchValidationAndUnbornHead) {
    EXPECT_THROW(r->branch("new"), Error);
    commit();
    for (const auto &name : {"../bad", "HEAD", "a/b", "a..b", "x.lock", ""})
        EXPECT_THROW(r->branch(name), Error);
    r->branch("dev");
    EXPECT_THROW(r->branch("dev"), Error);
    EXPECT_THROW(r->checkout("missing"), Error);
}
TEST_F(RepoTest, DiffModesUseCorrectSnapshots) {
    save("a", "one\n");
    auto first = commit();
    save("a", "two\n");
    put("a", "three\n");
    auto unstaged = r->diff(), staged = r->diff(true);
    EXPECT_NE(unstaged.find("-two\n+three\n"), unstaged.npos);
    EXPECT_NE(staged.find("-one\n+two\n"), staged.npos);
    auto second = commit();
    EXPECT_NE(r->diff(first, second).find("+two\n"), std::string::npos);
}
TEST_F(RepoTest, DiffOmitsUntrackedFiles) {
    put("untracked", "x");
    EXPECT_TRUE(r->diff().empty());
}
TEST_F(RepoTest, CorruptIndexIsRejected) {
    put(".minigit/index", "garbage");
    EXPECT_THROW(r->index(), Error);
}
TEST_F(RepoTest, TruncatedIndexIsRejected) {
    save("a", "x");
    auto bytes = get(".minigit/index");
    bytes.pop_back();
    put(".minigit/index", bytes);
    EXPECT_THROW(r->index(), Error);
}
TEST_F(RepoTest, CorruptTargetDoesNotChangeWorkingFiles) {
    save("a", "base");
    commit();
    r->branch("bad");
    r->checkout("bad");
    save("a", "changed");
    commit();
    auto oid = r->snapshot().at("a");
    r->checkout("main");
    ObjectStore store(dir / ".minigit");
    atomicWrite(store.path(oid), "corrupt");
    EXPECT_THROW(r->checkout("bad"), Error);
    EXPECT_EQ(get("a"), "base");
    EXPECT_EQ(r->currentBranch(), "main");
}
TEST_F(RepoTest, LockBlocksMutation) {
    RepoLock lock(dir / ".minigit");
    put("a", "x");
    EXPECT_THROW(r->add({"a"}), Error);
    EXPECT_TRUE(r->index().empty());
}
TEST_F(RepoTest, SymlinkCannotEscapeRepository) {
    fs::path target = dir / "real";
    put("real", "original");
    std::error_code ec;
    fs::create_symlink(target, dir / "link", ec);
    if (ec)
        GTEST_SKIP() << "Symlink creation unavailable";
    EXPECT_THROW(r->add({"link"}), Error);
    EXPECT_THROW(r->add({"."}), Error);
    EXPECT_EQ(get("real"), "original");
}
} // namespace minigit
