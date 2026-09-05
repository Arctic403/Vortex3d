# Phase 6 — Object Transforms and Gizmo Interaction

Phase 6 turns the Phase 5 gizmo foundation into real object manipulation while keeping authored truth in the portable engine.

## Phase 6A — authored transform contract

`ObjectBlock` now owns a local-to-parent `ObjectTransform`:

```text
translation      = (0, 0, 0)
rotationRadians  = (0, 0, 0)
scale            = (1, 1, 1)
```

Transforms are finite-only authored state. `Document::setObjectTransform()` is the mutation boundary: it rejects missing objects and non-finite values, treats an identical transform as a no-op, advances the object/document revision only for a real change, and emits the normal Object/Updated change event.

The portable transform matrix convention is column-major with column vectors. Local matrices use:

```text
T * Rz * Ry * Rx * S
```

Parenting composes as:

```text
world = parentWorld * local
```

`Document::objectWorldMatrix()` resolves the complete parent chain and refuses malformed/missing ancestry rather than inventing a world transform.

## Undo/redo

`SetObjectTransformCommand` records a compact `SetObjectTransformDelta { before, after }`. It uses the existing `EditorHistory` chronological timeline, so object transforms interleave correctly with mesh edits and other document commands. There is no viewport-owned transform history.

A no-op transform command produces no history record. Undo/redo always re-enters the normal `Document::setObjectTransform()` mutation path, preserving revision/change-journal behavior.

## Project schema v2

Project schema v2 adds three tightly packed `Vec3` values to each serialized object:

```text
translation
rotationRadians
scale
```

The reader explicitly supports schema v1 and v2. A schema-v1 object receives the identity transform. A schema-v2 file containing non-finite transform data is rejected even if its checksum is otherwise valid.

The existing file magic remains unchanged; the explicit schema-version field is the compatibility boundary.

## Phase 6A validation gate

The portable test gate covers:

- identity defaults;
- no-op transform revision behavior;
- non-finite rejection;
- parent/child world-matrix composition;
- transform command apply/undo/redo;
- chronological undo ordering across transform, mesh, and rename commands;
- schema-v2 transform round trip;
- hostile non-finite schema-v2 data;
- schema-v1 migration to identity transforms.

## Next slice — Phase 6B

After 6A is green, the Android viewport will stop baking object placement into mesh vertices. Each visible object will carry its engine-derived world matrix and draw range. Rendering, CPU picking, the selection outline, and the gizmo origin must all consume the same derived matrix.

Once that renderer boundary is verified on ARMv7, the Move gizmo will become interactive. Drag previews may remain transient tool state, but commit/cancel will resolve to `SetObjectTransformCommand`. Rotate and Scale follow only after Move is stable.
