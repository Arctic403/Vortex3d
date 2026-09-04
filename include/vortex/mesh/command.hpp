#pragma once

#include "vortex/mesh/editable_mesh.hpp"

#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace vortex {

struct VertexPositionTarget final {
    VertexId vertexId;
    Vec3 position;
};

struct VertexPositionChange final {
    VertexId vertexId;
    Vec3 before;
    Vec3 after;
};

struct VertexPositionHistory final {
    std::vector<VertexPositionChange> changes;
};

using MeshHistoryPayload = std::variant<VertexPositionHistory>;

struct MeshHistoryRecord final {
    std::string name;
    MeshHistoryPayload payload;

    [[nodiscard]] std::size_t estimatedBytes() const noexcept;
};

struct MeshCommandResult final {
    std::vector<VertexId> touchedVertices;
    std::optional<FaceExtrudeResult> extrusion;
};

struct MeshCommandExecution final {
    MeshCommandResult result;
    std::optional<MeshHistoryRecord> history;
};

class MeshCommand {
public:
    virtual ~MeshCommand() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual bool undoable() const noexcept = 0;
    [[nodiscard]] virtual std::optional<MeshCommandExecution> apply(EditableMesh& mesh) = 0;
};

class MoveVerticesCommand final : public MeshCommand {
public:
    explicit MoveVerticesCommand(std::vector<VertexPositionTarget> targets);

    [[nodiscard]] std::string_view name() const noexcept override { return "Move Vertices"; }
    [[nodiscard]] bool undoable() const noexcept override { return true; }
    [[nodiscard]] std::optional<MeshCommandExecution> apply(EditableMesh& mesh) override;

private:
    std::vector<VertexPositionTarget> targets_;
};

class ExtrudeFaceCommand final : public MeshCommand {
public:
    ExtrudeFaceCommand(FaceId faceId, Vec3 offset) : faceId_(faceId), offset_(offset) {}

    [[nodiscard]] std::string_view name() const noexcept override { return "Extrude Face"; }

    // Execution is command-routed already, but topology history is not compact yet.
    // MeshHistory refuses this command until a topology delta record is implemented.
    [[nodiscard]] bool undoable() const noexcept override { return false; }
    [[nodiscard]] std::optional<MeshCommandExecution> apply(EditableMesh& mesh) override;

private:
    FaceId faceId_;
    Vec3 offset_;
};

class MeshHistory final {
public:
    explicit MeshHistory(std::size_t budgetBytes = 8U * 1024U * 1024U) : budgetBytes_(budgetBytes) {}

    [[nodiscard]] bool execute(EditableMesh& mesh, MeshCommand& command, MeshCommandResult* result = nullptr);
    [[nodiscard]] bool undo(EditableMesh& mesh);
    [[nodiscard]] bool redo(EditableMesh& mesh);

    void clear() noexcept;
    void setBudgetBytes(std::size_t budgetBytes) noexcept;

    [[nodiscard]] std::size_t budgetBytes() const noexcept { return budgetBytes_; }
    [[nodiscard]] std::size_t retainedBytes() const noexcept { return retainedBytes_; }
    [[nodiscard]] std::size_t undoCount() const noexcept { return undo_.size(); }
    [[nodiscard]] std::size_t redoCount() const noexcept { return redo_.size(); }

private:
    [[nodiscard]] bool applyRecord(EditableMesh& mesh, const MeshHistoryRecord& record, bool forward);
    void clearRedo() noexcept;
    void enforceBudget() noexcept;

    std::size_t budgetBytes_ = 0;
    std::size_t retainedBytes_ = 0;
    std::deque<MeshHistoryRecord> undo_;
    std::deque<MeshHistoryRecord> redo_;
};

} // namespace vortex
