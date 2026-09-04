#pragma once

#include "vortex/core/command.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace vortex {

class RenameObjectCommand final : public Command {
public:
    RenameObjectCommand(ObjectId objectId, std::string name) : objectId_(objectId), name_(std::move(name)) {}
    [[nodiscard]] std::string_view name() const noexcept override { return "Rename Object"; }
    [[nodiscard]] bool apply(Document& document) override { return document.renameObject(objectId_, name_); }

private:
    ObjectId objectId_;
    std::string name_;
};

class SetObjectParentCommand final : public Command {
public:
    SetObjectParentCommand(ObjectId objectId, ObjectId parentId) : objectId_(objectId), parentId_(parentId) {}
    [[nodiscard]] std::string_view name() const noexcept override { return "Set Object Parent"; }
    [[nodiscard]] bool apply(Document& document) override { return document.setObjectParent(objectId_, parentId_); }

private:
    ObjectId objectId_;
    ObjectId parentId_;
};

class SetObjectMeshCommand final : public Command {
public:
    SetObjectMeshCommand(ObjectId objectId, MeshId meshId) : objectId_(objectId), meshId_(meshId) {}
    [[nodiscard]] std::string_view name() const noexcept override { return "Set Object Mesh"; }
    [[nodiscard]] bool apply(Document& document) override { return document.setObjectMesh(objectId_, meshId_); }

private:
    ObjectId objectId_;
    MeshId meshId_;
};

class MakeObjectMeshUniqueCommand final : public Command {
public:
    explicit MakeObjectMeshUniqueCommand(ObjectId objectId) : objectId_(objectId) {}
    [[nodiscard]] std::string_view name() const noexcept override { return "Make Object Mesh Unique"; }
    [[nodiscard]] bool apply(Document& document) override {
        result_ = document.makeObjectMeshUnique(objectId_);
        return static_cast<bool>(result_);
    }
    [[nodiscard]] MeshId result() const noexcept { return result_; }

private:
    ObjectId objectId_;
    MeshId result_;
};

} // namespace vortex
