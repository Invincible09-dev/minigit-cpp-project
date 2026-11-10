#include "minigit/object.hpp"
#include "minigit/io.hpp"
#include "minigit/sha1.hpp"
#include <algorithm>
#include <charconv>
#include <set>
#include <sstream>

namespace minigit {
std::string Object::encoded() const {
    const auto payload = serialize();
    return type() + " " + std::to_string(payload.size()) + '\0' + payload;
}
std::string Object::id() const {
    return sha1(encoded());
}
Tree::Tree(std::vector<TreeEntry> entries) : entries_(std::move(entries)) {
    std::set<std::string> names;
    for (const auto &e : entries_) {
        validatePath(e.name);
        if (e.name.find('/') != std::string::npos || !validOid(e.oid) ||
            !names.insert(e.name).second)
            throw Error("Invalid tree entry");
    }
    std::sort(entries_.begin(), entries_.end(), [](const auto &a, const auto &b) {
        return a.name + (a.directory ? "/" : "") < b.name + (b.directory ? "/" : "");
    });
}
std::string Tree::serialize() const {
    std::string out;
    for (const auto &e : entries_)
        out += (e.directory ? "40000 " : "100644 ") + e.name + '\0' + oidToRaw(e.oid);
    return out;
}
Commit::Commit(std::string t, std::string p, std::string a, std::int64_t ts, std::string m)
    : tree(std::move(t)), parent(std::move(p)), author(std::move(a)), message(std::move(m)),
      timestamp(ts) {
    if (!validOid(tree) || (!parent.empty() && !validOid(parent)))
        throw Error("Invalid commit reference");
    if (author.empty() || author.find_first_of("\r\n") != std::string::npos ||
        author.find('\0') != std::string::npos)
        throw Error("Invalid author");
    if (message.find_first_not_of(" \t\r\n") == std::string::npos ||
        message.find('\0') != std::string::npos)
        throw Error("Commit message must be nonempty text");
    if (timestamp < 0)
        throw Error("Invalid timestamp");
}
std::string Commit::serialize() const {
    std::string out = "tree " + tree + "\n";
    if (!parent.empty())
        out += "parent " + parent + "\n";
    const auto identity = author + " " + std::to_string(timestamp) + " +0000\n";
    return out + "author " + identity + "committer " + identity + "\n" + message;
}
std::unique_ptr<Object> ObjectFactory::decode(std::string_view bytes) {
    const auto zero = bytes.find('\0'), space = bytes.find(' ');
    if (zero == bytes.npos || space == bytes.npos || space >= zero)
        throw Error("Malformed object header");
    std::size_t size = 0;
    auto parsed = std::from_chars(bytes.data() + space + 1, bytes.data() + zero, size);
    if (parsed.ec != std::errc{} || parsed.ptr != bytes.data() + zero ||
        std::to_string(size) != bytes.substr(space + 1, zero - space - 1) ||
        size != bytes.size() - zero - 1)
        throw Error("Object size mismatch");
    auto type = bytes.substr(0, space), payload = bytes.substr(zero + 1);
    std::unique_ptr<Object> result;
    if (type == "blob")
        result = std::make_unique<Blob>(std::string(payload));
    else if (type == "tree") {
        std::vector<TreeEntry> entries;
        std::size_t pos = 0;
        while (pos < payload.size()) {
            auto sp = payload.find(' ', pos), nul = payload.find('\0', pos);
            if (sp == payload.npos || nul == payload.npos || sp >= nul ||
                payload.size() - nul - 1 < 20)
                throw Error("Malformed tree");
            auto mode = payload.substr(pos, sp - pos);
            if (mode != "40000" && mode != "100644")
                throw Error("Unsupported file mode");
            entries.push_back({std::string(payload.substr(sp + 1, nul - sp - 1)),
                               rawToOid(payload.substr(nul + 1, 20)), mode == "40000"});
            pos = nul + 21;
        }
        result = std::make_unique<Tree>(std::move(entries));
    } else if (type == "commit") {
        const auto sep = payload.find("\n\n");
        if (sep == payload.npos)
            throw Error("Malformed commit");
        std::istringstream in(std::string(payload.substr(0, sep)));
        std::string line, tree, parent, identity, committer;
        if (!std::getline(in, line) || line.rfind("tree ", 0) != 0)
            throw Error("Missing tree");
        tree = line.substr(5);
        if (!std::getline(in, line))
            throw Error("Missing author");
        if (line.rfind("parent ", 0) == 0) {
            parent = line.substr(7);
            if (!std::getline(in, line))
                throw Error("Missing author");
        }
        if (line.rfind("author ", 0) != 0)
            throw Error("Missing author");
        identity = line.substr(7);
        if (!std::getline(in, line) || line.rfind("committer ", 0) != 0)
            throw Error("Missing committer");
        committer = line.substr(10);
        if (identity != committer || std::getline(in, line))
            throw Error("Unsupported commit headers");
        auto zone = identity.rfind(' ');
        if (zone == std::string::npos || identity.substr(zone) != " +0000")
            throw Error("Invalid timezone");
        auto time = identity.rfind(' ', zone - 1);
        if (time == std::string::npos)
            throw Error("Missing timestamp");
        std::int64_t timestamp = 0;
        auto number =
            std::from_chars(identity.data() + time + 1, identity.data() + zone, timestamp);
        if (number.ec != std::errc{} || number.ptr != identity.data() + zone)
            throw Error("Invalid timestamp");
        result = std::make_unique<Commit>(tree, parent, identity.substr(0, time), timestamp,
                                          std::string(payload.substr(sep + 2)));
    } else
        throw Error("Unknown object type");
    if (result->encoded() != bytes)
        throw Error("Noncanonical object encoding");
    return result;
}
} // namespace minigit
