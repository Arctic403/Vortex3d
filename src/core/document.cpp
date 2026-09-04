#include "vortex/core/document.hpp"

#include <utility>

namespace vortex {

Document::Document() : id_(DocumentId{1}), nextId_(2) {}

SceneId Document::createScene(std::string name) {
    const SceneId sceneId = allocateId<SceneId>();
    const CollectionId rootId = allocateId<CollectionId>();

    scenes_.emplace(sceneId, SceneBlock{sceneId, std::move(name), rootId, 1});
    collections_.emplace(rootId, CollectionBlock{rootId, "Scene Collection", sceneId, {}, {}, 1});

    markChanged(DataKind::Scene, ChangeKind::Created, sceneId.value());
    markChanged(DataKind::Collection, ChangeKind::Created, rootId.value());
    return sceneId;
}

CollectionId Document::createCollection(const SceneId sceneId, std::string name, const CollectionId parentId) {
    const auto sceneIt = scenes_.find(sceneId);
    if (sceneIt == scenes_.end()) {
        return {};
    }

    const CollectionId resolvedParent = parentId ? parentId : sceneIt->second.rootCollectionId;
    if (!collectionBelongsToScene(resolvedParent, sceneId)) {
        return {};
    }

    const CollectionId id = allocateId<CollectionId>();
    collections_.emplace(id, CollectionBlock{id, std::move(name), sceneId, resolvedParent, {}, 1});
    markChanged(DataKind::Collection, ChangeKind::Created, id.value());
    return id;
}

MeshId Document::createMesh(std::string name) {
    const MeshId id = allocateId<MeshId>();
    meshes_.emplace(id, MeshBlock{id, std::move(name), 1});
    markChanged(DataKind::Mesh, ChangeKind::Created, id.value());
    return id;
}

ObjectId Document::createObject(std::string name, const MeshId meshId) {
    if (meshId && !hasMesh(meshId)) {
        return {};
    }

    const ObjectId id = allocateId<ObjectId>();
    objects_.emplace(id, ObjectBlock{id, std::move(name), meshId, {}, 1});
    markChanged(DataKind::Object, ChangeKind::Created, id.value());
    return id;
}

bool Document::hasScene(const SceneId id) const noexcept {
    return id && scenes_.contains(id);
}

bool Document::hasCollection(const CollectionId id) const noexcept {
    return id && collections_.contains(id);
}

bool Document::hasMesh(const MeshId id) const noexcept {
    return id && meshes_.contains(id);
}

bool Document::hasObject(const ObjectId id) const noexcept {
    return id && objects_.contains(id);
}

const SceneBlock* Document::scene(const SceneId id) const noexcept {
    const auto it = scenes_.find(id);
    return it == scenes_.end() ? nullptr : &it->second;
}

const CollectionBlock* Document::collection(const CollectionId id) const noexcept {
    const auto it = collections_.find(id);
    return it == collections_.end() ? nullptr : &it->second;
}

const MeshBlock* Document::mesh(const MeshId id) const noexcept {
    const auto it = meshes_.find(id);
    return it == meshes_.end() ? nullptr : &it->second;
}

const ObjectBlock* Document::object(const ObjectId id) const noexcept {
    const auto it = objects_.find(id);
    return it == objects_.end() ? nullptr : &it->second;
}

bool Document::renameScene(const SceneId sceneId, std::string name) {
    const auto it = scenes_.find(sceneId);
    if (it == scenes_.end()) {
        return false;
    }
    if (it->second.name == name) {
        return true;
    }
    it->second.name = std::move(name);
    ++it->second.revision;
    markChanged(DataKind::Scene, ChangeKind::Updated, sceneId.value());
    return true;
}

bool Document::renameCollection(const CollectionId collectionId, std::string name) {
    const auto it = collections_.find(collectionId);
    if (it == collections_.end()) {
        return false;
    }
    if (it->second.name == name) {
        return true;
    }
    it->second.name = std::move(name);
    ++it->second.revision;
    markChanged(DataKind::Collection, ChangeKind::Updated, collectionId.value());
    return true;
}

bool Document::renameMesh(const MeshId meshId, std::string name) {
    const auto it = meshes_.find(meshId);
    if (it == meshes_.end()) {
        return false;
    }
    if (it->second.name == name) {
        return true;
    }
    it->second.name = std::move(name);
    ++it->second.revision;
    markChanged(DataKind::Mesh, ChangeKind::Updated, meshId.value());
    return true;
}

bool Document::renameObject(const ObjectId objectId, std::string name) {
    const auto it = objects_.find(objectId);
    if (it == objects_.end()) {
        return false;
    }
    if (it->second.name == name) {
        return true;
    }
    it->second.name = std::move(name);
    ++it->second.revision;
    markChanged(DataKind::Object, ChangeKind::Updated, objectId.value());
    return true;
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
    markChanged(DataKind::Object, ChangeKind::Updated, objectId.value());
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
    markChanged(DataKind::Object, ChangeKind::Updated, objectId.value());
    return true;
}

MeshId Document::makeObjectMeshUnique(const ObjectId objectId) {
    const auto objectIt = objects_.find(objectId);
    if (objectIt == objects_.end() || !objectIt->second.meshId) {
        return {};
    }

    const MeshId sourceId = objectIt->second.meshId;
    if (meshUserCount(sourceId) <= 1) {
        return sourceId;
    }

    const auto sourceIt = meshes_.find(sourceId);
    if (sourceIt == meshes_.end()) {
        return {};
    }

    const MeshId cloneId = allocateId<MeshId>();
    MeshBlock clone = sourceIt->second;
    clone.id = cloneId;
    clone.revision = 1;
    meshes_.emplace(cloneId, std::move(clone));

    objectIt->second.meshId = cloneId;
    ++objectIt->second.revision;

    markChanged(DataKind::Mesh, ChangeKind::Created, cloneId.value());
    markChanged(DataKind::Object, ChangeKind::Updated, objectId.value());
    return cloneId;
}

bool Document::linkObjectToCollection(const ObjectId objectId, const CollectionId collectionId) {
    if (!hasObject(objectId)) {
        return false;
    }

    const auto collectionIt = collections_.find(collectionId);
    if (collectionIt == collections_.end()) {
        return false;
    }

    const auto [_, inserted] = collectionIt->second.objectIds.insert(objectId);
    if (!inserted) {
        return true;
    }

    ++collectionIt->second.revision;
    markChanged(DataKind::Collection, ChangeKind::Linked, collectionId.value());
    return true;
}

bool Document::unlinkObjectFromCollection(const ObjectId objectId, const CollectionId collectionId) {
    const auto collectionIt = collections_.find(collectionId);
    if (collectionIt == collections_.end()) {
        return false;
    }

    if (collectionIt->second.objectIds.erase(objectId) == 0) {
        return false;
    }

    ++collectionIt->second.revision;
    markChanged(DataKind::Collection, ChangeKind::Unlinked, collectionId.value());
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
            markChanged(DataKind::Object, ChangeKind::Updated, child.id.value());
        }
    }

    for (auto& [collectionId, collection] : collections_) {
        if (collection.objectIds.erase(id) != 0) {
            ++collection.revision;
            markChanged(DataKind::Collection, ChangeKind::Unlinked, collectionId.value());
        }
    }

    objects_.erase(it);
    markChanged(DataKind::Object, ChangeKind::Removed, id.value());
    return true;
}

