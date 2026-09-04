# Vortex3D Native Status

Last updated: 2026-09-04

## Current phase

**Phase 0 — Native Core Bootstrap**

## Completed

- Clean native repository established.
- MIT license retained.
- Architecture blueprint, staged roadmap, foundation rules, donor strategy, Android dual-ABI contract, project format, mesh-kernel contract, research index, and code policy written.
- CMake C++20 `vortex_core` target created.
- Strong typed 64-bit ID foundation created.
- Headless `Document` registry created with Scene/Collection/Object/Mesh data-blocks.
- Shared mesh references, public user-count query, and Make Unique implemented.
- Parent-cycle rejection, safe detachment, collection unlink-on-delete, and referenced-mesh deletion protection implemented.
- Monotonic Document revisions and queryable change events implemented.
- `Command` + snapshot-backed `Transaction` commit/rollback boundary implemented.
- Concrete document commands implemented for rename, parent changes, mesh assignment, and Make Unique.
- Explicit invalid-ID and allocator behavior coverage added.
- `EditableMesh` v2 created outside the Document layer.
- Stable Vertex/Edge/Face/Corner topology identities implemented.
- Face boundary `next/prev` cycles and edge radial Corner cycles implemented.
- Non-manifold radial topology with 3+ faces around one edge is supported and tested.
- Generic typed AttributeSet implemented with `(name, domain)` identity so the same semantic name can coexist on different domains.
- Generic deterministic attribute compaction, copying, and interpolation helpers implemented.
- Initial attributes implemented: position, crease, sharp, seam, material index, UV, and corner normal.
- Mesh validator reports structured diagnostics for topology and attribute invariant failures.
- Validator now proves each Corner edge connects that Corner to the next face Corner and every radial cycle covers all live uses of its edge.
- Trusted topology primitives implemented: add/remove vertex, add/remove edge, add/remove polygon face, and split edge.
- Face removal compacts Face/Corner attributes, rebuilds radials, and optionally removes unused affected edges without silently deleting vertices.
- Edge/vertex removal are conservative and reject still-referenced topology.
- Edge split has a locked stable-ID contract: original edge ID survives on `vertexA -> newVertex`; the second segment receives a fresh edge ID.
- Edge split interpolates vertex/corner attributes, copies edge attributes, updates every radial face, and supports boundary/manifold/non-manifold topology.
- Authoring n-gons are supported; concave n-gon, shared-edge, non-manifold, and cube fixtures pass validation.
- Native CTest coverage now contains Document/Transaction, Mesh Kernel, and Mesh Mutation suites.
- GCC, Clang, warnings-as-errors, AddressSanitizer, UndefinedBehaviorSanitizer, and portable-core boundary checks pass on GitHub-hosted runners for the edge-split/removal code.
- Portable core remains free of Android/JNI/Vulkan/WebView/Emscripten/Three.js/platform-header dependencies.
- `.clang-format`, `.clang-tidy`, and `docs/STYLE.md` establish the code-quality policy.

## Phase 0.1 — Document hardening

- [x] Grow lightweight native test harness.
- [x] Add explicit ID uniqueness/invalid-ID behavior tests.
- [x] Add concrete Command APIs for ordinary document mutations.
- [x] Add `Scene` and `Collection` data-blocks.
- [x] Add shared-mesh user-count/query API.
- [x] Add Make Unique behavior with tests.
- [x] Add document-level revision/change event model.
- [x] Add first Command/Transaction mutation boundary.

**Phase 0.1 is complete.**

> The current Transaction implementation intentionally snapshots the small Phase-0 Document. Before large mesh payloads land in undo state, this must evolve toward delta/copy-on-write history so 32-bit Android memory stays under control.

## Phase 0.2 — Mesh Kernel v2

- [x] Create `EditableMesh` separate from `Document`.
- [x] Define Vertex/Edge/Face/Corner storage with stable IDs.
- [x] Define generic AttributeSet domains.
- [x] Key generic attributes by name + domain.
- [x] Add topology validator result/diagnostic type.
- [x] Add first trusted topology primitives and tests.
- [x] Add shared-edge radial fixture.
- [x] Add non-manifold 3+ radial-face fixture.
- [x] Add concave n-gon fixture.
- [x] Add closed cube fixture.
- [x] Add removal primitives with deterministic attribute compaction rules.
- [x] Add edge-split primitive with explicit ID-inheritance contract.
- [x] Test edge split across a shared manifold edge.
- [x] Test edge split across a 3-face non-manifold radial edge.
- [x] Test vertex interpolation and edge-attribute inheritance during split.
- [ ] Port donor topology fixtures as behavior tests, not source-copy architecture.
- [ ] Add randomized trusted-operation validation tests.
- [ ] Add the next trusted topology primitive needed by the first higher-level modeling operation.

## Phase 0.3 — Portability gate

- [x] Add GCC + Clang CI coverage.
- [x] Add warnings-as-errors CI.
- [x] Add ASan/UBSan host test job.
- [x] Add formatting/static-analysis policy.
- [x] Add portable-core dependency scanner.
- [x] Confirm the current core has no Android/JNI/Vulkan/WebView/Emscripten/Three.js/platform-header dependency.
- [x] Confirm the expanded GitHub Actions matrix completes successfully on GitHub-hosted runners with the mutation kernel enabled.

**Phase 0.3 portability gates are established and green.**

## Next engineering target

The next kernel work is now **behavior hardening and composition**:

1. randomized trusted-operation validation,
2. donor topology fixtures rewritten as native contract tests,
3. choose and implement the next primitive required for a real higher-level modeling operation,
4. build that operation through the trusted kernel rather than bypassing topology contracts.

The leading higher-level target remains a simple **face extrude** path because it exercises duplication, side-face creation, transforms, stable selection identities, commands, and eventual undo without forcing us to build the entire modeling toolset first.

## First architecture milestone

```text
Launch APK
-> Create cube
-> Enter Edit Mode
-> Select face
-> Extrude
-> Undo
-> Redo
-> Add Mirror modifier
-> Save
-> Kill app
-> Reopen
-> Export GLB
-> Re-import and validate
```

We are intentionally building the engine underneath that workflow in dependency order.
