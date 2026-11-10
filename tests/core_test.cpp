#include "minigit/diff.hpp"
#include "minigit/sha1.hpp"
#include "support.hpp"
#include <random>

namespace minigit {
TEST(Sha1, StandardVectors) {
    EXPECT_EQ(sha1(""), "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    EXPECT_EQ(sha1("abc"), "a9993e364706816aba3e25717850c26c9cd0d89d");
    EXPECT_EQ(sha1("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
              "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
    EXPECT_EQ(sha1(std::string(1000000, 'a')), "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
}
TEST(Sha1, RawIdRoundTrip) {
    auto oid = sha1(std::string("\0\xff\x80", 3));
    EXPECT_EQ(rawToOid(oidToRaw(oid)), oid);
    EXPECT_THROW(oidToRaw("../x"), Error);
    EXPECT_THROW(rawToOid("short"), Error);
    EXPECT_FALSE(validOid(std::string(40, 'g')));
}
TEST(Objects, GitKnownIds) {
    EXPECT_EQ(Blob("").id(), "e69de29bb2d1d6434b8b29ae775ad8c2e48c5391");
    EXPECT_EQ(Blob("hello\n").id(), "ce013625030ba8dba906f756967f9e9ca394464a");
    EXPECT_EQ(Tree().id(), "4b825dc642cb6eb9a060e54bf8d69288fbee4904");
}
TEST(Objects, BinaryBlobRoundTrip) {
    Blob original(std::string("\0a\xff\nb", 5));
    auto decoded = ObjectFactory::decode(original.encoded());
    ASSERT_NE(dynamic_cast<Blob *>(decoded.get()), nullptr);
    EXPECT_EQ(decoded->serialize(), original.data);
}
TEST(Objects, TreesAreCanonicalAndNested) {
    auto id = Blob("x").id();
    Tree a({{"z", id, false}, {"a", id, true}, {"a.c", id, false}});
    Tree b({{"a.c", id, false}, {"z", id, false}, {"a", id, true}});
    EXPECT_EQ(a.id(), b.id());
    EXPECT_EQ(a.entries()[0].name, "a.c");
    EXPECT_EQ(a.entries()[1].name, "a");
    EXPECT_EQ(ObjectFactory::decode(a.encoded())->serialize(), a.serialize());
}
TEST(Objects, InvalidTreeEntries) {
    auto id = Blob("").id();
    EXPECT_THROW(Tree({{"../x", id, false}}), Error);
    EXPECT_THROW(Tree({{".minigit", id, true}}), Error);
    EXPECT_THROW(Tree({{"x", id, false}, {"x", id, true}}), Error);
    EXPECT_THROW(Tree({{"x", "bad", false}}), Error);
}
TEST(Objects, RejectsExcessivePathDepth) {
    std::string path;
    for (int i = 0; i < 257; ++i)
        path += "a/";
    path += "file";
    EXPECT_THROW(validatePath(path), Error);
}
TEST(Objects, CommitRoundTrip) {
    Commit c(Tree().id(), Blob("parent").id(), "Tushar <t@example.com>", 123, "first\nsecond");
    auto obj = ObjectFactory::decode(c.encoded());
    auto parsed = dynamic_cast<Commit *>(obj.get());
    ASSERT_NE(parsed, nullptr);
    EXPECT_EQ(parsed->tree, c.tree);
    EXPECT_EQ(parsed->parent, c.parent);
    EXPECT_EQ(parsed->author, c.author);
    EXPECT_EQ(parsed->timestamp, 123);
    EXPECT_EQ(parsed->message, c.message);
    EXPECT_EQ(parsed->id(), c.id());
}
TEST(Objects, InvalidCommitFields) {
    EXPECT_THROW(Commit("bad", "", "a", 1, "msg"), Error);
    EXPECT_THROW(Commit(Tree().id(), "", "a\nb", 1, "msg"), Error);
    EXPECT_THROW(Commit(Tree().id(), "", "a", 1, " \n"), Error);
}
TEST(Objects, RejectMalformedEncoding) {
    EXPECT_THROW(ObjectFactory::decode("blob 3"), Error);
    EXPECT_THROW(ObjectFactory::decode(std::string("blob 3\0hi", 9)), Error);
    EXPECT_THROW(ObjectFactory::decode(std::string("blob 00\0", 8)), Error);
    EXPECT_THROW(ObjectFactory::decode(std::string("tag 0\0", 6)), Error);
    EXPECT_THROW(ObjectFactory::decode(std::string("tree 1\0x", 8)), Error);
}
TEST_F(Sandbox, StoreDeduplicatesAndDetectsCorruption) {
    ObjectStore store(dir / ".minigit");
    Blob b("same content");
    auto id = store.write(b);
    EXPECT_EQ(store.write(b), id);
    EXPECT_EQ(store.read(id)->serialize(), b.data);
    EXPECT_EQ(store.path(id).filename().string(), id.substr(2));
    atomicWrite(store.path(id), "damaged");
    EXPECT_THROW(store.read(id), Error);
    EXPECT_THROW(store.write(b), Error);
}
TEST_F(Sandbox, StoreRejectsInvalidAndMissingIds) {
    ObjectStore store(dir / ".minigit");
    EXPECT_THROW(store.read("../../outside"), Error);
    EXPECT_THROW(store.read(Blob("absent").id()), Error);
}
TEST_F(Sandbox, WriterLockIsExclusiveAndReleased) {
    fs::create_directory(dir / ".minigit");
    {
        RepoLock first(dir / ".minigit");
        EXPECT_THROW(RepoLock second(dir / ".minigit"), Error);
    }
    EXPECT_NO_THROW(RepoLock next(dir / ".minigit"));
}
TEST(Diff, EmptyAndIdentical) {
    EXPECT_TRUE(Diff::lcs("", "").empty());
    EXPECT_TRUE(Diff::format("f", "a\n", "a\n").empty());
    auto edits = Diff::lcs("a\n", "a\n");
    ASSERT_EQ(edits.size(), 1);
    EXPECT_EQ(edits[0].kind, EditKind::Equal);
}
TEST(Diff, InsertDeleteAndReplace) {
    auto e = Diff::lcs("a\nb\nc\n", "a\nx\nc\n");
    ASSERT_EQ(e.size(), 4);
    EXPECT_EQ(e[1].kind, EditKind::Delete);
    EXPECT_EQ(e[1].line, "b\n");
    EXPECT_EQ(e[2].kind, EditKind::Insert);
    EXPECT_EQ(e[2].line, "x\n");
    EXPECT_EQ(Diff::lcs("", "x")[0].kind, EditKind::Insert);
    EXPECT_EQ(Diff::lcs("x", "")[0].kind, EditKind::Delete);
}
TEST(Diff, NewlineAndBinaryHandling) {
    auto text = Diff::format("x", "hello", "hello\n");
    EXPECT_NE(text.find("No newline at end of file"), text.npos);
    EXPECT_NE(text.find("+hello\n"), text.npos);
    EXPECT_NE(Diff::format("x", std::string("\0", 1), "b").find("Binary files differ"),
              std::string::npos);
    EXPECT_NE(Diff::format("empty", "", "", false, true).find("new file mode"), std::string::npos);
}
TEST(Diff, RefusesExcessiveMatrix) {
    std::string text;
    for (int i = 0; i < 4100; ++i)
        text += "x\n";
    EXPECT_THROW(Diff::lcs(text, text), Error);
}
TEST(Diff, RandomScriptsReconstructBothInputsAndAreMinimal) {
    std::mt19937 rng(2026);
    for (int trial = 0; trial < 200; ++trial) {
        std::string a, b;
        int n = static_cast<int>(rng() % 8), m = static_cast<int>(rng() % 8);
        for (int i = 0; i < n; ++i) {
            a += static_cast<char>('a' + rng() % 3);
            a += '\n';
        }
        for (int i = 0; i < m; ++i) {
            b += static_cast<char>('a' + rng() % 3);
            b += '\n';
        }
        std::string left, right;
        int changes = 0;
        for (const auto &e : Diff::lcs(a, b)) {
            if (e.kind != EditKind::Insert)
                left += e.line;
            if (e.kind != EditKind::Delete)
                right += e.line;
            if (e.kind != EditKind::Equal)
                ++changes;
        }
        EXPECT_EQ(left, a);
        EXPECT_EQ(right, b);
        // Independent exhaustive subsequence oracle for these small inputs.
        int best = 0;
        for (int mask = 0; mask < (1 << n); ++mask) {
            int matched = 0, j = 0;
            bool ok = true;
            for (int i = 0; i < n; ++i)
                if (mask & (1 << i)) {
                    while (j < m && a[2 * i] != b[2 * j])
                        ++j;
                    if (j == m) {
                        ok = false;
                        break;
                    }
                    ++j;
                    ++matched;
                }
            if (ok)
                best = std::max(best, matched);
        }
        EXPECT_EQ(changes, n + m - 2 * best);
    }
}
} // namespace minigit
