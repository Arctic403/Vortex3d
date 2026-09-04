# Mesh Kernel Contract

## Purpose

The mesh kernel is the most correctness-sensitive subsystem in Vortex3D. It owns editable polygon topology and the invariants that make higher-level tools safe.

## Topology domains

### Vertex

A vertex owns a stable `VertexId` and its position attribute. It does not own face-varying data such as UVs.

### Edge

An edge connects exactly two vertex identities. It participates in zero or more face corners through radial connectivity.

### Face

A face represents an ordered polygon boundary and may have three or more corners. N-gons are legal authoring topology.

### Corner / Loop

A corner is one face-local use of a vertex and edge. Corners form:

- a circular boundary loop around a face,
- a radial cycle around an edge.

Face-varying attributes such as UV coordinates and split normals live naturally on this domain.

## Required invariants

For every valid mesh:

1. every referenced topology ID exists,
2. every face has at least three corners,
3. each face corner cycle closes exactly once,
4. `next` and `prev` links are mutually consistent,
5. each corner edge connects that corner's vertex to the next face-corner vertex,
6. radial links remain on the same edge,
7. each radial cycle closes exactly once and covers every live corner using that edge,
8. edge endpoint IDs are distinct,
9. stable IDs are unique inside their domain,
10. attribute storage length matches its declared domain size,
11. deleted elements are unreachable from live topology,
12. lookup caches never define truth; they are rebuildable from canonical topology.

The validator produces structured failure codes and useful diagnostic text rather than only a boolean.

## Generic attributes

Attributes are keyed by **semantic name + topology domain**:

```text
AttributeKey {
    name
    domain: Vertex | Edge | Face | Corner
    type: Bool | Int32 | UInt32 | Float | Vec2 | Vec3 | Vec4 | ...
}
```

This means `normal` can legally exist on both Vertex and Corner domains without collision.

Initial built-ins:

- Vertex `position : Vec3`
- Edge `crease : Float`
- Edge `sharp : Bool`
- Edge `seam : Bool`
- Face `material_index : Int32`
- Corner `uv:Map : Vec2`
- Corner `normal : Vec3`

### Attribute compaction

When a topology element is deleted, its packed attribute slot is erased from every layer on that domain. Remaining values preserve their relative order and the transient ID-to-packed-index lookup is rebuilt.

Stable topology IDs never change merely because packed attribute storage compacts.

### Attribute interpolation

When a topology operation creates a point between two existing domain elements:

- Float / Vec2 / Vec3 / Vec4 values interpolate linearly.
- Bool / integer values inherit from the nearest endpoint (`factor < 0.5` chooses the first endpoint, otherwise the second).
- Edge split copies all original edge-domain attributes to the newly created second segment before topology-specific edits can change them.

The current generic interpolation intentionally does not normalize semantic vector attributes such as normals. Normal recomputation/normalization belongs to the appropriate modeling or evaluation stage rather than the untyped storage layer.

## Trusted mutation primitives

Implemented foundation primitives:

- create/remove vertex,
- create/remove edge,
- create/remove polygon face,
- split edge.

Future candidates:

- collapse edge,
- split face,
- join faces across an edge,
- duplicate region/topology,
- reverse face winding.

Higher-level tools compose these operations through commands/transactions rather than directly mutating topology storage.

## Removal contract

### Remove Face

`removeFace(face, removeUnusedEdges=true)`:

1. removes all corners belonging to the face,
2. compacts Corner and Face attribute domains deterministically,
3. rebuilds radial cycles for every affected edge,
4. optionally deletes affected edges that have no remaining radial corners,
5. does **not** automatically delete now-isolated vertices.

Keeping vertex deletion explicit prevents a face deletion from unexpectedly invalidating vertex identities that an editor selection, script, modifier, or future AI operation may still reference.

### Remove Edge

An edge may only be removed when no live corner references it. The operation does not cascade into faces.

### Remove Vertex

A vertex may only be removed when no live edge references it. The operation does not cascade into edges or faces.

These deliberately conservative primitives are intended to be composed by higher-level dissolve/delete tools with an explicit transaction policy.

## Edge split contract

For an edge:

```text
A -------- B
    Edge E
```

`splitEdge(E, t)` with `0 < t < 1` produces:

```text
A ---- N ---- B
  E       E2
```

The contract is fixed as follows:

- the original `EdgeId E` survives on **original vertexA -> new vertex N**,
- a fresh `VertexId` is allocated for `N`,
- a fresh `EdgeId E2` is allocated for **N -> original vertexB**,
- the new vertex position and generic vertex attributes interpolate using `t`,
- every face using `E` receives exactly one new Corner at `N`,
- every inserted Corner receives a fresh `CornerId`,
- each affected original Corner keeps its own ID,
- face IDs survive unchanged and each affected face corner count increases by one,
- original edge-domain attributes are copied to `E2`,
- corner-domain attributes interpolate between the face-local endpoint corners,
- both new radial cycles contain the same number of face uses as the original edge,
- the operation supports boundary, manifold, and non-manifold radial edges.

For a face traversing the edge in reverse (`B -> A`), the face-local interpolation factor is `1 - t`; this preserves the same geometric split point while respecting face winding.

Invalid factors (`t <= 0` or `t >= 1`) are rejected without mutation.

## ID behavior

- Pure coordinate edits retain topology IDs.
- Attribute edits retain topology IDs.
- New topology always receives new IDs.
- Edge split follows the explicit inheritance contract above.
- Deleted elements become invalid and their IDs are never silently rebound to new elements.
- Packed-array compaction never changes a surviving topology ID.

These rules are part of the public modeling contract because selections, commands, undo, modifiers, scripting, serialization, and future AI references may depend on them.

## Evaluation boundary

The editable kernel does not need to be GPU-friendly.

`EditableMesh` may preserve n-gons, stable identities, radial topology, and rich attributes. A later evaluation/conversion step produces packed triangulated arrays for Vulkan and export.

## 32-bit considerations

Stable IDs remain 64-bit even on ARMv7; pointer size must not leak into persistent identity. Internal compact indices may be 32-bit when validated against limits, but they are caches/implementation details and not persistent IDs.

Avoid per-element heap allocation when a pool/packed-slot representation can provide the same semantics. The exact storage layout should be benchmarked before being locked.

No mesh primitive should rely on pointer-sized persistent handles.

## Current proof fixtures

The native suites now cover:

- isolated vertex/edge deletion,
- deterministic vertex attribute compaction,
- face deletion with Face/Corner attribute compaction,
- automatic deletion of now-unused face edges,
- quad,
- concave n-gon,
- closed cube,
- two faces sharing an edge,
- non-manifold edge with 3 radial faces,
- shared-edge split with stable-ID inheritance,
- vertex attribute interpolation on split,
- edge attribute copying on split,
- non-manifold 3-face edge split.

Still required as the kernel grows:

- collapse-edge fixtures,
- hole/boundary-region fixtures,
- randomized sequences of trusted operations followed by validation,
- donor behavior fixtures rewritten as native contract tests rather than architectural source copies.
