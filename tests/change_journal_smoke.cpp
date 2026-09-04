#include "vortex/core/command.hpp"
#include "vortex/core/document.hpp"
#include "vortex/core/document_commands.hpp"

#include <cassert>
#include <cstddef>
#include <utility>

int main() {
    using namespace vortex;

    Document document;
    const std::size_t twoEvents = sizeof(ChangeEvent) * 2U;
    document.setChangeHistoryBudgetBytes(twoEvents);
    assert(document.changeHistoryBudgetBytes() == twoEvents);

    const ObjectId first = document.createObject("First");
    const ObjectId second = document.createObject("Second");
    const ObjectId third = document.createObject("Third");
    assert(first && second && third);
    assert(document.revision() == 3U);
    assert(document.changeHistoryCount() == 2U);
    assert(document.retainedChangeHistoryBytes() <= document.changeHistoryBudgetBytes());
    assert(document.discardedChangesThroughRevision() == 1U);

    const ChangeQueryResult incomplete = document.changesSince(0U);
    assert(!incomplete.complete());
    assert(incomplete.discardedThroughRevision == 1U);
    assert(incomplete.events.size() == 2U);
    assert(incomplete.events.front().revision == 2U);
    assert(incomplete.events.back().revision == 3U);

    const ChangeQueryResult complete = document.changesSince(1U);
    assert(complete.complete());
    assert(complete.events.size() == 2U);

    document.clearChangeHistory();
    assert(document.changeHistoryCount() == 0U);
    assert(document.discardedChangesThroughRevision() == document.revision());
    assert(!document.changesSince(2U).complete());
    assert(document.changesSince(3U).complete());

    assert(document.renameObject(first, "First 1"));
    assert(document.renameObject(second, "Second 1"));
    const std::uint64_t beforeRollbackRevision = document.revision();
    const std::size_t beforeRollbackCount = document.changeHistoryCount();
    const std::uint64_t beforeRollbackDiscarded = document.discardedChangesThroughRevision();
    assert(beforeRollbackCount == 2U);

    {
        Transaction transaction(document);
        RenameObjectCommand renameFirst(first, "First temporary");
        RenameObjectCommand renameSecond(second, "Second temporary");
        RenameObjectCommand renameThird(third, "Third temporary");
        assert(transaction.execute(renameFirst));
        assert(transaction.execute(renameSecond));
        assert(transaction.execute(renameThird));
        // Budget pruning is deferred while rollback still needs the original journal suffix.
        assert(document.changeHistoryCount() == beforeRollbackCount + 3U);
    }

    assert(document.revision() == beforeRollbackRevision);
    assert(document.changeHistoryCount() == beforeRollbackCount);
    assert(document.discardedChangesThroughRevision() == beforeRollbackDiscarded);
    assert(document.object(first)->name == "First 1");
    assert(document.object(second)->name == "Second 1");
    assert(document.object(third)->name == "Third");

    {
        Transaction transaction(document);
        RenameObjectCommand renameFirst(first, "First committed");
        RenameObjectCommand renameSecond(second, "Second committed");
        RenameObjectCommand renameThird(third, "Third committed");
        assert(transaction.execute(renameFirst));
        assert(transaction.execute(renameSecond));
        assert(transaction.execute(renameThird));
        assert(transaction.commit());
    }

    assert(document.revision() == beforeRollbackRevision + 3U);
    assert(document.changeHistoryCount() == 2U);
    assert(document.retainedChangeHistoryBytes() <= twoEvents);
    assert(document.discardedChangesThroughRevision() == beforeRollbackRevision + 1U);
    assert(!document.changesSince(beforeRollbackRevision).complete());
    const ChangeQueryResult afterDiscard = document.changesSince(document.discardedChangesThroughRevision());
    assert(afterDiscard.complete());
    assert(afterDiscard.events.size() == 2U);

    document.setChangeHistoryBudgetBytes(0U);
    assert(document.changeHistoryCount() == 0U);
    assert(document.discardedChangesThroughRevision() == document.revision());

    const std::uint64_t beforeZeroBudgetChange = document.revision();
    assert(document.renameObject(first, "First zero budget"));
    assert(document.revision() == beforeZeroBudgetChange + 1U);
    assert(document.changeHistoryCount() == 0U);
    assert(document.discardedChangesThroughRevision() == document.revision());
    assert(!document.changesSince(beforeZeroBudgetChange).complete());
    assert(document.changesSince(document.revision()).complete());

    // Administrative clearing is safe even if requested while an atomic transaction is
    // active. The clear is deferred until the outer batch ends, so rollback never loses the
    // pre-transaction journal suffix it still needs.
    Document deferredClear;
    deferredClear.setChangeHistoryBudgetBytes(sizeof(ChangeEvent) * 4U);
    const ObjectId deferredObject = deferredClear.createObject("Deferred");
    assert(deferredObject);
    const std::uint64_t deferredStartRevision = deferredClear.revision();
    {
        Transaction transaction(deferredClear);
        RenameObjectCommand temporaryRename(deferredObject, "Deferred temporary");
        assert(transaction.execute(temporaryRename));
        deferredClear.clearChangeHistory();
        assert(deferredClear.changeHistoryCount() == 2U);
    }
    assert(deferredClear.revision() == deferredStartRevision);
    assert(deferredClear.changeHistoryCount() == 0U);
    assert(deferredClear.discardedChangesThroughRevision() == deferredStartRevision);
    assert(deferredClear.changesSince(deferredStartRevision).complete());

    Document deferredCommitClear;
    deferredCommitClear.setChangeHistoryBudgetBytes(sizeof(ChangeEvent) * 8U);
    const ObjectId committedObject = deferredCommitClear.createObject("Committed");
    {
        Transaction transaction(deferredCommitClear);
        RenameObjectCommand beforeClear(committedObject, "Before clear");
        RenameObjectCommand afterClear(committedObject, "After clear");
        assert(transaction.execute(beforeClear));
        deferredCommitClear.clearChangeHistory();
        assert(transaction.execute(afterClear));
        assert(transaction.commit());
    }
    assert(deferredCommitClear.discardedChangesThroughRevision() == 2U);
    const ChangeQueryResult postClearEvent = deferredCommitClear.changesSince(2U);
    assert(postClearEvent.complete());
    assert(postClearEvent.events.size() == 1U);
    assert(postClearEvent.events[0].revision == 3U);

    // DocumentHistory also defers pruning across a command that must be auto-rewound because
    // its undo record cannot fit the configured history budget.
    Document oversizedUndo;
    oversizedUndo.setChangeHistoryBudgetBytes(sizeof(ChangeEvent) * 2U);
    const ObjectId oversizedFirst = oversizedUndo.createObject("Oversized first");
    const ObjectId oversizedSecond = oversizedUndo.createObject("Oversized second");
    assert(oversizedFirst && oversizedSecond);
    assert(oversizedUndo.changeHistoryCount() == 2U);
    DocumentHistory zeroUndoBudget(0U);
    RenameObjectCommand rejectedRename(oversizedFirst, "Should rewind");
    assert(!zeroUndoBudget.execute(oversizedUndo, rejectedRename));
    assert(oversizedUndo.revision() == 2U);
    assert(oversizedUndo.object(oversizedFirst)->name == "Oversized first");
    assert(oversizedUndo.changeHistoryCount() == 2U);
    assert(oversizedUndo.discardedChangesThroughRevision() == 0U);
    assert(oversizedUndo.changesSince(0U).complete());

    const RuntimeDocumentId lineage = document.runtimeId();
    Document moved = std::move(document);
    assert(moved.runtimeId() == lineage);
    assert(moved.changeHistoryBudgetBytes() == 0U);
    assert(moved.discardedChangesThroughRevision() == moved.revision());
    assert(moved.changesSince(moved.revision()).complete());
    assert(document.runtimeId() != lineage);
    assert(document.validate());
    assert(moved.validate());

    return 0;
}
