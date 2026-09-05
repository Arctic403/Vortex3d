#pragma once

#include "vortex/core/command.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace vortex {

class RenameObjectCommand final : public Command {
public:
    RenameObjectCommand(ObjectId objectId, std::string name) : objectId_(objectId), name_(std::move(name)) {}
    [[nodiscard]] std::string_view name() const noexcept override { return "Rename Object"; }

    [[nodiscard]] std::optional<DocumentHistoryRecord> apply(Document& document) override {
        const ObjectBlock* object = document.object(objectId_);
        if (object == nullptr) {
            return std::nullopt;
        }

        const std::string before = object->name;
        if (!document.renameObject(objectId_, name_)) {
            return std::nullopt;
        }

        DocumentHistoryRecord record{std::string{name()}, {}};
        if (before != name_) {
            record.deltas.emplace_back(RenameObjectDelta{objectId_, before, name_});
        }
        return record;
    }

private:
    ObjectId objectId_;
    std::string name_;
};

class SetObjectParentCommand final : public Command {
public:
    SetObjectParentCommand(ObjectId objectId, ObjectId parentId) : objectId_(objectId), parentId_(parentId) {}
    [[nodiscard]] std::string_view name() const noexcept override { return "Set Object Parent"; }

    [[nodiscard]] std::optional<DocumentHistoryRecord> apply(Document& document) override {
        const ObjectBlock* object = document.object(objectId_);
        if (object == nullptr) {
            return std::nullopt;
        }

        const ObjectId before = object->parentId;
        if (!document.setObjectParent(objectId_, parentId_)) {
            return std::nullopt;
        }

        DocumentHistoryRecord record{std::string{name()}, {}};
        if (before != parentId_) {
            record.deltas.emplace_back(SetObjectParentDelta{objectId_, before, parentId_});
        }
        return record;
    }

private:
    ObjectId objectId_;
    ObjectId parentId_;
};

class SetObjectMeshCommand final : public Command {
public:
    SetObjectMeshCommand(ObjectId objectId, MeshId meshId) : objectId_(objectId), meshId_(meshId) {}
    [[nodiscard]] std::string_view name() const noexcept override { return "Set Object Mesh"; }

    [[nodiscard]] std::optional<DocumentHistoryRecord> apply(Document& document) override {
        const ObjectBlock* object = document.object(objectId_);
        if (object == nullptr) {
            return std::nullopt;
        }

        const MeshId before = object->meshId;
        if (!document.setObjectMesh(objectId_, meshId_)) {
            return std::nullopt;
        }

        DocumentHistoryRecord record{std::string{name()}, {}};
        if (before != meshId_) {
            record.deltas.emplace_back(SetObjectMeshDelta{objectId_, before, meshId_});
        }
        return record;
    }

private:
    ObjectId objectId_;
    MeshId meshId_;
};

class SetObjectTransformCommand final : public Command {
public:
    SetObjectTransformCommand(ObjectId objectId, ObjectTransform transform)
        : objectId_(objectId), transform_(transform) {}
    [[nodiscard]] std::string_view name() const noexcept override { return "Set Object Transform"; }

    [[nodiscard]] std::optional<DocumentHistoryRecord> apply(Document& document) override {
        const ObjectBlock* object = document.object(objectId_);
        if (object == nullptr || !isFiniteObjectTransform(transform_)) {
            return std::nullopt;
        }

        const ObjectTransform before = object->transform;
        if (!document.setObjectTransform(objectId_, transform_)) {
            return std::nullopt;
        }

        DocumentHistoryRecord record{std::string{name()}, {}};
        if (!(before == transform_)) {
            record.deltas.emplace_back(SetObjectTransformDelta{objectId_, before, transform_});
        }
        return record;
    }

private:
    ObjectId objectId_;
    ObjectTransform transform_;
};

class MakeObjectMeshUniqueCommand final : public Command {
public:
    explicit MakeObjectMeshUniqueCommand(ObjectId objectId) : objectId_(objectId) {}
    [[nodiscard]] std::string_view name() const noexcept override { return "Make Object Mesh Unique"; }

    [[nodiscard]] std::optional<DocumentHistoryRecord> apply(Document& document) override {
        result_ = {};
        const ObjectBlock* object = document.object(objectId_);
        if (object == nullptr || !object->meshId) {
            return std::nullopt;
        }

        const MeshId sourceMeshId = object->meshId;
        result_ = document.makeObjectMeshUnique(objectId_);
        if (!result_) {
            return std::nullopt;
        }

        DocumentHistoryRecord record{std::string{name()}, {}};
        if (result_ != sourceMeshId) {
            record.deltas.emplace_back(MakeObjectMeshUniqueDelta{objectId_, sourceMeshId, result_, std::nullopt});
        }
        return record;
    }

    [[nodiscard]] MeshId result() const noexcept { return result_; }

private:
    ObjectId objectId_;
    MeshId result_;
};

} // namespace vortex
