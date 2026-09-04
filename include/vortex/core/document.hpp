#pragma once

#include "vortex/core/id.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace vortex {

struct MeshBlock final {
    MeshId id;
    std::string name;
    std::uint64_t revision = 1;
};

struct ObjectBlock final {
    ObjectId id;
    std::string name;
    MeshId meshId;
    ObjectId parentId;
    std::uint64_t revision = 1;
};

class Document final {
public:
    Document();

    [[nodiscard]] DocumentId id() const noexcept { return id_; }

    [[nodiscard]] MeshId createMesh(std::string name);
    [[nodiscard]] ObjectId createObject(std::string name, MeshId meshId = {});

    [[nodiscard]] bool hasMesh(MeshId id) const noexcept;
    [[nodiscard]] bool hasObject(ObjectId id) const noexcept;

    [[nodiscard]] const MeshBlock* mesh(MeshId id) const noexcept;
    [[nodiscard]] const ObjectBlock* object(ObjectId id) const noexcept;

    [[nodiscard]] bool setObjectMesh(ObjectId objectId, MeshId meshId);
    [[nodiscard]] bool setObjectParent(ObjectId objectId, ObjectId parentId);

    [[nodiscard]] bool removeObject(ObjectId id);
    [[nodiscard]] bool removeMesh(MeshId id);

    [[nodiscard]] std::size_t meshCount() const noexcept { return meshes_.size(); }
    [[nodiscard]] std::size_t objectCount() const noexcept { return objects_.size(); }

    [[nodiscard]] bool validate() const noexcept;

private:
    template <typename IdType>
    [[nodiscard]] IdType allocateId() noexcept {
        return IdType{nextId_++};
    }

    [[nodiscard]] bool wouldCreateParentCycle(ObjectId objectId, ObjectId parentId) const noexcept;
    [[nodiscard]] std::size_t meshUserCount(MeshId meshId) const noexcept;

    DocumentId id_;
    std::uint64_t nextId_ = 1;

    std::unordered_map<MeshId, MeshBlock, IdHash<MeshId>> meshes_;
    std::unordered_map<ObjectId, ObjectBlock, IdHash<ObjectId>> objects_;
};

} // namespace vortex
