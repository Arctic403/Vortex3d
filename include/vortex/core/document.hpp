#pragma once

#include "vortex/core/id.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vortex {

class EditableMesh;
class MeshCommand;
class MeshHistory;
struct MeshCommandResult;
class DocumentHistory;
class Transaction;

class MeshBlock final {
public:
    MeshBlock(MeshId id, std::string name, std::unique_ptr<EditableMesh> authoredMesh, std::uint64_t revision = 1);
    ~MeshBlock();

    MeshBlock(MeshBlock&&) noexcept;
    MeshBlock& operator=(MeshBlock&&) noexcept;
    MeshBlock(const MeshBlock&) = delete;
    MeshBlock& operator=(const MeshBlock&) = delete;

    [[nodiscard]] const EditableMesh* authoredMesh() const noexcept { return authoredMesh_.get(); }

    MeshId id;
    std::string name;
    std::uint64_t revision = 1;

private:
    friend class Document;

    std::unique_ptr<EditableMesh> authoredMesh_;
};

enum class DataKind : std::uint8_t {
    Document,
    Scene,
    Collection,
    Object,
    Mesh,
};

enum class ChangeKind : std::uint8_t {
    Created,
    Updated,
    Removed,
    Linked,
    Unlinked,
};

struct ChangeEvent final {
    std::uint64_t revision = 0;
    DataKind dataKind = DataKind::Document;
    ChangeKind changeKind = ChangeKind::Updated;
    std::uint64_t entityId = 0;
};

struct ObjectBlock final {
    ObjectId id;
    std::string name;
    MeshId meshId;
    ObjectId parentId;
    std::uint64_t revision = 1;
};

struct CollectionBlock final {
    CollectionId id;
    std::string name;
    SceneId sceneId;
    CollectionId parentId;
    std::unordered_set<ObjectId, IdHash<ObjectId>> objectIds;
    std::uint64_t revision = 1;
};

struct SceneBlock final {
    SceneId id;
    std::string name;
    CollectionId rootCollectionId;
    std::uint64_t revision = 1;
};

class Document final {
public:
    Document();
    ~Document() = default;

    Document(Document&&) noexcept = default;
    Document& operator=(Document&&) noexcept = default;
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    [[nodiscard]] DocumentId id() const noexcept { return id_; }
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

    [[nodiscard]] SceneId createScene(std::string name);
    [[nodiscard]] CollectionId createCollection(SceneId sceneId, std::string name, CollectionId parentId = {});
    [[nodiscard]] MeshId createMesh(std::string name);
    [[nodiscard]] MeshId createMesh(std::string name, EditableMesh authoredMesh);
    [[nodiscard]] ObjectId createObject(std::string name, MeshId meshId = {});

    [[nodiscard]] bool hasScene(SceneId id) const noexcept;
    [[nodiscard]] bool hasCollection(CollectionId id) const noexcept;
    [[nodiscard]] bool hasMesh(MeshId id) const noexcept;
    [[nodiscard]] bool hasObject(ObjectId id) const noexcept;

    [[nodiscard]] const SceneBlock* scene(SceneId id) const noexcept;
    [[nodiscard]] const CollectionBlock* collection(CollectionId id) const noexcept;
    [[nodiscard]] const MeshBlock* mesh(MeshId id) const noexcept;
    [[nodiscard]] const ObjectBlock* object(ObjectId id) const noexcept;
    [[nodiscard]] const EditableMesh* authoredMesh(MeshId id) const noexcept;

    [[nodiscard]] bool renameScene(SceneId sceneId, std::string name);
    [[nodiscard]] bool renameCollection(CollectionId collectionId, std::string name);
    [[nodiscard]] bool renameMesh(MeshId meshId, std::string name);
    [[nodiscard]] bool renameObject(ObjectId objectId, std::string name);

    [[nodiscard]] bool setObjectMesh(ObjectId objectId, MeshId meshId);
    [[nodiscard]] bool setObjectParent(ObjectId objectId, ObjectId parentId);
    [[nodiscard]] MeshId makeObjectMeshUnique(ObjectId objectId);

    [[nodiscard]] bool executeMeshCommand(
        MeshId meshId,
        MeshHistory& history,
        MeshCommand& command,
        MeshCommandResult* result = nullptr);
    [[nodiscard]] bool undoMeshCommand(MeshId meshId, MeshHistory& history);
    [[nodiscard]] bool redoMeshCommand(MeshId meshId, MeshHistory& history);

    [[nodiscard]] bool linkObjectToCollection(ObjectId objectId, CollectionId collectionId);
    [[nodiscard]] bool unlinkObjectFromCollection(ObjectId objectId, CollectionId collectionId);

    [[nodiscard]] bool removeObject(ObjectId id);
    [[nodiscard]] bool removeMesh(MeshId id);

    [[nodiscard]] std::size_t sceneCount() const noexcept { return scenes_.size(); }
    [[nodiscard]] std::size_t collectionCount() const noexcept { return collections_.size(); }
    [[nodiscard]] std::size_t meshCount() const noexcept { return meshes_.size(); }
    [[nodiscard]] std::size_t objectCount() const noexcept { return objects_.size(); }
    [[nodiscard]] std::size_t meshUserCount(MeshId meshId) const noexcept;

    [[nodiscard]] std::vector<ChangeEvent> changesSince(std::uint64_t revision) const;
    void clearChangeHistory() noexcept { changes_.clear(); }

    [[nodiscard]] bool validate() const noexcept;

private:
    friend class DocumentHistory;
    friend class Transaction;

    template <typename IdType>
    [[nodiscard]] IdType allocateId() noexcept {
        return IdType{nextId_++};
    }

    void markChanged(DataKind dataKind, ChangeKind changeKind, std::uint64_t entityId);
    [[nodiscard]] bool wouldCreateParentCycle(ObjectId objectId, ObjectId parentId) const noexcept;
    [[nodiscard]] bool collectionBelongsToScene(CollectionId collectionId, SceneId sceneId) const noexcept;

    DocumentId id_;
    std::uint64_t nextId_ = 1;
    std::uint64_t revision_ = 0;

    std::unordered_map<SceneId, SceneBlock, IdHash<SceneId>> scenes_;
    std::unordered_map<CollectionId, CollectionBlock, IdHash<CollectionId>> collections_;
    std::unordered_map<MeshId, MeshBlock, IdHash<MeshId>> meshes_;
    std::unordered_map<ObjectId, ObjectBlock, IdHash<ObjectId>> objects_;
    std::vector<ChangeEvent> changes_;
};

} // namespace vortex
