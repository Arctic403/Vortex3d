# Vortex3D Mesh Commands and Undo

## Purpose

Editor actions, automation, plugins, scripts, and future AI must use the same mutation path. A successful editor-visible mutation must either be reversible or be rejected before it touches authored state.

The second hard requirement is 32-bit Android viability: production history must not retain a complete `EditableMesh` copy for every edit.

## Command boundary

`MeshCommand` owns one requested modeling action.

A command returns:

- a `MeshCommandResult` describing affected/new stable IDs for selection and editor repair,
- an optional reversible `MeshHistoryRecord`.

`MeshHistory::execute()` accepts only commands that report themselves undoable. A command whose compact history representation does not exist yet is rejected before mutation when routed through editor history.

This is why `ExtrudeFaceCommand` currently works for direct headless execution but is blocked by `MeshHistory` until its topology patch lands.

## History classes

### Pure value edits

Vertex movement uses compact deltas:

```text
VertexPositionChange {
    VertexId id
    Vec3 before
    Vec3 after
}
```

No topology, unrelated attributes, or unrelated vertices are copied.

### Topology edits

Topology history uses a **local exact-ID patch**, not a whole mesh snapshot.

For face extrusion the patch must retain only:

```text
ExtrudeTopologyHistory
  source face snapshot
  source corner snapshots
  created vertex snapshots
  created edge snapshots
  created face snapshots
  created corner snapshots
  affected attribute rows
```

Each element snapshot contains its stable ID, the minimal canonical topology fields needed to rebuild it, its packed-domain position where required for deterministic restoration, and one captured attribute row for that domain.

## Exact-ID requirement

Undo must not silently turn an old ID into a different identity.

For extrusion:

1. the first apply removes source FaceId `F` and creates cap/side/new topology,
2. undo deletes only topology created by that extrusion,
3. undo restores **the original `F` and original source CornerIds**,
4. redo removes `F` again,
5. redo restores the **same cap/side/vertex/edge/corner IDs created by the first apply**.

This makes command results stable across undo/redo and avoids forcing selection, modifiers, scripts, or AI references to guess at remapped topology.

The global monotonic allocator remains advanced after undo. Exact restoration is allowed only for IDs owned by the history patch and proven absent from live topology. Normal allocation never reuses those IDs.

## Attribute rows

Topology history captures attributes per affected element, not entire attribute layers.

`AttributeRow` is a list of typed `(AttributeKey, AttributeScalar)` values for one domain slot.

Required operations:

- capture one domain row,
- insert/append one domain row with captured values,
- erase an existing row through normal topology removal.

If a new attribute layer is introduced after a history record was created, restoring an old row uses that layer's default value. If a stored key no longer exists, the unknown stored value is ignored unless project/history migration later defines another policy.

## Radial and face links

History does not need to store every transient radial link of neighboring topology.

Canonical restoration rebuilds:

- face `next/prev` cycles from the restored ordered Corner list,
- edge radial cycles from all live Corners using the edge.

`anyCorner` and radial ordering are treated as rebuildable connectivity caches as long as every live use is represented and validator invariants hold.

## Memory budget

`MeshHistory` tracks estimated retained bytes and owns a configurable budget.

Policy:

1. redo and undo records share one retained-byte budget,
2. moving records between undo/redo does not duplicate byte accounting,
3. a divergent edit clears the redo branch,
4. oldest undo records are discarded first when the budget is exceeded,
5. a single record larger than the entire budget must be rejected/rewound rather than becoming a permanent non-undoable mutation.

The default host budget is currently 8 MiB only as an engineering baseline. Android will choose a device-aware budget after memory/capability reporting exists.

## Temporary snapshots

Whole-`EditableMesh` snapshots are currently permitted **inside one operation** to guarantee atomic rollback while topology primitives are still maturing.

They are not retained in the undo stack.

Once exact local topology patches are fully proven, operation-local snapshots should also be reduced where practical.

## Test gates

Before topology undo is considered usable:

- isolated quad extrude -> undo -> redo retains exact IDs,
- attached cube face extrude -> undo -> redo retains unaffected neighbor IDs and radial counts,
- concave n-gon extrude -> undo -> redo validates,
- mixed move + extrude history rewinds to semantic-equivalent source state,
- redo reproduces the original command result IDs,
- repeated cycles pass ASan/UBSan,
- retained history bytes stay below the configured budget,
- no history record contains a full `EditableMesh` object.
