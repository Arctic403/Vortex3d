# Vortex3D Commands, Transactions, and Undo

## Purpose

Editor actions, automation, plugins, scripts, and future AI use the same mutation path. A successful editor-visible mutation must either be reversible or be rejected before it becomes durable authored state.

32-bit Android viability is a hard requirement: production history does not retain a complete Document or EditableMesh copy for every edit.

## Document commands

Ordinary scene/document edits produce compact deltas:

- object rename -> previous/new names,
- parenting -> previous/new parent IDs,
- mesh assignment -> previous/new MeshIds,
- Make Unique -> source/new MeshIds plus ownership transfer of the created MeshBlock while undone.

`DocumentHistory` owns undo/redo records and a configurable retained-byte budget. It is bound to one live Document instance so records cannot be replayed against unrelated state.

The old whole-`Document snapshot_` transaction model has been removed. `Transaction` now stores only command deltas for atomic rollback. If an uncommitted transaction rolls back, its revision/change-log side effects are removed as well.

## Mesh commands

`MeshCommand` owns one requested modeling action and returns:

- a `MeshCommandResult` describing affected/new stable IDs for selection/editor repair,
- an optional reversible `MeshHistoryRecord`.

`MeshHistory` is also bound to one live EditableMesh instance after its first retained edit. Cross-mesh replay is rejected.

### Pure value edits

Vertex movement uses compact deltas:

```text
VertexPositionChange {
    VertexId id
    Vec3 before
    Vec3 after
}
```

### Topology edits

Face extrusion already uses a local exact-ID topology patch rather than a retained whole-mesh snapshot. The patch stores only source/created topology records and affected attribute rows.

Undo restores the original source Face/Corner IDs. Redo restores the same cap/side/new topology IDs produced by the first execution.

## Memory budgets

Document and mesh histories both track estimated retained bytes.

Policy:

1. undo and redo share one budget per history,
2. divergent edits clear redo,
3. oldest retained records are discarded first when necessary,
4. an individual edit larger than the entire budget is reversed instead of becoming a permanent non-undoable mutation,
5. estimates are conservative engineering controls, not allocator-exact profiling data.

The current default budgets are engineering baselines and will become device-aware after Android memory/capability reporting exists.

## Operation-local rollback

Whole-`EditableMesh` copies are still permitted temporarily **inside one topology operation** to guarantee atomic rollback while primitives mature. Those copies are not retained in history.

They should be removed only when benchmark/profiling evidence and local rollback machinery justify the change.

## Stable identity

Undo/redo never treats packed indices as external identity. Restored topology receives the exact stable IDs owned by its history patch. Normal allocation remains monotonic; rolled-back/deleted IDs are not silently rebound to unrelated elements.

## Test gates

History changes must preserve:

- deterministic undo/redo,
- exact topology IDs where promised,
- failed-command atomicity,
- bounded retained bytes,
- GCC and Clang builds,
- ASan/UBSan,
- Android/32-bit compile compatibility once the Android CI gate is active.
