#include "vortex/core/command.hpp"

#include "vortex/mesh/editable_mesh.hpp"

#include <type_traits>
#include <utility>

namespace vortex {
namespace {

template <typename Function>
class ScopeExit final {
public:
    explicit ScopeExit(Function function) : function_(std::move(function)) {}
    ~ScopeExit() { function_(); }

    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;

private:
    Function function_;
};

std::size_t estimatedMeshBlockDynamicBytes(const MeshBlock& mesh) noexcept {
    std::size_t bytes = mesh.name.capacity();
    const EditableMesh* authoredMesh = mesh.authoredMesh();
    if (authoredMesh == nullptr) {
        return bytes;
    }

    const EditableMesh& authored = *authoredMesh;
    bytes += sizeof(EditableMesh);
    bytes += authored.attributes().estimatedDynamicBytes();

    // Conservative logical-storage estimate. Exact unordered-map node/allocator cost is
    // implementation-specific and is measured by the benchmark/memory stage instead.
    bytes += authored.vertexCount() * (sizeof(VertexId) + sizeof(MeshVertex) + sizeof(std::size_t) + 6U * sizeof(void*));
    bytes += authored.edgeCount() * (sizeof(EdgeId) + sizeof(MeshEdge) + sizeof(std::size_t) + 6U * sizeof(void*));
    bytes += authored.faceCount() * (sizeof(FaceId) + sizeof(MeshFace) + sizeof(std::size_t) + 6U * sizeof(void*));
    bytes += authored.cornerCount() * (sizeof(CornerId) + sizeof(MeshCorner) + sizeof(std::size_t) + 6U * sizeof(void*));
    return bytes;
}

} // namespace

std::size_t DocumentHistoryRecord::estimatedBytes() const noexcept {
    std::size_t bytes = sizeof(DocumentHistoryRecord) + name.capacity();
    bytes += deltas.capacity() * sizeof(DocumentDelta);

    for (const DocumentDelta& delta : deltas) {
        std::visit(
            [&bytes](const auto& value) {
                using Delta = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Delta, RenameObjectDelta>) {
                    bytes += value.before.capacity() + value.after.capacity();
                } else if constexpr (std::is_same_v<Delta, MakeObjectMeshUniqueDelta>) {
                    if (value.detachedMesh) {
                        bytes += estimatedMeshBlockDynamicBytes(*value.detachedMesh);
                    }
                }
            },
            delta);
    }
    return bytes;
}

bool DocumentHistory::accepts(const Document& document) noexcept {
    if (!document.runtimeId()) {
        return false;
    }
    if (!ownerDocumentRuntimeId_) {
        ownerDocumentRuntimeId_ = document.runtimeId();
        return true;
    }
    return ownerDocumentRuntimeId_ == document.runtimeId();
}

bool DocumentHistory::applyRecord(Document& document, DocumentHistoryRecord& record, const bool forward) {
    document.beginChangeHistoryBatch();
    const ScopeExit endBatch{[&document]() noexcept { document.endChangeHistoryBatch(); }};
    const std::uint64_t startRevision = document.revision_;
    const std::size_t startChangeCount = document.changes_.size();
    std::vector<std::size_t> applied;
    applied.reserve(record.deltas.size());

    const auto applyDelta = [&document](DocumentDelta& delta, const bool direction) -> bool {
        return std::visit(
            [&document, direction](auto& value) -> bool {
                using Delta = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Delta, RenameObjectDelta>) {
                    return document.renameObject(value.objectId, direction ? value.after : value.before);
                } else if constexpr (std::is_same_v<Delta, SetObjectParentDelta>) {
                    return document.setObjectParent(value.objectId, direction ? value.after : value.before);
                } else if constexpr (std::is_same_v<Delta, SetObjectMeshDelta>) {
                    return document.setObjectMesh(value.objectId, direction ? value.after : value.before);
                } else if constexpr (std::is_same_v<Delta, MakeObjectMeshUniqueDelta>) {
                    auto objectIt = document.objects_.find(value.objectId);
                    if (objectIt == document.objects_.end() || !document.hasMesh(value.sourceMeshId)) {
                        return false;
                    }

                    if (!direction) {
                        auto meshIt = document.meshes_.find(value.uniqueMeshId);
                        if (meshIt == document.meshes_.end() || value.detachedMesh ||
                            objectIt->second.meshId != value.uniqueMeshId || document.meshUserCount(value.uniqueMeshId) != 1) {
                            return false;
                        }

                        if (!document.setObjectMesh(value.objectId, value.sourceMeshId)) {
                            return false;
                        }
                        value.detachedMesh.emplace(std::move(meshIt->second));
                        document.meshes_.erase(meshIt);
                        document.markChanged(DataKind::Mesh, ChangeKind::Removed, value.uniqueMeshId.value());
                        return true;
                    }

                    if (document.hasMesh(value.uniqueMeshId) || !value.detachedMesh ||
                        objectIt->second.meshId != value.sourceMeshId) {
                        return false;
                    }

                    auto [meshIt, inserted] = document.meshes_.emplace(value.uniqueMeshId, std::move(*value.detachedMesh));
                    if (!inserted) {
                        return false;
                    }
                    value.detachedMesh.reset();
                    document.markChanged(DataKind::Mesh, ChangeKind::Created, value.uniqueMeshId.value());
                    if (!document.setObjectMesh(value.objectId, value.uniqueMeshId)) {
                        return false;
                    }
                    (void)meshIt;
                    return true;
                }
                return false;
            },
            delta);
    };

    const auto rollbackApplied = [&]() {
        for (auto it = applied.rbegin(); it != applied.rend(); ++it) {
            (void)applyDelta(record.deltas[*it], !forward);
        }
        document.revision_ = startRevision;
        document.changes_.resize(startChangeCount);
    };

    if (forward) {
        for (std::size_t index = 0; index < record.deltas.size(); ++index) {
            if (!applyDelta(record.deltas[index], true)) {
                rollbackApplied();
                return false;
            }
            applied.push_back(index);
        }
    } else {
        for (std::size_t index = record.deltas.size(); index > 0; --index) {
            const std::size_t deltaIndex = index - 1;
            if (!applyDelta(record.deltas[deltaIndex], false)) {
                rollbackApplied();
                return false;
            }
            applied.push_back(deltaIndex);
        }
    }

    return true;
}

