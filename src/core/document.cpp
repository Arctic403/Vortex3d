#include "vortex/core/document.hpp"

#include "vortex/mesh/command.hpp"
#include "vortex/mesh/editable_mesh.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace vortex {
namespace {

// Authoring is single-threaded today, so runtime identity allocation follows that contract.
// Zero is reserved as invalid. Exhaustion terminates rather than silently reusing an identity.
std::uint64_t nextRuntimeDocumentValue = 1;

[[nodiscard]] RuntimeDocumentId allocateRuntimeDocumentId() noexcept {
    if (nextRuntimeDocumentValue == 0U) {
        std::abort();
    }
    const RuntimeDocumentId result{nextRuntimeDocumentValue};
    ++nextRuntimeDocumentValue;
    return result;
}

} // namespace

MeshBlock::MeshBlock(
    const MeshId meshId,
    std::string meshName,
    std::unique_ptr<EditableMesh> mesh,
    const std::uint64_t meshRevision)
    : MeshBlock({}, meshId, std::move(meshName), std::move(mesh), meshRevision) {}

MeshBlock::MeshBlock(
    const RuntimeDocumentId ownerDocumentRuntimeId,
    const MeshId meshId,
    std::string meshName,
    std::unique_ptr<EditableMesh> mesh,
    const std::uint64_t meshRevision)
    : id(meshId),
      name(std::move(meshName)),
      revision(meshRevision),
      ownerDocumentRuntimeId_(ownerDocumentRuntimeId),
      evaluationRevision_(meshRevision),
      authoredMesh_(std::move(mesh)) {}

MeshBlock::~MeshBlock() = default;
MeshBlock::MeshBlock(MeshBlock&&) noexcept = default;
MeshBlock& MeshBlock::operator=(MeshBlock&&) noexcept = default;

Document::Document() : runtimeId_(allocateRuntimeDocumentId()), id_(DocumentId{1}), nextId_(2) {}

Document::Document(Document&& other) noexcept
    : runtimeId_(other.runtimeId_),
      id_(other.id_),
      nextId_(other.nextId_),
      revision_(other.revision_),
      scenes_(std::move(other.scenes_)),
      collections_(std::move(other.collections_)),
      meshes_(std::move(other.meshes_)),
      objects_(std::move(other.objects_)),
      changeHistoryBudgetBytes_(other.changeHistoryBudgetBytes_),
      discardedChangesThroughRevision_(other.discardedChangesThroughRevision_),
      pendingClearChangesThroughRevision_(other.pendingClearChangesThroughRevision_),
      changes_(std::move(other.changes_)) {
    other.resetMovedFrom();
}

Document& Document::operator=(Document&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    runtimeId_ = other.runtimeId_;
    id_ = other.id_;
    nextId_ = other.nextId_;
    revision_ = other.revision_;
    scenes_ = std::move(other.scenes_);
    collections_ = std::move(other.collections_);
    meshes_ = std::move(other.meshes_);
    objects_ = std::move(other.objects_);
    changeHistoryBudgetBytes_ = other.changeHistoryBudgetBytes_;
    discardedChangesThroughRevision_ = other.discardedChangesThroughRevision_;
    changeHistoryBatchDepth_ = 0;
    pendingClearChangesThroughRevision_ = other.pendingClearChangesThroughRevision_;
    changes_ = std::move(other.changes_);
    other.resetMovedFrom();
    return *this;
}

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
    return createMesh(std::move(name), EditableMesh{});
}

