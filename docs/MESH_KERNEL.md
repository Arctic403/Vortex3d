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
5. each corner's vertex belongs to the corner's edge,
6. radial links remain on the same edge,
7. each radial cycle closes exactly once,
8. edge endpoint IDs are distinct,
9. stable IDs are unique inside their domain,
10. attribute storage length matches its declared domain size,
11. deleted elements are unreachable from live topology,
12. lookup caches never define truth; they are rebuildable from canonical topology.

The validator should be able to produce a useful failure reason in debug/test tooling, not only a boolean.

## Generic attributes

The kernel should eventually expose a registry similar to:

```text
AttributeKey {
    name
    domain: Vertex | Edge | Face | Corner
    type: Bool | Int32 | UInt32 | Float | Vec2 | Vec3 | Vec4 | ...
}
```

Required built-in semantics can still have fast paths, but their storage contract should use the same domain model where practical.

Initial attributes:

- Vertex `position : Vec3`
- Edge `crease : Float`
- Edge `sharp : Bool`
- Edge `seam : Bool`
- Face `material_index : Int32`
- Corner `uv:Map : Vec2`
- Corner `normal : Vec3` when authored/split normals exist

## Trusted mutation primitives

Do not begin with dozens of UI tools. Build a small set of kernel operations with exhaustive invariant tests.

Candidate primitives:

- create/remove vertex,
- create/remove edge,
- create/remove polygon face,
- split edge,
- collapse edge,
- split face,
- join faces across an edge,
- duplicate region/topology,
- reverse face winding.

Higher-level tools compose these operations inside a transaction.

## ID behavior

- Pure coordinate edits retain topology IDs.
- Attribute edits retain topology IDs.
- Edge split retains the old edge identity only when the operation's contract explicitly defines which segment inherits it; otherwise replace with new identities deterministically.
- New topology always receives new IDs.
- Dissolved/deleted elements become invalid and are never silently rebound to new elements.

Exact inheritance rules must be documented per primitive because selection, modifiers, history, and future AI references may depend on them.

## Evaluation boundary

The editable kernel does not need to be GPU-friendly.

`EditableMesh` may preserve n-gons, sparse stable handles, and rich attributes. An evaluation/conversion step produces packed arrays and triangulation for Vulkan/export.

## 32-bit considerations

Stable IDs remain 64-bit even on ARMv7; pointer size must not leak into persistent identity. Internal compact indices may be 32-bit when validated against limits, but they are caches/implementation details and not persistent IDs.

Avoid per-element heap allocation when a pool/packed-slot representation can provide the same semantics. The exact storage layout should be benchmarked before being locked.

## Phase 2 proof fixtures

Tests should include at minimum:

- triangle,
- quad,
- concave n-gon,
- cube,
- boundary edge,
- isolated edge/vertex if supported,
- two faces sharing an edge,
- non-manifold edge with 3+ radial faces,
- hole/boundary cases once supported,
- random sequences of trusted operations followed by validation.
