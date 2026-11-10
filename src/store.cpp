#include "minigit/store.hpp"
#include "minigit/sha1.hpp"

namespace minigit {
fs::path ObjectStore::path(const std::string &oid) const {
    if (!validOid(oid))
        throw Error("Invalid object ID: " + oid);
    return directory_ / oid.substr(0, 2) / oid.substr(2);
}
std::string ObjectStore::write(const Object &object) const {
    const auto bytes = object.encoded(), oid = sha1(bytes);
    const auto target = path(oid);
    if (pathExists(target)) {
        if (readFile(target) != bytes)
            throw Error("Object collision or corruption: " + oid);
    } else
        atomicWrite(target, bytes);
    return oid;
}
std::unique_ptr<Object> ObjectStore::read(const std::string &oid) const {
    const auto bytes = readFile(path(oid));
    if (sha1(bytes) != oid)
        throw Error("Corrupt object: " + oid);
    return ObjectFactory::decode(bytes);
}
} // namespace minigit