MeshId Document::createMesh(std::string name, EditableMesh authoredMeshValue) {
    if (!authoredMeshValue.validateStrict()) {
        return {};
    }

    const MeshId id = allocateId<MeshId>();
    meshes_.emplace(
        id,
        MeshBlock{runtimeId_, id, std::move(name), std::make_unique<EditableMesh>(std::move(authoredMeshValue)), 1});
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

bool Document::hasScene(const SceneId id) const noexcept { return id && scenes_.contains(id); }
bool Document::hasCollection(const CollectionId id) const noexcept { return id && collections_.contains(id); }
bool Document::hasMesh(const MeshId id) const noexcept { return id && meshes_.contains(id); }
bool Document::hasObject(const ObjectId id) const noexcept { return id && objects_.contains(id); }

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

const EditableMesh* Document::authoredMesh(const MeshId id) const noexcept {
    const auto it = meshes_.find(id);
    if (it == meshes_.end() || !it->second.authoredMesh_) {
        return nullptr;
    }
    return it->second.authoredMesh_.get();
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

bool Document::setObjectTransform(const ObjectId objectId, const ObjectTransform& transform) {
    const auto objectIt = objects_.find(objectId);
    if (objectIt == objects_.end() || !isFiniteObjectTransform(transform)) {
        return false;
    }
    if (objectIt->second.transform == transform) {
        return true;
    }

    objectIt->second.transform = transform;
    ++objectIt->second.revision;
    markChanged(DataKind::Object, ChangeKind::Updated, objectId.value());
    return true;
}

std::optional<TransformMatrix> Document::objectWorldMatrix(const ObjectId objectId) const {
    const auto objectIt = objects_.find(objectId);
    if (objectIt == objects_.end()) {
        return std::nullopt;
    }

    std::vector<const ObjectBlock*> chain;
    chain.reserve(8U);
    const ObjectBlock* cursor = &objectIt->second;
    while (cursor != nullptr) {
        chain.push_back(cursor);
        if (chain.size() > objects_.size()) {
            return std::nullopt;
        }
        if (!cursor->parentId) {
            break;
        }
        const auto parentIt = objects_.find(cursor->parentId);
        if (parentIt == objects_.end()) {
            return std::nullopt;
        }
        cursor = &parentIt->second;
    }

    TransformMatrix world = identityTransformMatrix();
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        world = multiplyTransformMatrices(world, objectTransformMatrix((*it)->transform));
    }
    return world;
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
    if (sourceIt == meshes_.end() || !sourceIt->second.authoredMesh_) {
        return {};
    }

    const MeshId cloneId = allocateId<MeshId>();
    auto authoredClone = std::make_unique<EditableMesh>(*sourceIt->second.authoredMesh_);
    MeshBlock clone{runtimeId_, cloneId, sourceIt->second.name, std::move(authoredClone), 1};
    meshes_.emplace(cloneId, std::move(clone));

    objectIt->second.meshId = cloneId;
    ++objectIt->second.revision;

    markChanged(DataKind::Mesh, ChangeKind::Created, cloneId.value());
    markChanged(DataKind::Object, ChangeKind::Updated, objectId.value());
    return cloneId;
}

bool Document::executeMeshCommand(
    const MeshId meshId,
    MeshHistory& history,
    MeshCommand& command,
    MeshCommandResult* result) {
    const auto meshIt = meshes_.find(meshId);
    if (meshIt == meshes_.end() || !meshIt->second.authoredMesh_ || !history.bindToDocumentMesh(*this, meshId)) {
        return false;
    }

    MeshCommandResult executionResult;
    if (!history.execute(*meshIt->second.authoredMesh_, command, &executionResult)) {
        return false;
    }

    const bool changed = executionResult.changed;
    if (result != nullptr) {
        *result = std::move(executionResult);
    }

    if (changed) {
        ++meshIt->second.revision;
        ++meshIt->second.evaluationRevision_;
        markChanged(DataKind::Mesh, ChangeKind::Updated, meshId.value());
    }
    return true;
}

bool Document::undoMeshCommand(const MeshId meshId, MeshHistory& history) {
    const auto meshIt = meshes_.find(meshId);
    if (meshIt == meshes_.end() || !meshIt->second.authoredMesh_ || !history.bindToDocumentMesh(*this, meshId)) {
        return false;
    }

    if (!history.undo(*meshIt->second.authoredMesh_)) {
        return false;
    }

    ++meshIt->second.revision;
    ++meshIt->second.evaluationRevision_;
    markChanged(DataKind::Mesh, ChangeKind::Updated, meshId.value());
    return true;
}

bool Document::redoMeshCommand(const MeshId meshId, MeshHistory& history) {
    const auto meshIt = meshes_.find(meshId);
    if (meshIt == meshes_.end() || !meshIt->second.authoredMesh_ || !history.bindToDocumentMesh(*this, meshId)) {
        return false;
    }

    if (!history.redo(*meshIt->second.authoredMesh_)) {
        return false;
    }

    ++meshIt->second.revision;
    ++meshIt->second.evaluationRevision_;
    markChanged(DataKind::Mesh, ChangeKind::Updated, meshId.value());
    return true;
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

ChangeQueryResult Document::changesSince(const std::uint64_t revision) const {
    ChangeQueryResult result;
    result.requestedAfterRevision = revision;
    result.discardedThroughRevision = discardedChangesThroughRevision_;
    result.events.reserve(changes_.size());
    for (const ChangeEvent& event : changes_) {
        if (event.revision > revision) {
            result.events.push_back(event);
        }
    }
    return result;
}

void Document::clearChangeHistory() noexcept {
    if (changeHistoryBatchDepth_ != 0U) {
        pendingClearChangesThroughRevision_ = revision_;
        return;
    }
    changes_.clear();
    discardedChangesThroughRevision_ = revision_;
}

void Document::setChangeHistoryBudgetBytes(const std::size_t budgetBytes) noexcept {
    changeHistoryBudgetBytes_ = budgetBytes;
    if (changeHistoryBatchDepth_ == 0U) {
        enforceChangeHistoryBudget();
    }
}

void Document::resetMovedFrom() noexcept {
    runtimeId_ = allocateRuntimeDocumentId();
    id_ = DocumentId{1};
    nextId_ = 2;
    revision_ = 0;
    scenes_.clear();
    collections_.clear();
    meshes_.clear();
    objects_.clear();
    discardedChangesThroughRevision_ = 0;
    changeHistoryBatchDepth_ = 0;
    pendingClearChangesThroughRevision_ = 0;
    changes_.clear();
}

void Document::markChanged(const DataKind dataKind, const ChangeKind changeKind, const std::uint64_t entityId) {
    ++revision_;
    changes_.push_back(ChangeEvent{revision_, dataKind, changeKind, entityId});
    if (changeHistoryBatchDepth_ == 0U) {
        enforceChangeHistoryBudget();
    }
}

void Document::endChangeHistoryBatch() noexcept {
    if (changeHistoryBatchDepth_ == 0U) {
        return;
    }
    --changeHistoryBatchDepth_;
    if (changeHistoryBatchDepth_ == 0U) {
        if (pendingClearChangesThroughRevision_ != 0U) {
            const std::uint64_t clearThrough = std::min(pendingClearChangesThroughRevision_, revision_);
            while (!changes_.empty() && changes_.front().revision <= clearThrough) {
                changes_.pop_front();
            }
            discardedChangesThroughRevision_ = std::max(discardedChangesThroughRevision_, clearThrough);
            pendingClearChangesThroughRevision_ = 0;
        }
        enforceChangeHistoryBudget();
    }
}

void Document::enforceChangeHistoryBudget() noexcept {
    const std::size_t maximumEvents = changeHistoryBudgetBytes_ / sizeof(ChangeEvent);
    while (changes_.size() > maximumEvents) {
        discardedChangesThroughRevision_ = changes_.front().revision;
        changes_.pop_front();
    }
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
    if (!runtimeId_ || !id_ || nextId_ == 0U || nextId_ == std::numeric_limits<std::uint64_t>::max() ||
        revision_ == std::numeric_limits<std::uint64_t>::max()) {
        return false;
    }

    std::uint64_t maximumPersistentId = id_.value();

    for (const auto& [sceneId, scene] : scenes_) {
        if (!sceneId || scene.id != sceneId || scene.revision == 0U ||
            scene.revision == std::numeric_limits<std::uint64_t>::max()) {
            return false;
        }
        maximumPersistentId = std::max(maximumPersistentId, sceneId.value());
        const auto rootIt = collections_.find(scene.rootCollectionId);
        if (rootIt == collections_.end() || rootIt->second.sceneId != sceneId || rootIt->second.parentId) {
            return false;
        }
    }

    for (const auto& [collectionId, collection] : collections_) {
        if (!collectionId || collection.id != collectionId || collection.revision == 0U ||
            collection.revision == std::numeric_limits<std::uint64_t>::max() || !hasScene(collection.sceneId)) {
            return false;
        }
        maximumPersistentId = std::max(maximumPersistentId, collectionId.value());

        const SceneBlock& owningScene = scenes_.at(collection.sceneId);
        if (!collection.parentId) {
            if (owningScene.rootCollectionId != collectionId) {
                return false;
            }
        } else {
            const auto parentIt = collections_.find(collection.parentId);
            if (parentIt == collections_.end() || parentIt->second.sceneId != collection.sceneId) {
                return false;
            }

            CollectionId cursor = collection.parentId;
            std::size_t visited = 0;
            while (cursor) {
                if (cursor == collectionId) {
                    return false;
                }
                const auto ancestorIt = collections_.find(cursor);
                if (ancestorIt == collections_.end() || ancestorIt->second.sceneId != collection.sceneId) {
                    return false;
                }
                cursor = ancestorIt->second.parentId;
                if (++visited > collections_.size()) {
                    return false;
                }
            }
        }

        for (const ObjectId objectId : collection.objectIds) {
            if (!hasObject(objectId)) {
                return false;
            }
        }
    }

    for (const auto& [meshId, mesh] : meshes_) {
        if (!meshId || mesh.id != meshId || mesh.ownerDocumentRuntimeId_ != runtimeId_ ||
            mesh.revision == 0U || mesh.revision == std::numeric_limits<std::uint64_t>::max() ||
            mesh.evaluationRevision_ == 0U || mesh.evaluationRevision_ == std::numeric_limits<std::uint64_t>::max() ||
            mesh.evaluationRevision_ > mesh.revision ||
            !mesh.authoredMesh_ || !mesh.authoredMesh_->validateStrict()) {
            return false;
        }
        maximumPersistentId = std::max(maximumPersistentId, meshId.value());
    }

    for (const auto& [objectId, object] : objects_) {
        if (!objectId || object.id != objectId || object.revision == 0U ||
            object.revision == std::numeric_limits<std::uint64_t>::max() ||
            !isFiniteObjectTransform(object.transform)) {
            return false;
        }
        maximumPersistentId = std::max(maximumPersistentId, objectId.value());
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

    return nextId_ > maximumPersistentId;
}

} // namespace vortex