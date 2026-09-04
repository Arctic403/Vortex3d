# Vortex3D Runtime Change Tracking

## Purpose

The runtime change journal is a bounded invalidation aid for live editor systems. It is not persistent project history and it is not the undo stack.

Every successful authored mutation may advance the `Document` revision and append a compact `ChangeEvent` containing the revision, data kind, change kind, and stable entity ID. Consumers use `changesSince(revision)` to request the retained suffix.

## Bounded journal contract

The journal has a configurable logical payload budget. The default is 256 KiB. Oldest events are discarded deterministically when the retained payload exceeds that budget.

`ChangeQueryResult::complete()` reports whether the requested suffix is complete. If a caller asks from a revision older than `discardedChangesThroughRevision()`, `complete()` is false. The caller must then perform a broader state resync instead of treating the returned events as exhaustive.

This prevents a slow renderer, exporter, UI observer, or future background evaluator from silently missing invalidations after falling behind the bounded journal.

## Atomic mutation boundaries

`Transaction` and `DocumentHistory` temporarily defer journal pruning while a multi-step operation may still need to roll back its event suffix. The budget is enforced again when the outer atomic boundary ends.

Rollback restores the pre-operation document revision and removes events produced by the failed/uncommitted operation. A failed command therefore does not leave durable change-journal side effects.

## Mesh evaluation revisions

`MeshBlock::revision` is the general datablock revision. `MeshBlock::evaluationRevision()` is narrower: it advances only when authored geometry or shading inputs change.

Metadata-only edits such as mesh rename may advance the general mesh/document revision without invalidating an otherwise identical evaluated geometry snapshot.

Mesh commands that are semantic no-ops do not advance either evaluation state or runtime change tracking. Examples include moving a vertex to its current position or reapplying an already-active shading value.

## Consumer rules

1. Store the last fully consumed `Document::revision()`.
2. Call `changesSince(lastRevision)` before incremental work.
3. If `complete()` is true, process the returned events in revision order.
4. If `complete()` is false, discard incremental assumptions and rebuild the consumer's derived state from current authored truth.
5. Never serialize `RuntimeDocumentId`, journal contents, or journal retention state as project identity.

## Memory accounting

`retainedChangeHistoryBytes()` reports logical `ChangeEvent` payload bytes (`event count * sizeof(ChangeEvent)`). Container allocator/block overhead is platform-dependent and intentionally excluded from this stable engineering metric.

The event count is nevertheless strictly bounded by the configured payload budget.

## Tests

`vortex.document.change.journal.smoke` covers bounded retention, gap detection, rollback behavior, and revision semantics. History and revision suites additionally verify that no-op mutations do not create invalidation noise.
