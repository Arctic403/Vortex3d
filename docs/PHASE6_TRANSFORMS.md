# Phase 6 — Object Transforms and Gizmo Interaction

Phase 6 turns the Phase 5 gizmo foundation into real object manipulation while keeping authored truth in the portable engine.

## Phase 6A — authored transform contract

`ObjectBlock` owns a local-to-parent `ObjectTransform`:

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

## Phase 6B — transform-aware renderer boundary

The Android bootstrap scene no longer bakes the second object's placement into authored mesh vertices. Both meshes are centered in local space. The second object's world placement comes from persistent `ObjectBlock::transform`, and `ViewportHost` resolves the object world matrix through `Document::objectWorldMatrix()` before creating a renderer snapshot.

Each visible object now crosses the renderer boundary as:

```text
ObjectId
ViewportMesh in object-local space
engine-derived world TransformMatrix
```

The current Vulkan backend still batches local vertex/index storage for compactness, but it keeps a draw range per object. Command recording pushes `cameraViewProjection * objectWorld` for each draw, so one shared buffer can render independently transformed objects without changing authored topology.

CPU picking uses the same world matrix to build derived world-space pick triangles from the exact local `ViewportMesh`. The selected outline and XYZ gizmo foundation remain local geometry and are rendered with the selected object's same world matrix. This keeps rendered geometry, picking, and selection feedback on one transform source of truth.

Renderer-local synthetic face IDs remain only an internal batching/picking disambiguation mechanism. Picks are mapped back to stable engine `ObjectId + FaceId` before they reach `EditorContext`.

The Phase 6B Android gate is:

- both bootstrap meshes remain local-origin geometry;
- the second cube still appears at its engine-authored translated world location;
- tapping either transformed object selects the correct stable object;
- only that object's outline/gizmo is transformed and shown;
- tapping empty background deselects;
- orbit/pan/pinch and surface recreation remain unchanged;
- ARMv7 and ARM64 native/APK CI stays green.

## Next slice — Phase 6C Move gizmo

After the Phase 6B renderer boundary is verified on ARMv7, the Move gizmo becomes interactive. Axis hit testing and drag preview are tool/viewport state, not authored state. A successful gesture commits through `SetObjectTransformCommand`; cancel restores the original transform without creating history.

Rotate and Scale follow only after Move interaction and undo/redo are stable.