bool DocumentHistory::execute(Document& document, Command& command) {
    document.beginChangeHistoryBatch();
    const ScopeExit endBatch{[&document]() noexcept { document.endChangeHistoryBatch(); }};
    if (ownerDocumentRuntimeId_ && ownerDocumentRuntimeId_ != document.runtimeId()) {
        return false;
    }

    const std::uint64_t startRevision = document.revision_;
    const std::size_t startChangeCount = document.changes_.size();
    auto record = command.apply(document);
    if (!record) {
        return false;
    }

    if (record->deltas.empty()) {
        return true;
    }

    const std::size_t bytes = record->estimatedBytes();
    if (bytes > budgetBytes_) {
        if (!applyRecord(document, *record, false)) {
            return false;
        }
        document.revision_ = startRevision;
        document.changes_.resize(startChangeCount);
        return false;
    }

    if (!accepts(document)) {
        (void)applyRecord(document, *record, false);
        document.revision_ = startRevision;
        document.changes_.resize(startChangeCount);
        return false;
    }

    clearRedo();
    undo_.push_back(std::move(*record));
    recalculateRetainedBytes();
    enforceBudget();
    return true;
}

bool DocumentHistory::undo(Document& document) {
    if (undo_.empty() || ownerDocumentRuntimeId_ != document.runtimeId()) {
        return false;
    }

    DocumentHistoryRecord record = std::move(undo_.back());
    undo_.pop_back();
    if (!applyRecord(document, record, false)) {
        undo_.push_back(std::move(record));
        recalculateRetainedBytes();
        return false;
    }

    redo_.push_back(std::move(record));
    recalculateRetainedBytes();
    enforceBudget();
    return true;
}

bool DocumentHistory::redo(Document& document) {
    if (redo_.empty() || ownerDocumentRuntimeId_ != document.runtimeId()) {
        return false;
    }

    DocumentHistoryRecord record = std::move(redo_.back());
    redo_.pop_back();
    if (!applyRecord(document, record, true)) {
        redo_.push_back(std::move(record));
        recalculateRetainedBytes();
        return false;
    }

    undo_.push_back(std::move(record));
    recalculateRetainedBytes();
    enforceBudget();
    return true;
}

void DocumentHistory::clearRedo() noexcept {
    redo_.clear();
    recalculateRetainedBytes();
}

void DocumentHistory::recalculateRetainedBytes() noexcept {
    retainedBytes_ = 0;
    for (const DocumentHistoryRecord& record : undo_) {
        retainedBytes_ += record.estimatedBytes();
    }
    for (const DocumentHistoryRecord& record : redo_) {
        retainedBytes_ += record.estimatedBytes();
    }
}

void DocumentHistory::enforceBudget() noexcept {
    recalculateRetainedBytes();
    while (retainedBytes_ > budgetBytes_ && !undo_.empty()) {
        undo_.pop_front();
        recalculateRetainedBytes();
    }
    while (retainedBytes_ > budgetBytes_ && !redo_.empty()) {
        redo_.pop_front();
        recalculateRetainedBytes();
    }
}

void DocumentHistory::clear() noexcept {
    undo_.clear();
    redo_.clear();
    retainedBytes_ = 0;
    ownerDocumentRuntimeId_ = {};
}

void DocumentHistory::setBudgetBytes(const std::size_t budgetBytes) noexcept {
    budgetBytes_ = budgetBytes;
    enforceBudget();
}

Transaction::Transaction(Document& document)
    : document_(document), startRevision_(document.revision_), startChangeCount_(document.changes_.size()) {
    document_.beginChangeHistoryBatch();
}

Transaction::~Transaction() {
    if (active_) {
        rollback();
    }
}

bool Transaction::execute(Command& command) {
    if (!active_ || failed_) {
        return false;
    }

    auto record = command.apply(document_);
    if (!record) {
        failed_ = true;
        rollback();
        return false;
    }

    if (!record->deltas.empty()) {
        records_.push_back(std::move(*record));
    }
    return true;
}

bool Transaction::commit() {
    if (!active_ || failed_) {
        return false;
    }

    records_.clear();
    active_ = false;
    document_.endChangeHistoryBatch();
    return true;
}

void Transaction::rollback() {
    if (!active_) {
        return;
    }

    for (auto it = records_.rbegin(); it != records_.rend(); ++it) {
        if (!DocumentHistory::applyRecord(document_, *it, false)) {
            failed_ = true;
            break;
        }
    }

    document_.revision_ = startRevision_;
    document_.changes_.resize(startChangeCount_);
    records_.clear();
    active_ = false;
    document_.endChangeHistoryBatch();
}

} // namespace vortex
