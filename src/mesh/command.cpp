#include "vortex/mesh/command.hpp"

#include <type_traits>
#include <unordered_set>
#include <utility>

namespace vortex {

std::size_t MeshHistoryRecord::estimatedBytes() const noexcept {
    std::size_t bytes = sizeof(MeshHistoryRecord) + name.capacity();
    std::visit([&bytes](const auto& payload) {
        using Payload = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<Payload, VertexPositionHistory>) {
            bytes += sizeof(VertexPositionHistory);
            bytes += payload.changes.capacity() * sizeof(VertexPositionChange);
        }
    }, payload);
    return bytes;
}

MoveVerticesCommand::MoveVerticesCommand(std::vector<VertexPositionTarget> targets)
    : targets_(std::move(targets)) {}

std::optional<MeshCommandExecution> MoveVerticesCommand::apply(EditableMesh& mesh) {
    if (targets_.empty() || !mesh.validate()) {
        return std::nullopt;
    }

    std::unordered_set<VertexId, IdHash<VertexId>> unique;
    VertexPositionHistory history;
    history.changes.reserve(targets_.size());

    for (const VertexPositionTarget& target : targets_) {
        if (!target.vertexId || !unique.insert(target.vertexId).second || !mesh.hasVertex(target.vertexId)) {
            return std::nullopt;
        }

        const auto before = mesh.position(target.vertexId);
        if (!before) {
            return std::nullopt;
        }
        if (*before != target.position) {
            history.changes.push_back(VertexPositionChange{target.vertexId, *before, target.position});
        }
    }

    MeshCommandExecution execution;
    execution.result.touchedVertices.reserve(history.changes.size());

    for (const VertexPositionChange& change : history.changes) {
        if (!mesh.setPosition(change.vertexId, change.after)) {
            for (const VertexPositionChange& rollback : history.changes) {
                if (mesh.hasVertex(rollback.vertexId)) {
                    (void)mesh.setPosition(rollback.vertexId, rollback.before);
                }
            }
            return std::nullopt;
        }
        execution.result.touchedVertices.push_back(change.vertexId);
    }

    if (!mesh.validate()) {
        for (const VertexPositionChange& rollback : history.changes) {
            if (mesh.hasVertex(rollback.vertexId)) {
                (void)mesh.setPosition(rollback.vertexId, rollback.before);
            }
        }
        return std::nullopt;
    }

    if (!history.changes.empty()) {
        execution.history = MeshHistoryRecord{std::string{name()}, MeshHistoryPayload{std::move(history)}};
    }
    return execution;
}

std::optional<MeshCommandExecution> ExtrudeFaceCommand::apply(EditableMesh& mesh) {
    const auto extrusion = mesh.extrudeFace(faceId_, offset_);
    if (!extrusion) {
        return std::nullopt;
    }

    MeshCommandExecution execution;
    execution.result.extrusion = *extrusion;
    execution.result.touchedVertices = extrusion->newVertices;
    return execution;
}

bool MeshHistory::execute(EditableMesh& mesh, MeshCommand& command, MeshCommandResult* result) {
    // Editor/history execution only accepts commands that can produce reversible state.
    if (!command.undoable()) {
        return false;
    }

    const auto execution = command.apply(mesh);
    if (!execution) {
        return false;
    }

    if (result != nullptr) {
        *result = execution->result;
    }

    if (!execution->history) {
        // A successful no-op is valid and should not create a history step.
        return true;
    }

    MeshHistoryRecord record = *execution->history;
    const std::size_t bytes = record.estimatedBytes();
    if (bytes > budgetBytes_) {
        // The command already ran. Rewind it immediately rather than retaining an
        // unbounded step or leaving a non-undoable editor mutation behind.
        const bool rewound = applyRecord(mesh, record, false);
        if (result != nullptr) {
            *result = {};
        }
        return rewound ? false : false;
    }

    clearRedo();
    retainedBytes_ += bytes;
    undo_.push_back(std::move(record));
    enforceBudget();
    return true;
}

bool MeshHistory::undo(EditableMesh& mesh) {
    if (undo_.empty()) {
        return false;
    }

    MeshHistoryRecord record = std::move(undo_.back());
    undo_.pop_back();
    if (!applyRecord(mesh, record, false)) {
        undo_.push_back(std::move(record));
        return false;
    }

    redo_.push_back(std::move(record));
    return true;
}

bool MeshHistory::redo(EditableMesh& mesh) {
    if (redo_.empty()) {
        return false;
    }

    MeshHistoryRecord record = std::move(redo_.back());
    redo_.pop_back();
    if (!applyRecord(mesh, record, true)) {
        redo_.push_back(std::move(record));
        return false;
    }

    undo_.push_back(std::move(record));
    return true;
}

bool MeshHistory::applyRecord(EditableMesh& mesh, const MeshHistoryRecord& record, const bool forward) {
    if (!mesh.validate()) {
        return false;
    }

    return std::visit([&](const auto& payload) -> bool {
        using Payload = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<Payload, VertexPositionHistory>) {
            std::vector<VertexPositionChange> applied;
            applied.reserve(payload.changes.size());

            for (const VertexPositionChange& change : payload.changes) {
                if (!mesh.hasVertex(change.vertexId) || !mesh.position(change.vertexId)) {
                    return false;
                }
            }

            for (const VertexPositionChange& change : payload.changes) {
                const auto current = mesh.position(change.vertexId);
                if (!current) {
                    return false;
                }

                const Vec3 target = forward ? change.after : change.before;
                if (!mesh.setPosition(change.vertexId, target)) {
                    for (auto it = applied.rbegin(); it != applied.rend(); ++it) {
                        (void)mesh.setPosition(it->vertexId, it->before);
                    }
                    return false;
                }
                applied.push_back(VertexPositionChange{change.vertexId, *current, target});
            }

            if (!mesh.validate()) {
                for (auto it = applied.rbegin(); it != applied.rend(); ++it) {
                    (void)mesh.setPosition(it->vertexId, it->before);
                }
                return false;
            }
            return true;
        }
        return false;
    }, record.payload);
}

void MeshHistory::clearRedo() noexcept {
    for (const MeshHistoryRecord& record : redo_) {
        const std::size_t bytes = record.estimatedBytes();
        retainedBytes_ = bytes > retainedBytes_ ? 0 : retainedBytes_ - bytes;
    }
    redo_.clear();
}

void MeshHistory::enforceBudget() noexcept {
    while (retainedBytes_ > budgetBytes_ && !undo_.empty()) {
        const std::size_t bytes = undo_.front().estimatedBytes();
        retainedBytes_ = bytes > retainedBytes_ ? 0 : retainedBytes_ - bytes;
        undo_.pop_front();
    }
}

void MeshHistory::clear() noexcept {
    undo_.clear();
    redo_.clear();
    retainedBytes_ = 0;
}

void MeshHistory::setBudgetBytes(const std::size_t budgetBytes) noexcept {
    budgetBytes_ = budgetBytes;
    enforceBudget();

    while (retainedBytes_ > budgetBytes_ && !redo_.empty()) {
        const std::size_t bytes = redo_.front().estimatedBytes();
        retainedBytes_ = bytes > retainedBytes_ ? 0 : retainedBytes_ - bytes;
        redo_.pop_front();
    }
}

} // namespace vortex
