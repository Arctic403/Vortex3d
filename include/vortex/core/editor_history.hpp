#pragma once

#include "vortex/core/command.hpp"
#include "vortex/mesh/command.hpp"

#include <cstddef>
#include <deque>
#include <variant>

namespace vortex {

struct EditorMeshHistoryRecord final {
    MeshId meshId;
    MeshHistoryRecord record;

    [[nodiscard]] std::size_t estimatedBytes() const noexcept {
        return sizeof(EditorMeshHistoryRecord) + record.estimatedBytes();
    }
};

using EditorHistoryPayload = std::variant<DocumentHistoryRecord, EditorMeshHistoryRecord>;

struct EditorHistoryRecord final {
    EditorHistoryPayload payload;

    [[nodiscard]] std::size_t estimatedBytes() const noexcept;
};

// One chronological undo/redo timeline for editor-visible authored mutations.
// Document commands and mesh commands share the same byte budget and ordering.
class EditorHistory final {
public:
    explicit EditorHistory(
        std::size_t budgetBytes = std::size_t{24} * std::size_t{1024} * std::size_t{1024})
        : budgetBytes_(budgetBytes) {}

    [[nodiscard]] bool execute(Document& document, Command& command);
    [[nodiscard]] bool executeMesh(
        Document& document,
        MeshId meshId,
        MeshCommand& command,
        MeshCommandResult* result = nullptr);
    [[nodiscard]] bool undo(Document& document);
    [[nodiscard]] bool redo(Document& document);

    void clear() noexcept;
    void setBudgetBytes(std::size_t budgetBytes) noexcept;

    [[nodiscard]] std::size_t budgetBytes() const noexcept { return budgetBytes_; }
    [[nodiscard]] std::size_t retainedBytes() const noexcept { return retainedBytes_; }
    [[nodiscard]] std::size_t undoCount() const noexcept { return undo_.size(); }
    [[nodiscard]] std::size_t redoCount() const noexcept { return redo_.size(); }
    [[nodiscard]] RuntimeDocumentId ownerDocumentRuntimeId() const noexcept { return ownerDocumentRuntimeId_; }

private:
    [[nodiscard]] bool accepts(const Document& document) noexcept;
    [[nodiscard]] static bool applyRecord(Document& document, EditorHistoryRecord& record, bool forward);
    void clearRedo() noexcept;
    void recalculateRetainedBytes() noexcept;
    void enforceBudget() noexcept;

    RuntimeDocumentId ownerDocumentRuntimeId_;
    std::size_t budgetBytes_ = 0;
    std::size_t retainedBytes_ = 0;
    std::deque<EditorHistoryRecord> undo_;
    std::deque<EditorHistoryRecord> redo_;
};

} // namespace vortex
