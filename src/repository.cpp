#include "minigit/repository.hpp"
#include "minigit/diff.hpp"
#include "minigit/sha1.hpp"
#include <algorithm>
#include <chrono>
#include <functional>
#include <limits>
#include <set>

namespace minigit {
namespace {
void validateBranch(const std::string &name) {
    if (name.empty() || name == "HEAD" || name.front() == '.' || name.back() == '.' ||
        name.find("..") != std::string::npos || validOid(name) ||
        (name.size() >= 5 && name.substr(name.size() - 5) == ".lock"))
        throw Error("Invalid branch name");
    for (unsigned char c : name)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-' || c == '.'))
            throw Error("Branch names may contain letters, digits, dash, underscore and dot");
}
bool beneath(const std::string &path, const std::string &prefix) {
    return prefix.empty() || path == prefix ||
           (path.size() > prefix.size() && path.compare(0, prefix.size(), prefix) == 0 &&
            path[prefix.size()] == '/');
}
void validateSnapshot(const Snapshot &entries) {
    for (const auto &[path, oid] : entries) {
        validatePath(path);
        if (!validOid(oid))
            throw Error("Invalid index object ID");
        for (auto slash = path.find('/'); slash != std::string::npos;
             slash = path.find('/', slash + 1))
            if (entries.count(path.substr(0, slash)))
                throw Error("File/directory conflict in index");
    }
}
std::vector<Change> compare(const Snapshot &a, const Snapshot &b) {
    std::vector<Change> out;
    for (const auto &[path, oid] : a) {
        auto it = b.find(path);
        if (it == b.end())
            out.push_back({'D', path});
        else if (it->second != oid)
            out.push_back({'M', path});
    }
    for (const auto &[path, oid] : b)
        if (!a.count(path))
            out.push_back({'A', path});
    std::sort(out.begin(), out.end(), [](const auto &x, const auto &y) { return x.path < y.path; });
    return out;
}
} // namespace
Repository Repository::init(const fs::path &root) {
    auto absolute = fs::absolute(root).lexically_normal();
    checkNoSymlink(absolute);
    fs::create_directories(absolute);
    const auto meta = absolute / ".minigit";
    if (pathExists(meta))
        throw Error("Repository already exists");
    if (!fs::create_directory(meta))
        throw Error("Repository already exists");
    try {
        fs::create_directories(meta / "objects");
        fs::create_directories(meta / "refs" / "heads");
        atomicWrite(meta / "FORMAT", "minigit 1\n");
        atomicWrite(meta / "HEAD", "ref: refs/heads/main\n");
        atomicWrite(meta / "refs" / "heads" / "main", "");
        atomicWrite(meta / "index", std::string("MGIX\1", 5) + std::string(4, '\0'));
    } catch (...) {
        std::error_code ec;
        fs::remove_all(meta, ec);
        throw;
    }
    return Repository(absolute);
}
Repository Repository::discover(const fs::path &from) {
    auto path = fs::absolute(from).lexically_normal();
    while (true) {
        if (pathExists(path / ".minigit"))
            return Repository(path);
        auto parent = path.parent_path();
        if (parent == path)
            throw Error("Not inside a MiniGit repository");
        path = parent;
    }
}
Repository::Repository(fs::path root)
    : root_(fs::absolute(std::move(root)).lexically_normal()), meta_(root_ / ".minigit"),
      store_(meta_) {
    checkNoSymlink(meta_);
    if (!fs::is_directory(meta_) || readFile(meta_ / "FORMAT") != "minigit 1\n")
        throw Error("Unsupported repository format");
    (void)currentBranch();
}
std::string Repository::currentBranch() const {
    const auto text = readFile(meta_ / "HEAD");
    const std::string prefix = "ref: refs/heads/";
    if (text.rfind(prefix, 0) != 0 || text.back() != '\n')
        throw Error("Invalid HEAD");
    const auto name = text.substr(prefix.size(), text.size() - prefix.size() - 1);
    validateBranch(name);
    return name;
}
std::string Repository::head() const {
    auto value = readFile(meta_ / "refs" / "heads" / currentBranch());
    if (value.empty())
        return {};
    if (value.size() != 41 || value.back() != '\n' || !validOid(value.substr(0, 40)))
        throw Error("Invalid branch reference");
    return value.substr(0, 40);
}
std::string Repository::resolve(const std::string &revision) const {
    std::string oid;
    if (revision == "HEAD")
        oid = head();
    else if (validOid(revision))
        oid = revision;
    else {
        validateBranch(revision);
        auto data = readFile(meta_ / "refs" / "heads" / revision);
        if (data.size() != 41 || data.back() != '\n' || !validOid(data.substr(0, 40)))
            throw Error("Unborn or invalid branch");
        oid = data.substr(0, 40);
    }
    if (!oid.empty())
        (void)readCommit(oid);
    return oid;
}
Snapshot Repository::index() const {
    const auto bytes = readFile(meta_ / "index");
    if (bytes.substr(0, 5) != std::string("MGIX\1", 5))
        throw Error("Invalid index signature");
    std::size_t pos = 5;
    const auto count = readU32(bytes, pos);
    if (count > (bytes.size() - pos) / 25)
        throw Error("Invalid index count");
    Snapshot out;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto size = readU32(bytes, pos);
        if (size > bytes.size() - pos || bytes.size() - pos - size < 20)
            throw Error("Truncated index entry");
        auto path = bytes.substr(pos, size);
        pos += size;
        auto oid = rawToOid(std::string_view(bytes).substr(pos, 20));
        pos += 20;
        if (!out.emplace(path, oid).second)
            throw Error("Duplicate index entry");
    }
    if (pos != bytes.size())
        throw Error("Trailing index data");
    validateSnapshot(out);
    return out;
}
void Repository::saveIndex(const Snapshot &entries) const {
    validateSnapshot(entries);
    if (entries.size() > std::numeric_limits<std::uint32_t>::max())
        throw Error("Index too large");
    std::string bytes("MGIX\1", 5);
    appendU32(bytes, static_cast<std::uint32_t>(entries.size()));
    for (const auto &[path, oid] : entries) {
        if (path.size() > std::numeric_limits<std::uint32_t>::max())
            throw Error("Path too long");
        appendU32(bytes, static_cast<std::uint32_t>(path.size()));
        bytes += path;
        bytes += oidToRaw(oid);
    }
    atomicWrite(meta_ / "index", bytes);
}
std::string Repository::relativePath(const fs::path &input) const {
    auto full = (input.is_absolute() ? input : root_ / input).lexically_normal();
    auto rel = full.lexically_relative(root_).generic_string();
    if (rel == ".")
        return {};
    validatePath(rel);
    checkNoSymlink(full);
    return rel;
}
Snapshot Repository::working() const {
    Snapshot out;
    for (fs::recursive_directory_iterator it(root_), end; it != end; ++it) {
        const auto name = it->path().filename().string();
        if (name == ".minigit" || name == ".git") {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_symlink())
            throw Error("Symlinks are unsupported: " + it->path().string());
        if (it->is_regular_file()) {
            const auto path = it->path().lexically_relative(root_).generic_string();
            validatePath(path);
            out[path] = Blob(readFile(it->path())).id();
        } else if (!it->is_directory())
            throw Error("Unsupported file type: " + it->path().string());
    }
    return out;
}
void Repository::add(const std::vector<fs::path> &paths) {
    if (paths.empty())
        throw Error("add requires at least one path");
    RepoLock lock(meta_);
    auto next = index();
    for (const auto &input : paths) {
        auto rel = relativePath(input);
        const auto full = root_ / rel;
        std::vector<std::string> selected;
        if (fs::is_regular_file(full))
            selected.push_back(rel);
        else if (fs::is_directory(full)) {
            for (fs::recursive_directory_iterator it(full), end; it != end; ++it) {
                const auto name = it->path().filename().string();
                if (name == ".minigit" || name == ".git") {
                    it.disable_recursion_pending();
                    continue;
                }
                if (it->is_symlink())
                    throw Error("Symlinks are unsupported");
                if (it->is_regular_file())
                    selected.push_back(relativePath(it->path()));
                else if (!it->is_directory())
                    throw Error("Unsupported file type");
            }
        } else if (pathExists(full))
            throw Error("Unsupported file type");
        else {
            bool tracked = false;
            for (const auto &[path, oid] : next)
                if (beneath(path, rel)) {
                    tracked = true;
                    break;
                }
            if (!tracked)
                throw Error("Path does not exist and is not tracked: " + rel);
        }
        // A directory add refreshes that entire subtree, including deletions.
        for (auto it = next.begin(); it != next.end();)
            if (beneath(it->first, rel))
                it = next.erase(it);
            else
                ++it;
        for (const auto &path : selected)
            next[path] = store_.write(Blob(readFile(root_ / path)));
    }
    saveIndex(next);
}
void Repository::remove(const std::vector<fs::path> &paths) {
    if (paths.empty())
        throw Error("rm requires at least one path");
    RepoLock lock(meta_);
    auto next = index();
    // Stage removal only, preserving the working copy (like git rm --cached).
    for (const auto &input : paths) {
        auto rel = relativePath(input);
        bool found = false;
        for (auto it = next.begin(); it != next.end();) {
            if (beneath(it->first, rel)) {
                it = next.erase(it);
                found = true;
            } else
                ++it;
        }
        if (!found)
            throw Error("Path is not staged or tracked: " + rel);
    }
    saveIndex(next);
}
std::string Repository::blob(const std::string &oid) const {
    auto obj = store_.read(oid);
    auto value = dynamic_cast<Blob *>(obj.get());
    if (!value)
        throw Error("Expected blob: " + oid);
    return value->data;
}
Commit Repository::readCommit(const std::string &oid) const {
    auto obj = store_.read(oid);
    auto value = dynamic_cast<Commit *>(obj.get());
    if (!value)
        throw Error("Expected commit: " + oid);
    return *value;
}
std::string Repository::writeTree(const Snapshot &entries) const {
    validateSnapshot(entries);
    struct Node {
        std::map<std::string, Node> dirs;
        std::map<std::string, std::string> files;
    };
    Node root;
    for (const auto &[path, oid] : entries) {
        (void)blob(oid);
        Node *node = &root;
        std::size_t start = 0;
        for (auto slash = path.find('/'); slash != std::string::npos;
             slash = path.find('/', start)) {
            node = &node->dirs[path.substr(start, slash - start)];
            start = slash + 1;
        }
        node->files[path.substr(start)] = oid;
    }
    std::function<std::string(const Node &)> save = [&](const Node &node) {
        std::vector<TreeEntry> list;
        for (const auto &[name, oid] : node.files)
            list.push_back({name, oid, false});
        for (const auto &[name, child] : node.dirs)
            list.push_back({name, save(child), true});
        return store_.write(Tree(std::move(list)));
    };
    return save(root);
}
void Repository::readTree(const std::string &oid, const std::string &prefix, Snapshot &out,
                          std::size_t depth) const {
    if (depth > 256)
        throw Error("Tree nesting exceeds 256 levels");
    auto obj = store_.read(oid);
    auto tree = dynamic_cast<Tree *>(obj.get());
    if (!tree)
        throw Error("Expected tree");
    for (const auto &e : tree->entries()) {
        auto path = prefix + e.name;
        validatePath(path);
        if (e.directory)
            readTree(e.oid, path + "/", out, depth + 1);
        else {
            (void)blob(e.oid);
            if (!out.emplace(path, e.oid).second)
                throw Error("Duplicate tree path");
        }
    }
}
Snapshot Repository::snapshot(const std::string &revision) const {
    const auto oid = resolve(revision);
    Snapshot out;
    if (!oid.empty())
        readTree(readCommit(oid).tree, "", out);
    validateSnapshot(out);
    return out;
}
std::string Repository::commit(const std::string &message, const std::string &author,
                               std::int64_t timestamp) {
    RepoLock lock(meta_);
    auto entries = index();
    const auto parent = head();
    if (!parent.empty() && entries == snapshot())
        throw Error("Nothing staged to commit");
    if (timestamp < 0)
        timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
    const auto oid = store_.write(Commit(writeTree(entries), parent, author, timestamp, message));
    atomicWrite(meta_ / "refs" / "heads" / currentBranch(), oid + "\n");
    return oid;
}
Status Repository::status() const {
    auto staged = index(), work = working();
    Status out;
    out.staged = compare(snapshot(), staged);
    Snapshot tracked;
    for (const auto &[path, oid] : work) {
        if (staged.count(path))
            tracked[path] = oid;
        else
            out.untracked.push_back(path);
    }
    out.unstaged = compare(staged, tracked);
    return out;
}
void Repository::branch(const std::string &name) {
    validateBranch(name);
    RepoLock lock(meta_);
    const auto oid = head();
    if (oid.empty())
        throw Error("Create a commit before branching");
    (void)readCommit(oid);
    const auto path = meta_ / "refs" / "heads" / name;
    if (pathExists(path))
        throw Error("Branch already exists");
    atomicWrite(path, oid + "\n");
}
std::vector<std::string> Repository::branches() const {
    std::vector<std::string> out;
    checkNoSymlink(meta_ / "refs" / "heads");
    for (const auto &e : fs::directory_iterator(meta_ / "refs" / "heads")) {
        checkNoSymlink(e.path());
        if (!e.is_regular_file())
            throw Error("Invalid branch reference");
        auto name = e.path().filename().string();
        validateBranch(name);
        out.push_back(name);
    }
    std::sort(out.begin(), out.end());
    return out;
}
void Repository::checkout(const std::string &name) {
    validateBranch(name);
    RepoLock lock(meta_);
    if (name == currentBranch())
        return;
    const auto target = snapshot(name), current = snapshot(), staged = index(), work = working();
    if (staged != current)
        throw Error("Checkout blocked: staged changes");
    for (const auto &[path, oid] : current) {
        auto it = work.find(path);
        if (it == work.end() || it->second != oid)
            throw Error("Checkout blocked: modified tracked file " + path);
    }
    for (const auto &[path, oid] : target) {
        checkNoSymlink(root_ / path);
        for (const auto &[other, ignored] : work)
            if (!current.count(other) && (beneath(other, path) || beneath(path, other)))
                throw Error("Checkout would overwrite untracked file: " + other);
        // Empty untracked subdirectories are absent from the file snapshot.
        // Refuse them now, before removing any tracked files.
        if (fs::is_directory(root_ / path)) {
            for (const auto &entry : fs::recursive_directory_iterator(root_ / path)) {
                if (!entry.is_directory())
                    continue;
                const auto relative = entry.path().lexically_relative(root_).generic_string();
                bool trackedDescendant = false;
                for (const auto &[tracked, ignored] : current)
                    if (beneath(tracked, relative)) {
                        trackedDescendant = true;
                        break;
                    }
                if (!trackedDescendant)
                    throw Error("Untracked directory blocks checkout: " + relative);
            }
        }
    }
    // Read and validate every target before changing any working file.
    std::map<std::string, std::string> contents;
    for (const auto &[path, oid] : target)
        if (!current.count(path) || current.at(path) != oid)
            contents[path] = blob(oid);
    for (const auto &[path, oid] : current)
        if (!target.count(path)) {
            fs::remove(root_ / path);
            auto parent = (root_ / path).parent_path();
            while (parent != root_ && fs::is_directory(parent) && fs::is_empty(parent)) {
                fs::remove(parent);
                parent = parent.parent_path();
            }
        }
    for (const auto &[path, data] : contents) {
        auto full = root_ / path;
        if (fs::is_directory(full)) {
            if (!fs::is_empty(full))
                throw Error("Nonempty directory blocks checkout: " + path);
            fs::remove(full);
        }
        atomicWrite(full, data);
    }
    saveIndex(target);
    atomicWrite(meta_ / "HEAD", "ref: refs/heads/" + name + "\n");
}
std::vector<std::pair<std::string, Commit>> Repository::log() const {
    std::vector<std::pair<std::string, Commit>> out;
    std::set<std::string> seen;
    auto oid = head();
    while (!oid.empty()) {
        if (!seen.insert(oid).second)
            throw Error("Cycle in commit history");
        auto value = readCommit(oid);
        out.emplace_back(oid, value);
        oid = value.parent;
    }
    return out;
}
std::string Repository::diffSnapshots(const Snapshot &a, const Snapshot &b,
                                      bool workingTree) const {
    std::string out;
    for (const auto &change : compare(a, b)) {
        bool oldExists = a.count(change.path), newExists = b.count(change.path);
        auto before = oldExists ? blob(a.at(change.path)) : std::string{};
        auto after = newExists
                         ? (workingTree ? readFile(root_ / change.path) : blob(b.at(change.path)))
                         : std::string{};
        out += Diff::format(change.path, before, after, oldExists, newExists);
    }
    return out;
}
std::string Repository::diff(bool staged) const {
    auto entries = index();
    if (staged)
        return diffSnapshots(snapshot(), entries, false);
    Snapshot tracked;
    for (const auto &[path, oid] : entries) {
        checkNoSymlink(root_ / path);
        if (fs::is_regular_file(root_ / path))
            tracked[path] = Blob(readFile(root_ / path)).id();
    }
    return diffSnapshots(entries, tracked, true);
}
std::string Repository::diff(const std::string &before, const std::string &after) const {
    return diffSnapshots(snapshot(before), snapshot(after), false);
}
} // namespace minigit
