# Vortex3D Validation Contract

## Purpose

Validation is a correctness boundary, not a substitute for normal mutation logic. Vortex3D keeps authored geometry and evaluated geometry separate, so each representation has its own validator and invariants.

The hardening priority is:

1. reject structurally inconsistent authored state before evaluation/import-style trust boundaries,
2. reject malformed generated topology before it reaches cache, renderer, export, or later modifiers,
3. return structured diagnostics instead of relying on crashes or undefined behavior,
4. keep production validation portable across 32-bit ARMv7 and 64-bit targets.

## Authored mesh validation

`EditableMesh` exposes two validation levels.

### `validate()`

`validate()` is the existing topology/attribute validator used by current mutation paths. It checks the authored graph itself, including:

- missing elements,
- invalid face sizes,
- broken face cycles,
- broken radial cycles,
- invalid edge endpoints,
- corner/edge mismatches,
- attribute-size mismatches,
- unreachable corners.

This remains useful inside correctness-first editing operations without changing their established validation contract in the Phase 2 patch.

### `validateStrict()`

`validateStrict()` is the full structural trust-boundary validator. It performs the legacy checks and also verifies the redundant authored storage views agree.

For every Vertex/Edge/Face/Corner domain it verifies:

- order-vector size, packed-index-map size, and registry size agree,
- ordered stable IDs are non-zero and unique,
- every ordered ID exists in the registry,
- every packed-index entry points to the same ID at the same packed position,
- every registry record has a packed-index entry,
- every registry record's stored ID matches its registry key,
- `nextElementId_` is non-zero and strictly above every live stable element ID,
- no two authored edges connect the same unordered vertex pair,
- a face cannot satisfy `cornerCount` by repeating a shorter corner cycle.

New structured authored diagnostics include:

- `StorageSizeMismatch`,
- `DuplicateElementId`,
- `IndexMapMismatch`,
- `ElementIdentityMismatch`,
- `InvalidAllocatorState`,
- `DuplicateEdge`.

`MeshEvaluator` requires `validateStrict()` before authored geometry enters evaluated packing. Future import/deserialization paths must use the same strict boundary before trusting reconstructed internal storage.

Public mesh editing APIs remain responsible for maintaining invariants as they mutate. The strict validator is not permission to let internal storage drift and repair it later.

## Evaluated mesh validation

`EvaluatedMeshValidator` is the centralized validator for rebuildable packed geometry.

It checks:

- non-zero runtime Document and source Mesh identity,
- attribute domain sizes exactly matching generated topology domains,
- generated domain counts staying within the uint32 packed-index contract,
- edge endpoints in range and non-self edges,
- no duplicate unordered packed edge pair,
- corner vertex/edge/next/prev/radial references in range,
- faces having at least three corners,
- exactly `cornerCount` distinct corners per face cycle,
- face-cycle closure and mutual next/prev consistency,
- each corner belonging to at most one face cycle,
- each corner edge connecting the current vertex to the next face vertex,
- radial rings covering every corner that uses an edge exactly once,
- mutual radial-next/radial-prev consistency,
- every generated corner being reachable from a face.

Structured evaluated diagnostics are represented by `EvaluatedMeshValidationCode`, including:

- `InvalidSourceIdentity`,
- `AttributeSizeMismatch`,
- `ElementCountOverflow`,
- `InvalidEdgeEndpoints`,
- `DuplicateEdge`,
- `InvalidFaceSize`,
- `BrokenFaceCycle`,
- `BrokenRadialCycle`,
- `InvalidCornerReference`,
- `CornerEdgeMismatch`,
- `UnreachableCorner`,
- `CornerUsedByMultipleFaces`.

## Evaluation gates

The evaluator validates generated geometry at deterministic boundaries:

```text
EditableMesh
  -> validateStrict()
  -> authored-to-evaluated packing
  -> EvaluatedMeshValidator
  -> modifier 0
  -> EvaluatedMeshValidator
  -> modifier 1
  -> EvaluatedMeshValidator
  -> ...
  -> DerivedNormalsGenerator
  -> EvaluatedMeshValidator
  -> immutable evaluated snapshot
```

A modifier that returns success but leaves malformed generated topology is converted into:

```text
MeshEvaluationError::ModifierFailed
ModifierApplyError::GeneratedTopologyInvalid
modifierIndex = failing stack index
evaluatedValidationCode = first structural failure
```

A malformed packed conversion or final post-derived snapshot reports `MeshEvaluationError::InvalidEvaluatedMesh` plus the first evaluated validation code.

`EvaluationCache` forwards the same structured validation diagnostic on evaluation failure. Calling through the cache must not erase information that direct evaluation would have returned.

## Cache interaction

Evaluation-cache identity remains revision based:

```text
RuntimeDocumentId
+ MeshId
+ authored Mesh revision
+ ordered modifier-stack revision
```

Normal authored mutation is available only through revision-advancing Document/command paths, so a cache hit does not re-run the full validator. If future systems introduce a new mutation/import path, that path must preserve revision discipline and the strict validation boundary rather than mutating resident authored storage behind the cache.

## Deliberate corruption tests

Production code does not expose corruption APIs. Test builds use the narrow `VORTEX_ENABLE_TEST_HOOKS` boundary to create states that normal public APIs refuse to produce.

Phase 2 regression coverage deliberately corrupts authored storage to prove diagnostics for:

- duplicate ordered stable IDs,
- incorrect packed-index entries,
- registry/order size disagreement,
- registry record identity mismatch,
- duplicate undirected edges,
- allocator rewind,
- repeated short face cycles.

Evaluated regression coverage deliberately corrupts:

- edge endpoint ranges,
- duplicate generated edges,
- face cycles,
- radial references,
- attribute domain sizes,
- orphan generated corners.

It also proves that a deliberately corrupt modifier cannot leak a success result through either direct `MeshEvaluator` use or `EvaluationCache`.

Validation coverage still includes deterministic randomized public mutation sequences, so both deliberately invalid state and long valid mutation histories remain exercised.

## Performance policy

Strict validation is correctness-first and currently scans complete mesh domains. It is not run as a hidden per-element micro-operation.

Do not weaken invariants to improve benchmark numbers. If profiling later shows validation cost dominates large imports/evaluations, optimize the implementation while preserving the exact validation contract and structured diagnostics.

## Threading and portability

Validation follows the current single-threaded authoring/evaluation contract. The validator introduces no platform APIs, JNI, Vulkan, WebView, or persistent pointer-sized identity.

The same code must compile under both required Android ABIs:

- `armeabi-v7a` / 32-bit,
- `arm64-v8a` / 64-bit.
