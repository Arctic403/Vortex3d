# Derived Shading Normals

## Purpose

Vortex3D treats standard shading normals as **derived evaluated data**, not durable authored truth and not an ordinary user modifier.

The evaluation order is:

```text
EditableMesh
    |
    v
ordered modifier stack
    |
    +--> Transform
    +--> Mirror / Weld
    +--> Triangulate
    |
    v
DerivedNormalsGenerator
    |
    v
immutable EvaluatedMesh snapshot
    |
    v
future tangent generation / renderer / export
```

This keeps authored topology editable while ensuring normals describe the final generated geometry actually consumed downstream.

## Attribute semantics

### Authoring controls

Shading continuity is controlled by authoring attributes:

- Edge `sharp : Bool`, default `false`.
- Face `sharp_face : Bool`, semantic default `true` when the layer is absent.

A missing `sharp_face` layer therefore means flat shading. `SetFaceSharpCommand` materializes the layer on first authored edit using default `true` values so untouched faces retain flat behavior.

`SetEdgeSharpCommand` and `SetFaceSharpCommand` are reversible mesh commands. Their history records contain only the stable element ID and before/after boolean values. When executed through `Document`, the normal mesh/document revision path advances, which invalidates revision-keyed evaluated cache entries without special cache coupling.

### Standard `normal`

The standard Corner-domain `normal : Vec3` in a completed `EvaluatedMesh` is derived output.

New `EditableMesh` instances do **not** bootstrap an authored Corner `normal` layer. This avoids paying 12 logical bytes per authored corner for data that evaluation would immediately replace, and it avoids copying that redundant layer into the common evaluated path.

Legacy/imported meshes may still contain a Corner `normal` layer. If present, it is compatibility data rather than authoritative standard shading truth: the final normal stage validates its type and overwrites its evaluated storage in place. A future authored custom/split-normal feature must use an explicit semantic such as `custom_normal` and define how it participates in evaluation; it must not silently overload the standard derived `normal` contract.

Only final Corner normals are retained in evaluated snapshots. Face normals, edge-use counts, smoothing-fan bookkeeping, and face-cycle scratch are temporary operation data and are released after generation. Corner-angle weights are computed on demand rather than retained as a separate per-corner array.

## Geometric face normals

Each evaluated face receives a temporary geometric normal computed from its current evaluated positions using Newell accumulation. This supports triangles, quads, and simple n-gons without requiring authored triangulation.

The accumulated vector is normalized. Non-finite positions, zero-area/degenerate faces, collapsed face edges, broken face cycles, and invalid topology return structured normal-generation errors instead of producing NaN/Inf values.

Face winding determines normal direction. Reversing a valid face cycle reverses its geometric normal.

## Flat shading

A face is flat when `sharp_face` is `true`, or when the Face `sharp_face` layer is absent.

Every corner of a flat face receives exactly the normalized geometric face normal. No neighboring face can contribute to that corner's standard shading normal.

Flat-by-default behavior is intentional for early modeling primitives such as cubes.

## Smooth shading

A smooth face has `sharp_face == false`.

Smooth Corner normals are angle-weighted sums of the geometric normals in the connected smoothing fan around the same evaluated vertex:

```text
N = normalize(sum(face_normal * corner_angle))
```

The current implementation uses packed Corner indices plus a temporary disjoint-set structure. It does not allocate one hash map per corner and does not change stable authored identity.

### Smoothing boundaries

Smoothing crosses an evaluated edge only when all of these conditions hold:

1. exactly two live evaluated face corners use the edge,
2. the Edge `sharp` attribute is `false` or absent,
3. both incident faces are smooth,
4. the radial pair and edge endpoints are structurally valid.

Therefore:

- boundary edges stop smoothing,
- explicitly sharp edges stop smoothing,
- flat faces stop smoothing,
- non-manifold edges with three or more face uses stop smoothing.

Vortex supports non-manifold topology, but it does not invent an arbitrary smoothing relationship across an ambiguous multi-face radial edge.

## Interaction with modifiers

Normals are generated **after** all current geometry modifiers.

### Transform

Final normals are computed from transformed positions, so non-uniform scale is naturally reflected by the final geometry. Existing normal-transform helpers remain useful for future explicit custom-normal semantics, but standard derived normals do not rely on stale authored normal vectors.

### Mirror / Weld

