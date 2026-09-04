# Evaluated Geometry Architecture

## Purpose

`EditableMesh` is durable authored state. `EvaluatedMesh` is rebuildable generated state for modifiers, derived geometry, and rendering.

```text
Document / MeshBlock
        |
        v
EditableMesh          durable authored topology
        |
        v
vortex_eval
        |
        v
EvaluatedMesh         generated topology/attributes
        |
        v
ordered modifier stack
        |
        +--> future triangulation
        +--> future renderer upload
```

Nothing in `vortex_core` depends on the evaluator or renderer.

## Current evaluated representation

Evaluation starts with a deterministic one-to-one conversion from a validated `EditableMesh` and preserves source `MeshId`, source mesh revision, source Vertex/Edge/Face/Corner IDs, generic attribute layers, authored n-gon boundaries, face cycles, and radial edge topology.

Generated topology is independent from authored container addresses and hash nodes.

## Stable identity versus packed generated indices

Persistent public identity remains 64-bit and belongs to authored data. Evaluated connectivity uses checked `std::uint32_t` packed indices.

```text
stable source ID
      |
      v
Evaluated record
      |
      +--> 32-bit packed generated connectivity
```

- Packed indices are never serialized as persistent identity.
- Re-evaluation may repack generated geometry freely.
- Picking/diagnostics can map generated elements back to stable source IDs.
- One source ID may map to multiple generated elements after topology-generating modifiers.
- 32-bit connectivity reduces generated topology memory and is friendly to the required ARMv7 target.
- Evaluation/modifiers fail explicitly rather than silently truncating generated indices.

## Read-only external contract

`EvaluatedMesh` exposes const spans and const attributes to normal consumers. There is no editor mutation API.

Modifiers receive controlled internal mutation access through the evaluation layer only. Editor operations still modify authored data exclusively through commands/history.

An older evaluated snapshot remains valid as its own value until discarded; later authored edits or modifier evaluations do not silently mutate it.

## Modifier stack

`MeshModifier` is the non-destructive modifier contract. Each modifier provides a stable type, human-readable name, deterministic configuration token, `apply(EvaluatedMesh&)`, and focused structured failure information.

Modifier configurations are immutable values after construction in the current design. Modifiers execute strictly in stack order, and order is included in evaluation identity.

### Transform modifier

`TransformModifier` supports translation, XYZ Euler rotation in radians, and non-uniform scale. Authored positions remain unchanged.

Normals, when present on Vertex or Corner domains, use inverse scale followed by the same rotation and normalization. Zero/near-zero scale components and non-finite transform values are rejected.

```text
position -> scale -> rotate X -> rotate Y -> rotate Z -> translate
```

### Mirror modifier v0.2

`MirrorModifier` is the first topology-generating modifier. It supports X/Y/Z axes, an explicit plane offset, source-ID preserving generated topology, reflected positions/normals, reversed mirrored winding, and optional deterministic seam welding.

Mirror operates on the **current evaluated input**, so it can stack after Transform or another Mirror without touching authored topology.

#### No-weld mode

Welding is disabled by default. No-weld behavior remains compatible with Mirror v0.1: vertices, edges, faces, and corners are duplicated, including vertices exactly on the plane.

#### Weld configuration

Welding is controlled by:

```cpp
MirrorWeldSettings {
    bool enabled;
    float tolerance;
};
```

There is no hidden epsilon. A vertex welds when:

```text
abs(axis_coordinate - plane_offset) <= tolerance
```

Tolerance must be finite and non-negative. Exact-only welding is represented by `tolerance == 0`.

#### Deterministic seam rules

When welding is enabled:

1. The source evaluated vertex is the deterministic survivor.
2. A surviving seam vertex is projected exactly onto the configured plane in **evaluated output only**.
3. The mirrored duplicate of that vertex is not created.
4. A source edge whose two endpoints both weld reuses the source evaluated edge instead of creating a duplicate seam edge.
5. A face whose entire boundary welds to the plane is not duplicated back onto itself.
6. Other mirrored faces retain reversed winding.
7. Mirrored Corner attributes are copied according to the reversed source-corner order, preserving face-varying data such as UVs.
8. Mirrored Vertex/Corner normal data is reflected when present.
9. Radial rings are rebuilt globally after seam reuse. A seam edge may therefore become a normal two-face ring or a supported non-manifold ring with 3+ uses.
10. Stable authored IDs are never changed. Generated mirrored elements retain the source IDs they derive from; welded seam elements simply reuse the surviving evaluated element.

Welding is **not** broad spatial deduplication. It only decides whether each source vertex and its own mirrored counterpart collapse at the configured mirror plane. Symmetric but unrelated geometry away from the seam is not merged.

## Error model

Evaluation reports focused structured errors such as missing/invalid authored geometry, generated index overflow, missing topology references, null modifiers, and modifier failure.

Modifier failures additionally report `ModifierApplyError` and the failing stack index. Mirror-specific failures distinguish invalid axis/plane configuration, invalid weld settings, generated-topology overflow, invalid generated topology, missing position data, and attribute-copy failure.

The evaluator intentionally avoids an exception-heavy result architecture.

## Revision and cache contract

Every evaluated snapshot exposes:

```text
source MeshId
+ source MeshBlock revision
+ ordered modifier-stack revision
```

The modifier-stack revision hashes stable modifier type and configuration in stack order. Mirror axis, plane offset, weld enabled state, and weld tolerance all participate in the modifier configuration token.

Consequences:

- same authored revision + same ordered modifiers => same key,
- authored changes => different key,
- modifier setting changes => different key,
- weld setting/tolerance changes => different key,
- modifier reordering => different key.

`EvaluationCacheKeyHash` exists for a future cache. **No retained evaluation cache exists yet.** Cache lifetime and byte budgets will be designed separately for 32-bit Android.

## Modifier roadmap

1. Transform **implemented**
2. Mirror no-weld **implemented**
3. Mirror weld/merge v0.2 **implemented**
4. Triangulate
5. Recalculate Normals
6. Bevel
7. Subdivision

Topology-generating modifiers must preserve meaningful source mappings and must never write generated topology back into authored meshes.

Do not add parallel evaluation until deterministic dependency and invalidation behavior is proven single-threaded.

## Testing gate

Evaluation coverage now proves authored/evaluated separation, source mappings, packed generated topology, Transform behavior, modifier ordering/cache identity, Mirror no-weld topology, and Mirror weld behavior including:

- configurable tolerance,
- exact-only tolerance,
- source evaluated vertex survival,
- projection to the mirror plane without authored mutation,
- seam-edge reuse,
- fully planar face suppression,
- reversed mirrored Corner/UV mapping,
- reflected normals,
- manifold two-use seam radials,
- supported four-use non-manifold seam radials,
- invalid tolerance diagnostics.

The evaluator target is compiled by GCC, Clang, Android ARMv7, and Android ARM64 and is exercised under ASan/UBSan and clang-tidy through normal CI.
