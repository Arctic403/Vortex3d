# Gizmo System v2 — Constraint-Centric Transform Manipulation

Status: **experimental on `work/gizmo-visual-polish`**. This document describes the branch architecture, not the merged `main` baseline.

## Why v2 exists

The first transform gizmo proved selection, preview, commit, undo/redo and mobile touch routing, but it also mixed too many concerns: screen-space hit metadata, transform math, Euler storage and renderer state all influenced one another.

Gizmo System v2 makes the visible handle a selector for a mathematical constraint. Rendering is feedback only; the portable solver owns the geometry of the manipulation and returns a typed transform delta.

```text
pointer -> camera ray -> handle picker -> frozen drag session
                                |
                                v
                        portable constraint solver
                                |
                                v
                         typed TransformDelta
                                |
                                v
                         transform composer
                                |
                                v
                       renderer-only preview
                                |
                         finger up / confirm
                                |
                                v
                    one SetObjectTransformCommand
```

## Authoritative transform representation

`ObjectTransform` is quaternion-authoritative:

```text
translation : Vec3
rotation    : Quaternion
scale       : Vec3
```

Euler angles are no longer the physical rotation state of an object. They may be converted at UI/import boundaries when an angle-based presentation is useful, but gizmo composition and world-orientation propagation operate on quaternions.

Local matrices remain column-major and compose as:

```text
T * R(quaternion) * S
```

Parenting remains:

```text
worldMatrix   = parentWorldMatrix * localMatrix
worldRotation = parentWorldRotation * localRotation
```

World rotation is resolved separately from scale. This is deliberate: non-uniform parent scale or shear in an affine matrix must not tilt the local gizmo basis.

## Project schema

Schema v3 stores quaternion rotation directly. The reader still accepts:

- schema v1: no authored object transform -> identity transform;
- schema v2: translation + XYZ Euler radians + scale -> migrated once to a quaternion;
- schema v3: translation + quaternion + scale.

Non-finite or invalid transform payloads are rejected.

## Portable gizmo contract

`include/vortex/editor/gizmo.hpp` owns the platform-independent contract:

- tool modes: Move / Rotate / Scale;
- orientations: Global / Local / View;
- handles: axis, plane, center, view-ring and uniform-scale;
- constraint kinds;
- data-driven handle descriptors;
- pointer rays and camera/gizmo frames;
- axis, plane and rotation samples;
- typed translation/rotation/scale deltas;
- pure transform composition;
- constraint-parameter snapping hooks.

The solver contains no Android, JNI or Vulkan dependencies.

## Frozen drag sessions

A drag freezes its mathematical starting state at touch-down:

- authored local transform;
- world pivot;
- rotation-only gizmo frame;
- camera frame;
- parent world matrix and parent world rotation;
- initial constraint sample.

Every preview is derived from that frozen state plus the current pointer ray. Preview transforms are not fed back as the next mathematical baseline. Rotation keeps only the continuity state needed to unwrap the `-pi..pi` phase seam.

Invalid geometric samples do not switch to a second interaction model. The previous valid preview is held until the same constraint becomes solvable again.

## Constraints

### Move

- **Axis X/Y/Z:** closest-point parameter between the pointer ray and the selected 3D axis.
- **Plane XY/XZ/YZ:** pointer-ray intersection with the selected oriented plane.
- **Center:** pointer-ray intersection with a camera-facing view plane.

The solver produces a world-space translation delta. The composer maps that through the exact inverse parent linear transform before modifying authored parent-space translation.

### Rotate

- **Axis X/Y/Z:** pointer ray intersects the plane whose normal is the selected gizmo axis. The radial vector is converted to a ring phase and unwrapped across full turns.
- **View ring:** the same phase solver on a camera-facing plane around camera forward.

Local rotation composes as:

```text
qNew = qStart * qDelta
```