Mirror may duplicate topology, reverse winding, snap seam vertices, reuse seam edges, and create supported non-manifold radial rings. The derived stage runs after those changes, so standard normals describe the final welded/generated topology rather than attempting to maintain shading incrementally through every mutation.

### Triangulate

Triangulate remains non-destructive to authored n-gons. Generated diagonal edges have no authored `EdgeId` and receive default Edge-domain attribute values, including `sharp == false`.

A smooth source surface therefore does not gain an artificial shading split merely because a generated diagonal was introduced for rendering/export.

## Error model

`NormalGenerationError` currently includes:

- `MissingPositionAttribute`,
- `InvalidTopology`,
- `NonFinitePosition`,
- `DegenerateFace`,
- `DegenerateSmoothFan`,
- `InvalidShadingAttribute`,
- `AttributeWriteFailed`.

A failure is surfaced by `MeshEvaluationError::NormalGenerationFailed`, with the focused normal error retained by `MeshEvaluationResult` and `CachedEvaluationResult`.

The evaluator remains deterministic and exception-light.

## Cache and lifetime

Derived Corner normals live inside the immutable `EvaluatedMesh` snapshot and automatically count toward `EvaluationCache` retained-byte estimates through the normal Attribute storage.

The existing cache identity remains sufficient:

```text
MeshId
+ authored Mesh revision
+ ordered modifier-stack revision
```

Shading commands advance the authored Mesh revision, so changing `sharp` or `sharp_face` produces a new evaluation key. Older externally held `shared_ptr<const EvaluatedMesh>` snapshots keep their previous normals and remain immutable.

## 32-bit Android memory discipline

A `Vec3` Corner normal is 12 logical bytes. Large triangulated meshes can therefore spend tens of megabytes on standard shading normals alone.

V0.1 deliberately avoids retaining redundant Face and Vertex normal arrays. New authored meshes do not carry a standard Corner normal buffer. During generation, the final evaluated Corner `normal` storage doubles as the smooth-fan accumulation buffer, so there is no separate accumulated Vec3 array plus generated Vec3 array. Corner-angle weights are computed on demand, and one reusable face-cycle vector replaces per-face scratch allocation.

The remaining large temporary structures are packed topology/fan arrays required by the current algorithm. They are explicit future profiling targets rather than a reason to introduce allocator architecture prematurely.

No custom allocator or PMR conversion is introduced by this feature. Future allocator work remains profiling-driven.

## Performance measurement

`vortex_eval_bench` measures the derived normal stage independently from the existing core mesh benchmark. It builds a smooth manifold quad strip, creates an evaluated snapshot, and times `DerivedNormalsGenerator::generate()` directly.

Normal CI runs a 1,000-corner smoke profile and archives JSON output beside the existing mesh benchmark artifact. Manual 10k / 100k / 1M requested profiles are available through the benchmark workflow, but the fixture face count is currently capped because authored `addFace()` still relies on linear edge lookup during setup. The output records `capped=true` when the requested scale cannot be represented economically by that correctness-first authoring path.

The initial 64-bit CI Release smoke on 2026-09-04 measured 250 smooth quads / 1,000 corners at approximately 0.062 ms for the derived-normal stage and about 85.8 KB estimated retained evaluated bytes. Treat this as a regression baseline for that runner, not a cross-device performance guarantee.

## Testing contract

Normal coverage includes:

- absence of a standard authored Corner normal bootstrap,
- evaluated Corner normal materialization,
- flat default faces,
- reversed winding,
- fully smooth cube fans,
- sharp-edge fan splitting,
- face smooth/flat authoring commands,
- command undo/redo and evaluation-cache invalidation,
- immutable old cached snapshots,
- Mirror Weld followed by Triangulate,
- non-manifold smoothing boundaries,
- non-uniform Transform geometry,
- explicit degenerate-face failure.

All normal code remains under the normal GCC, Clang, ASan/UBSan, clang-tidy, Android ARMv7 32-bit, Android ARM64, portability, and benchmark-smoke gates.

## Deferred intentionally

Not part of Derived Shading Normals v0.1:

- custom/split normal authoring UI,
- Weighted Normal modifier behavior,
- automatic angle-threshold edge marking,
- MikkTSpace tangents,
- renderer vertex packing,
- GPU/Vulkan ownership,
- parallel normal generation,
- custom allocator infrastructure.

The next renderer-facing shading step should consume immutable evaluated Corner normals and, once required by normal-mapped materials/export, add MikkTSpace-compatible Corner tangents as another derived stage.
