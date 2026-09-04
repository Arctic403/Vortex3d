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
EvaluatedMesh         read-only generated topology
        |
        +--> future modifiers
        +--> future triangulation
        +--> future renderer upload
```

Nothing in `vortex_core` depends on the evaluator or renderer.

## Current evaluated representation

The first evaluator performs a deterministic one-to-one conversion from a validated `EditableMesh`.

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

## Read-only rule

`EvaluatedMesh` exposes const spans and const attributes. There is no normal editor mutation API.

Editor operations modify authored data through commands/history. A new source revision then produces a new evaluated snapshot.

An older evaluated snapshot remains valid as its own value until discarded; it is never silently mutated to mirror later source edits.

## Error model

Evaluation currently reports focused structured errors:

- `MissingAuthoredMesh`,
- `InvalidSourceMesh`,
- `ElementCountOverflow`,
- `MissingTopologyReference`.

This remains intentionally small. The evaluator does not introduce an exception-heavy result framework.

## Revision and cache contract

`EvaluatedMesh::sourceRevision()` records the `MeshBlock` revision that produced the snapshot.

A future cache may reuse an evaluated result only when its complete input key still matches. At minimum that key includes:

- source `MeshId`,
- source revision,
- modifier-stack revision/configuration once modifiers exist.

Cache ownership belongs to the evaluation/render side, never to persistent authored topology.

## Modifier rules

Future modifiers consume immutable evaluated input and produce new evaluated output or a controlled mutable build buffer internal to evaluation.

Initial modifier order remains:

1. Transform
2. Mirror
3. Triangulate
4. Recalculate Normals
5. Bevel
6. Subdivision

Do not add parallel evaluation until deterministic dependency and invalidation behavior is proven single-threaded.

## Testing gate

The evaluator smoke test proves:

- quad/n-gon boundary preservation,
- source-ID mappings,
- copied generic attributes,
- generated connectivity references valid packed indices,
- source revision propagation,
- a prior evaluated snapshot is unchanged after authored mutation,
- re-evaluation reflects a command-driven vertex move,
- undo followed by re-evaluation restores the authored position.

The evaluator target is compiled by GCC, Clang, Android ARMv7, and Android ARM64 and is exercised under ASan/UBSan and clang-tidy through normal CI.
