# Mesh Validation and Corruption Testing

## Role of validation

`EditableMesh::validate()` is the structural correctness gate for authored polygon topology. Modeling operations may use faster local preconditions, but topology-changing operations must leave the mesh in a state accepted by the validator.

Validation is intentionally deterministic and diagnostic. A failure returns one or more `MeshValidationIssue` records rather than only a boolean.

Current diagnostic categories cover:

- missing topology elements,
- faces with fewer than three corners,
- broken face next/previous cycles,
- broken edge radial cycles,
- invalid edge endpoints,
- corner/edge boundary mismatches,
- attribute-domain size mismatches,
- live corners unreachable from their owning face.

## Deliberate corruption fixtures

Normal public mutation APIs prevent many malformed states from being constructed. Therefore test builds enable the narrow `VORTEX_ENABLE_TEST_HOOKS` friend access used by `mesh_validation_corruption_smoke.cpp`.

The hook exists only when `VORTEX_BUILD_TESTS=ON`. Production, benchmark-only, and Android cross-compile builds do not expose it.

The corruption suite deliberately creates:

- a broken face cycle,
- a broken radial cycle,
- an invalid edge endpoint,
- an invalid two-corner face record,
- an orphan/unreachable live corner,
- an attribute-domain size mismatch.

It also verifies that public APIs reject duplicate/reversed duplicate edges, self-edges, missing endpoints, and illegal deletion order.

Tests assert the relevant structured diagnostic code instead of merely checking that validation returned false.

## Mutation testing policy

Validation coverage has three complementary layers:

1. hand-authored valid manifold and non-manifold fixtures,
2. deliberate invalid/corrupted fixtures targeting individual invariants,
3. deterministic randomized mutation sequences that validate after every step.

Future fuzzing should build on the same public mutation API and deterministic seed reproduction. Any discovered crash or invalid state becomes a permanent minimal regression fixture before the fuzzer is expanded further.

## Performance rule

Full validation is a correctness oracle, not a requirement that every future interactive hot path perform a complete O(mesh) scan. As the editor matures, trusted low-level operations may use local invariant checks and run full validation in tests/debug paths or at appropriate transaction boundaries.

Any reduction in validation frequency must be justified by tests and benchmarks; it must not weaken the invariants themselves.
