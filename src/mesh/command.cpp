#include "vortex/mesh/command.hpp"

#include <algorithm>
#include <span>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace vortex {
namespace {

std::size_t estimatedAttributeRowBytes(const AttributeRow& row) noexcept {
    std::size_t bytes = sizeof(AttributeRow) + row.values.capacity() * sizeof(AttributeRowValue);
    for (const AttributeRowValue& value : row.values) {
        bytes += value.key.name.capacity();
    }
    return bytes;
}

template <typename Snapshot>
std::size_t estimatedSnapshotVectorBytes(const std::vector<Snapshot>& snapshots) noexcept {
    std::size_t bytes = snapshots.capacity() * sizeof(Snapshot);
    for (const Snapshot& snapshot : snapshots) {
        bytes += estimatedAttributeRowBytes(snapshot.attributes);
    }
    return bytes;
}

template <typename IdType>
[[nodiscard]] std::optional<std::size_t> packedIndexOf(const std::span<const IdType> ids, const IdType id) noexcept {
    for (std::size_t index = 0; index < ids.size(); ++index) {
        if (ids[index] == id) {
            return index;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::vector<bool>* ensureBoolAttribute(
    EditableMesh& mesh,
    const std::string_view name,
    const AttributeDomain domain,
    const bool defaultValue) {
    AttributeSet& attributes = mesh.attributes();
    if (!attributes.contains(name, domain)) {
        if (!attributes.create<bool>(std::string{name}, domain, defaultValue)) {
            return nullptr;
        }
    }
    return attributes.values<bool>(name, domain);
}

} // namespace

std::size_t MeshHistoryRecord::estimatedBytes() const noexcept {
    std::size_t bytes = sizeof(MeshHistoryRecord) + name.capacity();
    std::visit([&bytes](const auto& payload) {
        using Payload = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<Payload, VertexPositionHistory>) {
            bytes += sizeof(VertexPositionHistory);
            bytes += payload.changes.capacity() * sizeof(VertexPositionChange);
        } else if constexpr (std::is_same_v<Payload, EdgeSharpHistory> || std::is_same_v<Payload, FaceSharpHistory>) {
            bytes += sizeof(Payload);
        } else if constexpr (std::is_same_v<Payload, FaceExtrudeHistory>) {
            bytes += sizeof(FaceExtrudeHistory);
            bytes += estimatedAttributeRowBytes(payload.sourceFace.attributes);
            bytes += estimatedSnapshotVectorBytes(payload.sourceCorners);
            bytes += estimatedSnapshotVectorBytes(payload.createdVertices);
            bytes += estimatedSnapshotVectorBytes(payload.createdEdges);
            bytes += estimatedSnapshotVectorBytes(payload.createdFaces);
            bytes += estimatedSnapshotVectorBytes(payload.createdCorners);
            bytes += payload.result.sideFaces.capacity() * sizeof(FaceId);
            bytes += payload.result.newVertices.capacity() * sizeof(VertexId);
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

std::optional<MeshCommandExecution> SetEdgeSharpCommand::apply(EditableMesh& mesh) {
    if (!edgeId_ || !mesh.hasEdge(edgeId_) || !mesh.validate()) {
        return std::nullopt;
    }

    const auto packedIndex = packedIndexOf<EdgeId>(mesh.edgeIds(), edgeId_);
    auto* sharpValues = ensureBoolAttribute(mesh, "sharp", AttributeDomain::Edge, false);
    if (!packedIndex || sharpValues == nullptr || *packedIndex >= sharpValues->size()) {
        return std::nullopt;
    }

    const bool before = static_cast<bool>((*sharpValues)[*packedIndex]);
    MeshCommandExecution execution;
    if (before == sharp_) {
        return execution;
    }

    (*sharpValues)[*packedIndex] = sharp_;
    if (!mesh.validate()) {
        (*sharpValues)[*packedIndex] = before;
        return std::nullopt;
    }

    execution.result.touchedEdges.push_back(edgeId_);
    execution.history = MeshHistoryRecord{
        std::string{name()}, MeshHistoryPayload{EdgeSharpHistory{edgeId_, before, sharp_}}};
    return execution;
}

std::optional<MeshCommandExecution> SetFaceSharpCommand::apply(EditableMesh& mesh) {
    if (!faceId_ || !mesh.hasFace(faceId_) || !mesh.validate()) {
        return std::nullopt;
    }

    const auto packedIndex = packedIndexOf<FaceId>(mesh.faceIds(), faceId_);
    auto* sharpValues = ensureBoolAttribute(mesh, "sharp_face", AttributeDomain::Face, true);
    if (!packedIndex || sharpValues == nullptr || *packedIndex >= sharpValues->size()) {
        return std::nullopt;
    }

    const bool before = static_cast<bool>((*sharpValues)[*packedIndex]);
    MeshCommandExecution execution;
    if (before == sharp_) {
        return execution;
    }

    (*sharpValues)[*packedIndex] = sharp_;
    if (!mesh.validate()) {
        (*sharpValues)[*packedIndex] = before;
        return std::nullopt;
    }

    execution.result.touchedFaces.push_back(faceId_);
    execution.history = MeshHistoryRecord{
        std::string{name()}, MeshHistoryPayload{FaceSharpHistory{faceId_, before, sharp_}}};
    return execution;
}

std::optional<MeshCommandExecution> ExtrudeFaceCommand::apply(EditableMesh& mesh) {
    if (!mesh.hasFace(faceId_) || !mesh.validate()) {
        return std::nullopt;
    }

    // Operation-local rollback only. This copy is never retained in MeshHistory.
    EditableMesh rollbackMesh = mesh;

    FaceExtrudeHistory history;
    const auto sourceFaceIndexIt = mesh.faceIndex_.find(faceId_);
    if (sourceFaceIndexIt == mesh.faceIndex_.end()) {
        return std::nullopt;
    }
    const auto sourceFaceRow = mesh.attributes_.captureDomainIndex(AttributeDomain::Face, sourceFaceIndexIt->second);
    if (!sourceFaceRow) {
        return std::nullopt;
    }
    history.sourceFace = FaceTopologySnapshot{mesh.faces_.at(faceId_), sourceFaceIndexIt->second, *sourceFaceRow};

    const std::vector<CornerId> sourceCornerIds = mesh.faceCorners(faceId_);
    if (sourceCornerIds.size() != history.sourceFace.data.cornerCount) {
        return std::nullopt;
    }
    history.sourceCorners.reserve(sourceCornerIds.size());
    for (const CornerId cornerId : sourceCornerIds) {
        const auto cornerIndexIt = mesh.cornerIndex_.find(cornerId);
        if (cornerIndexIt == mesh.cornerIndex_.end()) {
            return std::nullopt;
        }
        const auto row = mesh.attributes_.captureDomainIndex(AttributeDomain::Corner, cornerIndexIt->second);
        if (!row) {
            return std::nullopt;
        }
        history.sourceCorners.push_back(
            CornerTopologySnapshot{mesh.corners_.at(cornerId), cornerIndexIt->second, *row});
    }

    const std::unordered_set<VertexId, IdHash<VertexId>> beforeVertices(mesh.vertexOrder_.begin(), mesh.vertexOrder_.end());
    const std::unordered_set<EdgeId, IdHash<EdgeId>> beforeEdges(mesh.edgeOrder_.begin(), mesh.edgeOrder_.end());
    const std::unordered_set<FaceId, IdHash<FaceId>> beforeFaces(mesh.faceOrder_.begin(), mesh.faceOrder_.end());
    const std::unordered_set<CornerId, IdHash<CornerId>> beforeCorners(mesh.cornerOrder_.begin(), mesh.cornerOrder_.end());

    const auto extrusion = mesh.extrudeFace(faceId_, offset_);
    if (!extrusion) {
        return std::nullopt;
    }
    history.result = *extrusion;

    const auto failAndRollback = [&]() -> std::optional<MeshCommandExecution> {
        mesh = std::move(rollbackMesh);
        return std::nullopt;
    };

    for (const VertexId id : mesh.vertexOrder_) {
        if (beforeVertices.contains(id)) {
            continue;
        }
        const auto indexIt = mesh.vertexIndex_.find(id);
        if (indexIt == mesh.vertexIndex_.end()) {
            return failAndRollback();
        }
        const auto row = mesh.attributes_.captureDomainIndex(AttributeDomain::Vertex, indexIt->second);
        if (!row) {
            return failAndRollback();
        }
        history.createdVertices.push_back(VertexTopologySnapshot{mesh.vertices_.at(id), indexIt->second, *row});
    }

    for (const EdgeId id : mesh.edgeOrder_) {
        if (beforeEdges.contains(id)) {
            continue;
        }
        const auto indexIt = mesh.edgeIndex_.find(id);
        if (indexIt == mesh.edgeIndex_.end()) {
            return failAndRollback();
        }
        const auto row = mesh.attributes_.captureDomainIndex(AttributeDomain::Edge, indexIt->second);
        if (!row) {
            return failAndRollback();
        }
        history.createdEdges.push_back(EdgeTopologySnapshot{mesh.edges_.at(id), indexIt->second, *row});
    }

    for (const FaceId id : mesh.faceOrder_) {
        if (beforeFaces.contains(id)) {
            continue;
        }
        const auto indexIt = mesh.faceIndex_.find(id);
        if (indexIt == mesh.faceIndex_.end()) {
            return failAndRollback();
        }
        const auto row = mesh.attributes_.captureDomainIndex(AttributeDomain::Face, indexIt->second);
        if (!row) {
            return failAndRollback();
        }
        history.createdFaces.push_back(FaceTopologySnapshot{mesh.faces_.at(id), indexIt->second, *row});
    }

    for (const CornerId id : mesh.cornerOrder_) {
        if (beforeCorners.contains(id)) {
            continue;
        }
        const auto indexIt = mesh.cornerIndex_.find(id);
        if (indexIt == mesh.cornerIndex_.end()) {
            return failAndRollback();
        }
        const auto row = mesh.attributes_.captureDomainIndex(AttributeDomain::Corner, indexIt->second);
        if (!row) {
            return failAndRollback();
        }
        history.createdCorners.push_back(CornerTopologySnapshot{mesh.corners_.at(id), indexIt->second, *row});
    }

    if (!mesh.validate()) {
        return failAndRollback();
    }

    MeshCommandExecution execution;
    execution.result.extrusion = *extrusion;
    execution.result.touchedVertices = extrusion->newVertices;
    execution.history = MeshHistoryRecord{std::string{name()}, MeshHistoryPayload{std::move(history)}};
    return execution;
}

bool MeshHistory::execute(EditableMesh& mesh, MeshCommand& command, MeshCommandResult* result) {
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
        return true;
    }

    MeshHistoryRecord record = *execution->history;
    const std::size_t bytes = record.estimatedBytes();
    if (bytes > budgetBytes_) {
        (void)applyRecord(mesh, record, false);
        if (result != nullptr) {
            *result = {};
        }
        return false;
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
        } else if constexpr (std::is_same_v<Payload, EdgeSharpHistory>) {
            if (!mesh.hasEdge(payload.edgeId)) {
                return false;
            }
            const auto packedIndex = packedIndexOf<EdgeId>(mesh.edgeIds(), payload.edgeId);
            auto* sharpValues = ensureBoolAttribute(mesh, "sharp", AttributeDomain::Edge, false);
            if (!packedIndex || sharpValues == nullptr || *packedIndex >= sharpValues->size()) {
                return false;
            }
            const bool target = forward ? payload.after : payload.before;
            const bool rollback = static_cast<bool>((*sharpValues)[*packedIndex]);
            (*sharpValues)[*packedIndex] = target;
            if (!mesh.validate()) {
                (*sharpValues)[*packedIndex] = rollback;
                return false;
            }
            return true;
        } else if constexpr (std::is_same_v<Payload, FaceSharpHistory>) {
            if (!mesh.hasFace(payload.faceId)) {
                return false;
            }
            const auto packedIndex = packedIndexOf<FaceId>(mesh.faceIds(), payload.faceId);
            auto* sharpValues = ensureBoolAttribute(mesh, "sharp_face", AttributeDomain::Face, true);
            if (!packedIndex || sharpValues == nullptr || *packedIndex >= sharpValues->size()) {
                return false;
            }
            const bool target = forward ? payload.after : payload.before;
            const bool rollback = static_cast<bool>((*sharpValues)[*packedIndex]);
            (*sharpValues)[*packedIndex] = target;
            if (!mesh.validate()) {
                (*sharpValues)[*packedIndex] = rollback;
                return false;
            }
            return true;
        } else if constexpr (std::is_same_v<Payload, FaceExtrudeHistory>) {
            // Temporary operation-local rollback. It is never retained in the history record.
            EditableMesh rollbackMesh = mesh;
            const auto fail = [&]() -> bool {
                mesh = std::move(rollbackMesh);
                return false;
            };

            const auto rebuildAll = [&]() {
                mesh.rebuildVertexIndex();
                mesh.rebuildEdgeIndex();
                mesh.rebuildFaceIndex();
                mesh.rebuildCornerIndex();
                for (const EdgeId edgeId : mesh.edgeOrder_) {
                    mesh.rebuildRadialCycle(edgeId);
                }
            };

            const auto restoreVertex = [&](const VertexTopologySnapshot& snapshot) -> bool {
                if (!snapshot.data.id || mesh.vertices_.contains(snapshot.data.id) ||
                    snapshot.packedIndex > mesh.vertexOrder_.size()) {
                    return false;
                }
                if (!mesh.attributes_.insertDomainIndex(AttributeDomain::Vertex, snapshot.packedIndex, snapshot.attributes)) {
                    return false;
                }
                mesh.vertexOrder_.insert(
                    mesh.vertexOrder_.begin() + static_cast<std::ptrdiff_t>(snapshot.packedIndex), snapshot.data.id);
                return mesh.vertices_.emplace(snapshot.data.id, snapshot.data).second;
            };

            const auto restoreEdge = [&](const EdgeTopologySnapshot& snapshot) -> bool {
                if (!snapshot.data.id || mesh.edges_.contains(snapshot.data.id) ||
                    snapshot.packedIndex > mesh.edgeOrder_.size()) {
                    return false;
                }
                if (!mesh.attributes_.insertDomainIndex(AttributeDomain::Edge, snapshot.packedIndex, snapshot.attributes)) {
                    return false;
                }
                mesh.edgeOrder_.insert(
                    mesh.edgeOrder_.begin() + static_cast<std::ptrdiff_t>(snapshot.packedIndex), snapshot.data.id);
                return mesh.edges_.emplace(snapshot.data.id, snapshot.data).second;
            };

            const auto restoreFace = [&](const FaceTopologySnapshot& snapshot) -> bool {
                if (!snapshot.data.id || mesh.faces_.contains(snapshot.data.id) ||
                    snapshot.packedIndex > mesh.faceOrder_.size()) {
                    return false;
                }
                if (!mesh.attributes_.insertDomainIndex(AttributeDomain::Face, snapshot.packedIndex, snapshot.attributes)) {
                    return false;
                }
                mesh.faceOrder_.insert(
                    mesh.faceOrder_.begin() + static_cast<std::ptrdiff_t>(snapshot.packedIndex), snapshot.data.id);
                return mesh.faces_.emplace(snapshot.data.id, snapshot.data).second;
            };

            const auto restoreCorner = [&](const CornerTopologySnapshot& snapshot) -> bool {
                if (!snapshot.data.id || mesh.corners_.contains(snapshot.data.id) ||
                    snapshot.packedIndex > mesh.cornerOrder_.size()) {
                    return false;
                }
                if (!mesh.attributes_.insertDomainIndex(AttributeDomain::Corner, snapshot.packedIndex, snapshot.attributes)) {
                    return false;
                }
                mesh.cornerOrder_.insert(
                    mesh.cornerOrder_.begin() + static_cast<std::ptrdiff_t>(snapshot.packedIndex), snapshot.data.id);
                return mesh.corners_.emplace(snapshot.data.id, snapshot.data).second;
            };

            const auto orderedCornerPointers = [](const std::vector<CornerTopologySnapshot>& snapshots) {
                std::vector<const CornerTopologySnapshot*> ordered;
                ordered.reserve(snapshots.size());
                for (const CornerTopologySnapshot& snapshot : snapshots) {
                    ordered.push_back(&snapshot);
                }
                std::sort(ordered.begin(), ordered.end(), [](const auto* left, const auto* right) {
                    return left->packedIndex < right->packedIndex;
                });
                return ordered;
            };

            if (!forward) {
                if (mesh.hasFace(payload.sourceFace.data.id)) {
                    return false;
                }

                for (const FaceTopologySnapshot& snapshot : payload.createdFaces) {
                    if (!mesh.hasFace(snapshot.data.id) || !mesh.removeFace(snapshot.data.id, false)) {
                        return fail();
                    }
                }
                for (const EdgeTopologySnapshot& snapshot : payload.createdEdges) {
                    if (!mesh.hasEdge(snapshot.data.id) || !mesh.removeEdge(snapshot.data.id)) {
                        return fail();
                    }
                }
                for (const VertexTopologySnapshot& snapshot : payload.createdVertices) {
                    if (!mesh.hasVertex(snapshot.data.id) || !mesh.removeVertex(snapshot.data.id)) {
                        return fail();
                    }
                }

                if (!restoreFace(payload.sourceFace)) {
                    return fail();
                }
                for (const CornerTopologySnapshot* snapshot : orderedCornerPointers(payload.sourceCorners)) {
                    if (!restoreCorner(*snapshot)) {
                        return fail();
                    }
                }
                rebuildAll();
                return mesh.validate() ? true : fail();
            }

            if (!mesh.hasFace(payload.sourceFace.data.id) || !mesh.removeFace(payload.sourceFace.data.id, false)) {
                return fail();
            }

            for (const VertexTopologySnapshot& snapshot : payload.createdVertices) {
                if (!restoreVertex(snapshot)) {
                    return fail();
                }
            }
            for (const EdgeTopologySnapshot& snapshot : payload.createdEdges) {
                if (!restoreEdge(snapshot)) {
                    return fail();
                }
            }
            for (const FaceTopologySnapshot& snapshot : payload.createdFaces) {
                if (!restoreFace(snapshot)) {
                    return fail();
                }
            }
            for (const CornerTopologySnapshot* snapshot : orderedCornerPointers(payload.createdCorners)) {
                if (!restoreCorner(*snapshot)) {
                    return fail();
                }
            }

            rebuildAll();
            return mesh.validate() ? true : fail();
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
