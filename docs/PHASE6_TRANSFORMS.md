# Phase 6 — Object Transforms and Gizmo Interaction

Phase 6 established authored object transforms, renderer previews, command-based commit, undo/redo, and working axis Move/Rotate/Scale interaction. The merged baseline was device-verified on ARMv7.

> **Experimental branch note (2026-09-05):** `work/gizmo-visual-polish` now contains Gizmo System v2, a post-Phase-6 architectural refactor. The current branch uses quaternion-authoritative `ObjectTransform`, project schema v3, portable geometric constraints, plane/center/view/uniform handles, and frozen drag sessions. See `docs/GIZMO_SYSTEM_V2.md`. The historical Phase-6 implementation description below is superseded on this branch.

## Authored transform contract on this branch

`ObjectBlock` owns a finite local-to-parent transform:

```text
translation = (0, 0, 0)
rotation    = Quaternion::identity
scale       = (1, 1, 1)
```

Local matrices are column-major with column vectors:

```text
local = T * R(quaternion) * S
world = parentWorld * local
```

`Document::objectWorldRotation()` separately composes the quaternion hierarchy so gizmo orientation is not polluted by non-uniform parent scale.

`Document::setObjectTransform()` remains the authored mutation boundary. Preview movement is host/renderer state only; a successful drag commits exactly one `SetObjectTransformCommand` through `EditorHistory`.

## Project persistence

Project schema v3 stores translation, quaternion rotation and scale. The reader supports:

- v1 -> identity object transform;
- v2 -> legacy XYZ Euler radians converted once to quaternion;
- v3 -> native quaternion transform.

Invalid/non-finite transforms are rejected.

## Renderer boundary

Meshes remain object-local. The renderer receives stable object identity, extracted local geometry, an engine-derived world matrix and a rotation-only world quaternion. Rendering, picking, selection outline and gizmo placement therefore share the same preview transform without making Vulkan the authored source of truth.

## Gizmo System v2 interaction

The Android host converts touches to world rays. A data-driven handle selects a portable constraint:

```text
touch -> handle -> frozen DragSession -> portable constraint sample
      -> typed TransformDelta -> transform composer -> renderer preview
      -> ACTION_UP -> one SetObjectTransformCommand
```

Current handles are:

- Move: X/Y/Z axes, XY/XZ/YZ planes, center/view-plane move;
- Rotate: X/Y/Z rings plus an outer camera-facing view ring;
- Scale: X/Y/Z axes, XY/XZ/YZ planes, camera-facing uniform scale.

Axis Move uses a true 3D ray/axis closest-point solve. Plane/center Move uses ray/plane intersection. Rotate uses signed ring-plane phase with full-turn continuity and quaternion composition. Axis/plane/uniform Scale uses signed geometric ratios. Invalid edge-on/parallel samples hold the previous valid preview; the system does not switch to a different hidden pixel/tangent solver.

Local, Global and View orientation are represented in the host/renderer/solver contract and exposed by a dedicated Android orientation row. Local remains the default.

## Touch and lifecycle contract

A successful gizmo hit takes precedence over one-finger orbit. A second finger cancels the active transform before entering camera multitouch. `ACTION_CANCEL`, tool changes, Activity pause/destroy, surface destruction, Undo and Redo also cancel transient preview state safely.

Density-aware invisible pick regions are larger than visible geometry. Dense overlaps are scored by screen distance, descriptor priority and geometric conditioning.

## Validation / device gate

Before merging Gizmo System v2 back to `main`, validate on the 32-bit target:

- all Move axes, planes and center handle;
- all Rotate axis rings and the outer view ring at straight-on and edge-on camera angles;
- all Scale axes, planes and uniform ring, including deliberate negative scale crossing;
- mixed-axis repeated quaternion rotation without Euler trapping;
- parent rotation + non-uniform scale;
- selection/picking/outline staying aligned during previews;
- one undo step per completed drag;
- cancellation by second finger/lifecycle/tool change;
- orbit/pan/pinch/tap behavior outside gizmos;
- background/resume and surface recreation;
- no Vulkan validation/runtime errors.

Host tests, portable policy checks and both Android ABI CI builds remain required. Local environments without the Android NDK can validate portable code and syntax only; device/NDK CI is authoritative for the Android binary.
