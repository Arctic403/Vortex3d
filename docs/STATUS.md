# Vortex3D Native Status

Last updated: 2026-09-03

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
- Initial attributes implemented: position, crease, sharp, seam, material index, UV, and corner normal.
- Mesh validator reports structured diagnostics for topology and attribute invariant failures.
- First trusted topology primitives implemented: add vertex, find/create edge, add polygon face.
- Authoring n-gons are supported; concave n-gon, shared-edge, non-manifold, and cube fixtures pass validation.
- Native CTest coverage contains Document/Transaction and Mesh Kernel suites.
- Local GCC-style warnings-as-errors build passes 2/2 tests.
- Local Clang build passes 2/2 tests.
- Local AddressSanitizer + UndefinedBehaviorSanitizer build passes 2/2 tests.
- Portable-core boundary scanner passes locally.
- GitHub CI now gates GCC, Clang, warnings-as-errors, ASan/UBSan, and the portable-core boundary.
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
- [ ] Add removal primitive(s) with deterministic attribute compaction rules.
- [ ] Add an edge-split primitive with an explicit ID-inheritance contract.
- [ ] Port donor topology fixtures as behavior tests, not source-copy architecture.
- [ ] Add randomized trusted-operation validation tests.

## Phase 0.3 — Portability gate

- [x] Add GCC + Clang CI coverage.
- [x] Add warnings-as-errors CI.
- [x] Add ASan/UBSan host test job.
- [x] Add formatting/static-analysis policy.
- [x] Add portable-core dependency scanner.
- [x] Confirm the current core has no Android/JNI/Vulkan/WebView/Emscripten/Three.js/platform-header dependency.
- [ ] Confirm the latest expanded GitHub Actions matrix completes successfully on GitHub-hosted runners.

## Next engineering target

The next kernel slice is **topology removal + edge split**. That is the point where stable-ID inheritance and attribute compaction stop being theoretical and become enforceable behavior.

After that, the first higher-level modeling operation should be built from trusted primitives rather than bypassing them.

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
