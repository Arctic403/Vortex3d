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
- 32-bit connectivity reduces generated topology memory and is friendly to the required ARMv7 target.
- Evaluation fails with `MeshEvaluationError::ElementCountOverflow` rather than silently truncating an index.

## Read-only external contract

`EvaluatedMesh` exposes const spans and const attributes to normal consumers. There is no editor mutation API.

Modifiers receive controlled internal mutation access through the evaluation layer only. Editor operations still modify authored data exclusively through commands/history.

An older evaluated snapshot remains valid as its own value until discarded; later authored edits or modifier evaluations do not silently mutate it.

## Modifier stack v0.1

`MeshModifier` is the first non-destructive modifier contract.

Each modifier provides:

- a stable modifier type,
- a human-readable name,
- a deterministic configuration/revision token,
- an `apply(EvaluatedMesh&)` operation,
- focused structured failure information.

Modifier configurations are immutable values after construction in the current design. Editing modifier settings means constructing/replacing the configuration, which naturally changes its revision token.

Modifiers execute strictly in stack order. Order is semantically meaningful and is included in evaluation identity.

### Transform modifier

`TransformModifier` is the first implemented modifier.

It supports:

- translation,
- XYZ Euler rotation in radians,
- non-uniform scale.

The operation transforms evaluated vertex positions only. Authored positions are unchanged.

Normals, when present on Vertex or Corner domains, are transformed using inverse scale followed by the same rotation and normalization. Zero/near-zero scale components and non-finite transform values are rejected instead of producing undefined geometry.

Transform order is:

```text
position -> scale -> rotate X -> rotate Y -> rotate Z -> translate
```

This convention is now part of the deterministic modifier contract and should not change casually.

## Error model

Evaluation reports focused structured errors:

- `MissingAuthoredMesh`,
- `InvalidSourceMesh`,
- `ElementCountOverflow`,
- `MissingTopologyReference`,
- `NullModifier`,
- `ModifierFailed`.

Modifier failures additionally report `ModifierApplyError` and the failing stack index.

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
2. Mirror
3. Triangulate
4. Recalculate Normals
5. Bevel
6. Subdivision

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
- Transform does not mutate authored data,
- Transform position and normal behavior,
- modifier order changes output,
- modifier order/settings change the cache key,
- authored revision changes the cache key without changing the modifier-stack revision,
- null/invalid modifiers fail with structured diagnostics.

The evaluator target is compiled by GCC, Clang, Android ARMv7, and Android ARM64 and is exercised under ASan/UBSan and clang-tidy through normal CI.
