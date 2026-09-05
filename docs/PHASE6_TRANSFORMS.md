# Phase 6 — Object Transforms and Gizmo Interaction

Phase 6 turns the Phase 5 selection/gizmo foundation into real object manipulation while keeping authored truth in the portable engine.

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

## Undo/redo and project persistence

`SetObjectTransformCommand` records a compact `SetObjectTransformDelta { before, after }`. It uses the existing `EditorHistory` chronological timeline, so object transforms interleave correctly with mesh edits and other document commands. There is no viewport-owned authored transform history.

A no-op transform command produces no history record. Undo/redo re-enters the normal `Document::setObjectTransform()` mutation path, preserving revision/change-journal behavior.

Project schema v2 stores translation, rotation and scale on every serialized object. The reader explicitly supports schema v1 and v2. A schema-v1 object receives the identity transform; non-finite schema-v2 transform data is rejected even with a valid checksum.

Portable validation covers identity defaults, no-op revisions, non-finite rejection, parent/child world composition, command apply/undo/redo, chronological mixed-history ordering, v2 round trips, hostile transform data and v1 migration.

## Phase 6B — transform-aware renderer boundary

The Android bootstrap scene no longer bakes object placement into authored mesh vertices. Meshes remain centered in object-local space. `ViewportHost` resolves persistent object placement through `Document::objectWorldMatrix()` and sends the renderer:

```text
ObjectId
ViewportMesh in object-local space
engine-derived world TransformMatrix
```

The Vulkan backend keeps per-object draw ranges over shared local vertex/index storage. Command recording pushes `cameraViewProjection * objectWorld` for every object draw.

CPU picking, the active-object outline and the XYZ gizmo use the same derived world transform. Renderer-local synthetic face IDs exist only to disambiguate batched picking and are mapped back to stable engine `ObjectId + FaceId` before editor state changes.

This means authored topology never needs to be rewritten merely because an object moves, rotates or scales.

## Phase 6C — interactive Move / Rotate / Scale

The XYZ gizmo is now touch-interactive. A touch beginning close to one of the selected object's projected axes locks that axis before the normal one-finger orbit recognizer starts.

The interaction flow is:

```text
ACTION_DOWN on gizmo axis
        |
        v
screen-space axis hit + drag scale
        |
        v
transient ObjectTransform preview
        |
        v
engine-derived preview world matrix
        |
        v
render + CPU picking + outline + gizmo update
        |
ACTION_UP
        |
        v
one SetObjectTransformCommand
        |
        v
EditorHistory
```

Move is constrained to the chosen object's local X/Y/Z axis. Rotate changes the corresponding local Euler component. Scale changes the corresponding local scale component and clamps the magnitude away from a singular zero scale.

Critically, `ACTION_MOVE` does **not** write the preview into `Document`. The drag owns transient tool state only. A successful `ACTION_UP` commits exactly one `SetObjectTransformCommand`; cancellation restores the original renderer-derived world transform and adds no history record.

Cancellation occurs on:

- `ACTION_CANCEL`;
- a second finger entering the gesture;
- tool changes;
- Activity pause/destroy;
- surface destruction;
- Undo/Redo while a preview is active.

The existing camera contract remains intact when the gesture does not start on a gizmo axis:

- one finger: orbit after touch slop;
- two coherent fingers: pan;
- symmetric pinch: zoom;
- short one-finger tap: object selection.

The Android toolbar exposes Move, Rotate, Scale, Undo and Redo. JNI calls remain on the Android UI thread.

## Phase 6 exit gate

Automated acceptance requires:

- portable transform/history/project tests green;
- warnings-as-errors GCC and Clang builds green;
- sanitizer and clang-tidy jobs green;
- ARMv7 and ARM64 NDK builds green;
- split and universal APK packaging green.

On-device acceptance for the 32-bit target is:

- select either cube and drag X, Y and Z in Move mode;
- switch to Rotate and rotate on each usable projected axis;
- switch to Scale and scale on each usable projected axis;
- the mesh, pick target, outline and gizmo remain aligned during previews;
- completed drags create one Undo step each;
- Undo and Redo restore the complete transform accurately;
- adding a second finger during a gizmo drag cancels the preview and enters camera multitouch cleanly;
- background/surface recreation leaves authored state and selection behavior stable;
- no Vulkan errors occur.

Phase 6 is considered implementation-complete when this code and CI gate are merged. It is marked device-verified only after the Android acceptance pass is performed on the target phone.