World/View rotation composes in world space and is mapped back through the parent world quaternion. There is no tangent/pixels-per-radian fallback.

### Scale

- **Axis X/Y/Z:** signed ratio of current/start axis parameters.
- **Plane XY/XZ/YZ:** independent signed ratios of the two plane coordinates.
- **Uniform:** signed projection ratio in a camera-facing plane.

Signed ratios permit intentional reflection. A small host-side zero hysteresis filters finger noise around the singular zero point, while the mathematical representation itself supports negative scale.

## Gizmo frame and orientation

The local gizmo frame is derived from quaternion world orientation, not by independently normalizing affine matrix columns.

- **Local:** object world quaternion.
- **Global:** identity/world basis.
- **View:** camera basis converted to a quaternion.

The Android test host exposes **Global / Local / View** as a dedicated orientation row, so all three frame modes can be exercised on-device.

## Rendering and mobile picking

Visible geometry and interaction geometry are deliberately separate.

Current handles:

- Move: three arrow axes, three plane handles, center/view-plane handle;
- Rotate: three torus axis rings, outer camera-facing view ring;
- Scale: three cube-ended axes, three plane handles, camera-facing uniform-scale ring.

The rotate axis radius remains `1.45` local gizmo units and is still screen-space calibrated.

Touch radii are density-aware and clamped. Picking scores candidates using distance, handle priority and constraint conditioning so a nearly edge-on or camera-aligned handle is less likely to steal a touch from a well-conditioned overlapping handle. The active handle is fixed for the drag.

A future refinement may keep multiple near-equal candidates through initial touch slop and use the first meaningful drag direction as an additional disambiguation score. That deferred resolver is intentionally **not** claimed as implemented yet.

## Rotate feedback

The active ring brightens and thickens. Rotation feedback is rendering-only and receives:

- active handle;
- accumulated angular sweep;
- start ring phase;
- current ring phase.

Reference/current spokes and an angular arc make straight-on rotations visually legible without changing rotation semantics.

## Snapping

Snapping belongs at the constraint-parameter layer, before transform composition. `GizmoSnapSettings` and `snapConstraintValue()` provide the portable foundation. The Android UI/modifier gesture for enabling snap is not wired yet.

Examples:

```text
axis distance -> snap(distance, translationStep) -> TranslateDelta
ring angle    -> snap(angle, rotationStep)       -> RotateDelta
scale ratio   -> snap(ratio, scaleStep)          -> ScaleDelta
```

## Tested invariants

Portable smoke coverage includes:

- ray/axis parameter solving;
- plane sampling;
- axis and view-ring phase sampling;
- camera-aligned axis conditioning;
- orthogonal mixed quaternion gizmo axes;
- complete handle descriptor sets;
- constraint-parameter snapping;
- translation composition under parent rotation + non-uniform scale;
- local quaternion rotation composition;
- world quaternion rotation through a parent;
- signed/negative scale composition;
- 2,000 deterministic randomized transform-composition cases covering parent rotation, non-uniform scale, negative scale/reflection, local/world rotation, translation inversion, and orthonormal gizmo bases;
- singular parent-scale rejection for translation composition;
- schema-v3 quaternion persistence;
- schema-v2 Euler migration;
- schema-v1 identity migration.

## Remaining branch work

The architecture intentionally leaves some features as explicit follow-ups instead of hiding them inside current math:

1. expose translation/rotation/scale snap controls and precision mode;
2. optionally add deferred first-motion candidate disambiguation for dense touch overlaps;
3. add hover/pressed states for pointer-capable Android devices;
4. extend randomized coverage if true affine shear becomes authored/supported rather than only appearing as a composed matrix consequence;
5. device-test every new plane/center/view/uniform handle and Global/Local/View mode on ARMv7 before merge.

Blender remains an interaction/reference source only; GPL source/assets are not copied into this MIT repository. The implementation is Vortex-owned portable C++ plus the Android/Vulkan adapter.
