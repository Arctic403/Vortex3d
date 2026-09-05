#pragma once

#include "vortex/core/document.hpp"

#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace vortex {

struct RenameObjectDelta final {
    ObjectId objectId;
    std::string before;
    std::string after;
};

struct SetObjectParentDelta final {
    ObjectId objectId;
    ObjectId before;
    ObjectId after;
};

struct SetObjectMeshDelta final {
    ObjectId objectId;
    MeshId before;
    MeshId after;
};

struct SetObjectTransformDelta final {
    ObjectId objectId;
    ObjectTransform before;
    ObjectTransform after;
};

struct MakeObjectMeshUniqueDelta final {
    ObjectId objectId;
    MeshId sourceMeshId;
    MeshId uniqueMeshId;
    std::optional<MeshBlock> detachedMesh;
};

using DocumentDelta = std::variant<
    RenameObjectDelta,
    SetObjectParentDelta,
    SetObjectMeshDelta,
    SetObjectTransformDelta,
    MakeObjectMeshUniqueDelta>;

struct DocumentHistoryRecord final {
    std::string name;
    std::vector<DocumentDelta> deltas;

    [[nodiscard]] std::size_t estimatedBytes() const noexcept;
};

class Command {
public:
    virtual ~Command() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual std::optional<DocumentHistoryRecord> apply(Document& document) = 0;
};

class DocumentHistory final {
public:
    explicit DocumentHistory(
        std::size_t budgetBytes = std::size_t{16} * std::size_t{1024} * std::size_t{1024})
        : budgetBytes_(budgetBytes) {}

    [[nodiscard]] bool execute(Document& document, Command& command);
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
    friend class Transaction;
    friend class EditorHistory;

    [[nodiscard]] static bool applyRecord(Document& document, DocumentHistoryRecord& record, bool forward);
    [[nodiscard]] bool accepts(const Document& document) noexcept;
    void clearRedo() noexcept;
    void recalculateRetainedBytes() noexcept;
    void enforceBudget() noexcept;

    RuntimeDocumentId ownerDocumentRuntimeId_;
    std::size_t budgetBytes_ = 0;
    std::size_t retainedBytes_ = 0;
    std::deque<DocumentHistoryRecord> undo_;
    std::deque<DocumentHistoryRecord> redo_;
};

// Transaction is an atomic composition boundary, not an undo store. It retains only
// the compact deltas produced by commands executed inside it. An uncommitted or failed
// transaction reverses those deltas and restores the original revision/change log.
class Transaction final {
public:
    explicit Transaction(Document& document);
    ~Transaction();

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    [[nodiscard]] bool execute(Command& command);
    [[nodiscard]] bool commit();
    void rollback();

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] std::uint64_t startRevision() const noexcept { return startRevision_; }

private:
    Document& document_;
    std::vector<DocumentHistoryRecord> records_;
    std::uint64_t startRevision_ = 0;
    std::size_t startChangeCount_ = 0;
    bool active_ = true;
    bool failed_ = false;
};

} // namespace vortex
