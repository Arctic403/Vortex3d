#include "vortex/core/document.hpp"

#include <utility>

namespace vortex {

Document::Document() : id_(DocumentId{1}), nextId_(2) {}

MeshId Document::createMesh(std::string name) {
    const MeshId id = allocateId<MeshId>();
    meshes_.emplace(id, MeshBlock{id, std::move(name), 1});
    return id;
}

ObjectId Document::createObject(std::string name, const MeshId meshId) {
    if (meshId && !hasMesh(meshId)) {
        return {};
    }

    const ObjectId id = allocateId<ObjectId>();
    objects_.emplace(id, ObjectBlock{id, std::move(name), meshId, {}, 1});
    return id;
}

bool Document::hasMesh(const MeshId id) const noexcept {
    return id && meshes_.contains(id);
}

bool Document::hasObject(const ObjectId id) const noexcept {
    return id && objects_.contains(id);
}

const MeshBlock* Document::mesh(const MeshId id) const noexcept {
    const auto it = meshes_.find(id);
    return it == meshes_.end() ? nullptr : &it->second;
}

const ObjectBlock* Document::object(const ObjectId id) const noexcept {
    const auto it = objects_.find(id);
    return it == objects_.end() ? nullptr : &it->second;
}

bool Document::setObjectMesh(const ObjectId objectId, const MeshId meshId) {
    const auto objectIt = objects_.find(objectId);
    if (objectIt == objects_.end()) {
        return false;
    }
    if (meshId && !hasMesh(meshId)) {
        return false;
    }
    if (objectIt->second.meshId == meshId) {
        return true;
    }

    objectIt->second.meshId = meshId;
    ++objectIt->second.revision;
    return true;
}

bool Document::setObjectParent(const ObjectId objectId, const ObjectId parentId) {
    const auto objectIt = objects_.find(objectId);
    if (objectIt == objects_.end()) {
        return false;
    }
    if (parentId && !hasObject(parentId)) {
        return false;
    }
    if (wouldCreateParentCycle(objectId, parentId)) {
        return false;
    }
    if (objectIt->second.parentId == parentId) {
        return true;
    }

    objectIt->second.parentId = parentId;
    ++objectIt->second.revision;
    return true;
}

bool Document::removeObject(const ObjectId id) {
    const auto it = objects_.find(id);
    if (it == objects_.end()) {
        return false;
    }

    for (auto& [childId, child] : objects_) {
        (void)childId;
        if (child.parentId == id) {
            child.parentId = {};
            ++child.revision;
        }
    }

    objects_.erase(it);
    return true;
}

bool Document::removeMesh(const MeshId id) {
    const auto it = meshes_.find(id);
    if (it == meshes_.end() || meshUserCount(id) != 0) {
        return false;
    }

    meshes_.erase(it);
    return true;
}

bool Document::wouldCreateParentCycle(const ObjectId objectId, ObjectId parentId) const noexcept {
    if (!parentId) {
        return false;
    }

    std::size_t visited = 0;
    ObjectId cursor = parentId;
    while (cursor) {
        if (cursor == objectId) {
            return true;
        }

        const auto it = objects_.find(cursor);
        if (it == objects_.end()) {
            return false;
        }

        cursor = it->second.parentId;
        if (++visited > objects_.size()) {
            return true;
        }
    }

    return false;
}

std::size_t Document::meshUserCount(const MeshId meshId) const noexcept {
    std::size_t users = 0;
    for (const auto& [objectId, object] : objects_) {
        (void)objectId;
        if (object.meshId == meshId) {
            ++users;
        }
    }
    return users;
}

bool Document::validate() const noexcept {
    if (!id_) {
        return false;
    }

    for (const auto& [meshId, mesh] : meshes_) {
        if (!meshId || mesh.id != meshId) {
            return false;
        }
    }

    for (const auto& [objectId, object] : objects_) {
        if (!objectId || object.id != objectId) {
            return false;
        }
        if (object.meshId && !hasMesh(object.meshId)) {
            return false;
        }
        if (object.parentId && !hasObject(object.parentId)) {
            return false;
        }
        if (wouldCreateParentCycle(objectId, object.parentId)) {
            return false;
        }
    }

    return true;
}

} // namespace vortex
