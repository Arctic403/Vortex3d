#include "vortex/core/editor_history.hpp"

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

} // namespace

std::size_t EditorHistoryRecord::estimatedBytes() const noexcept {
    return sizeof(EditorHistoryRecord) + std::visit(
        [](const auto& value) -> std::size_t {
            using Value = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Value, DocumentHistoryRecord>) {
                return value.estimatedBytes();
            } else {
                return value.estimatedBytes();
            }
        },
        payload);
}

bool EditorHistory::accepts(const Document& document) noexcept {
    if (!document.runtimeId()) {
        return false;
    }
    if (!ownerDocumentRuntimeId_) {
        ownerDocumentRuntimeId_ = document.runtimeId();
        return true;
    }
    return ownerDocumentRuntimeId_ == document.runtimeId();
}

bool EditorHistory::execute(Document& document, Command& command) {
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

    EditorHistoryRecord editorRecord{EditorHistoryPayload{std::move(*record)}};
    if (editorRecord.estimatedBytes() > budgetBytes_ || !accepts(document)) {
        auto& documentRecord = std::get<DocumentHistoryRecord>(editorRecord.payload);
        (void)DocumentHistory::applyRecord(document, documentRecord, false);
        document.revision_ = startRevision;
        document.changes_.resize(startChangeCount);
        return false;
    }

    clearRedo();
    undo_.push_back(std::move(editorRecord));
    recalculateRetainedBytes();
    enforceBudget();
    return true;
}

bool EditorHistory::executeMesh(
    Document& document,
    const MeshId meshId,
    MeshCommand& command,
    MeshCommandResult* result) {
    if (!command.undoable() || (ownerDocumentRuntimeId_ && ownerDocumentRuntimeId_ != document.runtimeId())) {
        return false;
    }

    const auto meshIt = document.meshes_.find(meshId);
    if (meshIt == document.meshes_.end() || !meshIt->second.authoredMesh_) {
        return false;
    }

    const auto execution = command.apply(*meshIt->second.authoredMesh_);
    if (!execution) {
        return false;
    }

    MeshCommandResult executionResult = execution->result;
    executionResult.changed = execution->history.has_value();
    if (result != nullptr) {
        *result = executionResult;
    }
    if (!execution->history) {
        return true;
    }

    EditorHistoryRecord editorRecord{
        EditorHistoryPayload{EditorMeshHistoryRecord{meshId, *execution->history}}};
    if (editorRecord.estimatedBytes() > budgetBytes_ || !accepts(document)) {
        auto& meshRecord = std::get<EditorMeshHistoryRecord>(editorRecord.payload).record;
        (void)MeshHistory::applyRecord(*meshIt->second.authoredMesh_, meshRecord, false);
        if (result != nullptr) {
            *result = {};
        }
        return false;
    }

    clearRedo();
    undo_.push_back(std::move(editorRecord));
    recalculateRetainedBytes();
    enforceBudget();

    ++meshIt->second.revision;
    ++meshIt->second.evaluationRevision_;
    document.markChanged(DataKind::Mesh, ChangeKind::Updated, meshId.value());
    return true;
}

bool EditorHistory::applyRecord(Document& document, EditorHistoryRecord& record, const bool forward) {
    return std::visit(
        [&document, forward](auto& value) -> bool {
            using Value = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Value, DocumentHistoryRecord>) {
                return DocumentHistory::applyRecord(document, value, forward);
            } else {
                const auto meshIt = document.meshes_.find(value.meshId);
                if (meshIt == document.meshes_.end() || !meshIt->second.authoredMesh_) {
                    return false;
                }
                if (!MeshHistory::applyRecord(*meshIt->second.authoredMesh_, value.record, forward)) {
                    return false;
                }
                ++meshIt->second.revision;
                ++meshIt->second.evaluationRevision_;
                document.markChanged(DataKind::Mesh, ChangeKind::Updated, value.meshId.value());
                return true;
            }
        },
        record.payload);
}

bool EditorHistory::undo(Document& document) {
    if (undo_.empty() || ownerDocumentRuntimeId_ != document.runtimeId()) {
        return false;
    }

    EditorHistoryRecord record = std::move(undo_.back());
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

bool EditorHistory::redo(Document& document) {
    if (redo_.empty() || ownerDocumentRuntimeId_ != document.runtimeId()) {
        return false;
    }

    EditorHistoryRecord record = std::move(redo_.back());
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

void EditorHistory::clearRedo() noexcept {
    redo_.clear();
    recalculateRetainedBytes();
}

void EditorHistory::recalculateRetainedBytes() noexcept {
    retainedBytes_ = 0;
    for (const EditorHistoryRecord& record : undo_) {
        retainedBytes_ += record.estimatedBytes();
    }
    for (const EditorHistoryRecord& record : redo_) {
        retainedBytes_ += record.estimatedBytes();
    }
}

void EditorHistory::enforceBudget() noexcept {
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

void EditorHistory::clear() noexcept {
    undo_.clear();
    redo_.clear();
    retainedBytes_ = 0;
    ownerDocumentRuntimeId_ = {};
}

void EditorHistory::setBudgetBytes(const std::size_t budgetBytes) noexcept {
    budgetBytes_ = budgetBytes;
    enforceBudget();
}

} // namespace vortex