bool Document::removeMesh(const MeshId id) {
    const auto it = meshes_.find(id);
    if (it == meshes_.end() || meshUserCount(id) != 0) {
        return false;
    }

    meshes_.erase(it);
    markChanged(DataKind::Mesh, ChangeKind::Removed, id.value());
    return true;
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

std::vector<ChangeEvent> Document::changesSince(const std::uint64_t revision) const {
    std::vector<ChangeEvent> result;
    for (const ChangeEvent& event : changes_) {
        if (event.revision > revision) {
            result.push_back(event);
        }
    }
    return result;
}

void Document::markChanged(const DataKind dataKind, const ChangeKind changeKind, const std::uint64_t entityId) {
    ++revision_;
    changes_.push_back(ChangeEvent{revision_, dataKind, changeKind, entityId});
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

bool Document::collectionBelongsToScene(const CollectionId collectionId, const SceneId sceneId) const noexcept {
    const auto it = collections_.find(collectionId);
    return it != collections_.end() && it->second.sceneId == sceneId;
}

bool Document::validate() const noexcept {
    if (!id_) {
        return false;
    }

    for (const auto& [sceneId, scene] : scenes_) {
        if (!sceneId || scene.id != sceneId) {
            return false;
        }
        const auto rootIt = collections_.find(scene.rootCollectionId);
        if (rootIt == collections_.end() || rootIt->second.sceneId != sceneId || rootIt->second.parentId) {
            return false;
        }
    }

    for (const auto& [collectionId, collection] : collections_) {
        if (!collectionId || collection.id != collectionId || !hasScene(collection.sceneId)) {
            return false;
        }
        if (collection.parentId) {
            const auto parentIt = collections_.find(collection.parentId);
            if (parentIt == collections_.end() || parentIt->second.sceneId != collection.sceneId) {
                return false;
            }
        }
        for (const ObjectId objectId : collection.objectIds) {
            if (!hasObject(objectId)) {
                return false;
            }
        }
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
