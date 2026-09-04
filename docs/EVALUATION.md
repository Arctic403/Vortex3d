# Evaluated Geometry Architecture

## Purpose

`EditableMesh` is durable authored state. `EvaluatedMesh` is rebuildable generated state for modifiers, derived geometry, and rendering.

The evaluator is downstream of the authoring core:

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

Evaluation starts with a deterministic one-to-one conversion from a validated `EditableMesh`.

It preserves:

- source `MeshId`,
- source mesh revision,
- source `VertexId` per evaluated vertex,
- source `EdgeId` per evaluated edge,
- source `FaceId` per evaluated face,
- source `CornerId` per evaluated corner,
- all current generic attribute layers,
- authored n-gon boundaries,
- face next/previous topology,
- radial edge topology.

The generated topology is independent from authored container addresses and hash nodes.

## Stable identity versus packed generated indices

Persistent public identity remains 64-bit and belongs to authored data.

Evaluated connectivity uses checked `std::uint32_t` packed indices:

```text
stable source ID
      |
      v
Evaluated record
      |
      +--> 32-bit packed generated connectivity
```

This is intentional.

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

## Modifier stack v0.1

`MeshModifier` is the non-destructive modifier contract.

Each modifier provides:

- a stable modifier type,
- a human-readable name,
- a deterministic configuration/revision token,
- an `apply(EvaluatedMesh&)` operation,
- focused structured failure information.

Modifier configurations are immutable values after construction in the current design. Editing modifier settings means constructing/replacing the configuration, which naturally changes its revision token.

Modifiers execute strictly in stack order. Order is semantically meaningful and is included in evaluation identity.

### Transform modifier

`TransformModifier` supports:

- translation,
- XYZ Euler rotation in radians,
- non-uniform scale.

The operation transforms evaluated vertex positions only. Authored positions are unchanged.

Normals, when present on Vertex or Corner domains, are transformed using inverse scale followed by the same rotation and normalization. Zero/near-zero scale components and non-finite transform values are rejected instead of producing undefined geometry.

Transform order is:

```text
position -> scale -> rotate X -> rotate Y -> rotate Z -> translate
```

This convention is part of the deterministic modifier contract and should not change casually.

### Mirror modifier v0.1

`MirrorModifier` is the first topology-generating modifier.

It supports:

- X, Y, or Z mirror axis,
- an explicit mirror-plane offset,
- deterministic duplication of the current evaluated vertices, edges, faces, and corners,
- reflected vertex positions,
- reflected Vertex/Corner normals when present,
- preserved generic attributes,
- stable source-ID mappings for every mirrored generated element.

Mirroring is performed on the **current evaluated input**, so Transform -> Mirror, Mirror -> Transform, and stacked Mirror configurations remain deterministic stack operations without touching authored topology.

Reflection reverses orientation. The mirrored half therefore reverses each face boundary cycle. A mirrored corner uses the mirrored copy of the source **previous corner's edge** so the corner's edge still connects that corner vertex to its new `next` vertex. Mirrored radial edge cycles are rebuilt deterministically from the generated edge uses rather than copying source radial pointers blindly.

Generated duplicates retain the same stable source IDs as the source elements they derive from. This is a many-generated-to-one-source mapping; generated packed indices distinguish individual evaluated instances.

#### No welding in v0.1

Vertices exactly on the mirror plane are still duplicated.

This is intentional. Mirror-plane welding requires explicit decisions for:

- merge tolerance,
- which generated vertex survives,
- edge/corner collapse behavior,
- UV and other corner-domain data,
- source mapping after merges,
- non-manifold results.

Those rules will be introduced as a separate tested patch instead of hiding an arbitrary epsilon inside the initial Mirror implementation.

## Error model

Evaluation reports focused structured errors:

- `MissingAuthoredMesh`,
- `InvalidSourceMesh`,
- `ElementCountOverflow`,
- `MissingTopologyReference`,
- `NullModifier`,
- `ModifierFailed`.

Modifier failures additionally report `ModifierApplyError` and the failing stack index. Current modifier errors include invalid transforms, invalid mirror configuration, generated-topology overflow, missing position data, and attribute-copy failure.

This remains intentionally small. The evaluator does not introduce an exception-heavy result framework.

## Revision and cache contract

Every evaluated snapshot exposes an `EvaluationCacheKey` containing:

```text
source MeshId
+ source MeshBlock revision
+ ordered modifier-stack revision
```

The modifier-stack revision is a deterministic hash of each modifier's stable type and configuration token in stack order.

Consequences:

- same authored revision + same ordered modifiers => same key,
- changing authored geometry => different key,
- changing modifier settings => different key,
- reordering modifiers => different key.

`EvaluationCacheKeyHash` exists so a future cache can use the key directly. **No retained evaluation cache is introduced yet.** Cache lifetime and memory budgets will be designed separately so 32-bit Android does not accumulate unbounded generated meshes.

Cache ownership belongs to the evaluation/render side, never to persistent authored topology.

## Modifier roadmap

Current order:

1. Transform **implemented**
2. Mirror **implemented without welding**
3. Mirror weld/merge rules
4. Triangulate
5. Recalculate Normals
6. Bevel
7. Subdivision

Topology-generating modifiers must preserve meaningful source mappings and must never write generated topology back into the authored mesh.

Do not add parallel evaluation until deterministic dependency and invalidation behavior is proven single-threaded.

## Testing gate

Evaluation tests now prove:

- quad/n-gon boundary preservation,
- source-ID mappings,
- copied generic attributes,
- generated connectivity references valid packed indices,
- source revision propagation,
- prior evaluated snapshots remain unchanged after authored mutation,
- command/undo-driven re-evaluation,
- Transform source immutability, position, and normal behavior,
- modifier order/settings change the cache key,
- authored revision changes the cache key without changing modifier-stack revision,
- null/invalid modifiers fail with structured diagnostics,
- Mirror doubles generated topology without welding,
- mirror-plane offsets and axes affect evaluation identity,
- mirrored source IDs remain mapped to authored identities,
- mirrored face winding and face-edge continuity remain valid,
- mirrored radial cycles remain internally consistent,
- reflected corner normals are preserved correctly,
- invalid Mirror configurations fail structurally.

The evaluator target is compiled by GCC, Clang, Android ARMv7, and Android ARM64 and is exercised under ASan/UBSan and clang-tidy through normal CI.
