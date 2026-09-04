# Vortex3D Native Status

Last updated: 2026-09-04

## Current engineering focus

**Commands, mesh transactions, and undo/redo.**

The portable Document foundation and Mesh Kernel v2 foundation are now far enough along to begin routing real modeling operations through command/history infrastructure. Android and Vulkan remain intentionally downstream.

## Completed foundation

- Clean native repository established under MIT.
- Architecture blueprint, staged roadmap, foundation rules, donor strategy, Android dual-ABI contract, project format, mesh-kernel contract, research index, and code policy written.
- CMake C++20 `vortex_core` target created.
- Strong typed 64-bit ID foundation created.
- Headless `Document` registry created with Scene/Collection/Object/Mesh data-blocks.
- Shared mesh references, public user-count query, and Make Unique implemented.
- Parent-cycle rejection, safe detachment, collection unlink-on-delete, and referenced-mesh deletion protection implemented.
- Monotonic Document revisions and queryable change events implemented.
- `Command` + snapshot-backed Document `Transaction` commit/rollback boundary implemented.
- Concrete document commands implemented for rename, parent changes, mesh assignment, and Make Unique.
- Explicit invalid-ID and allocator behavior coverage added.
- `EditableMesh` v2 created outside the Document layer.
- Stable Vertex/Edge/Face/Corner topology identities implemented.
- Face boundary `next/prev` cycles and edge radial Corner cycles implemented.
- Non-manifold radial topology with 3+ faces around one edge is supported and tested.
- Generic typed AttributeSet implemented with `(name, domain)` identity.
- Deterministic attribute compaction, copying, and interpolation helpers implemented.
- Initial attributes implemented: position, crease, sharp, seam, material index, UV, and corner normal.
- Structured mesh validator implemented and hardened around face-edge continuity and complete radial coverage.
- Trusted primitives implemented: add/remove vertex, add/remove edge, add/remove polygon face, and split edge.
- Face removal compacts Face/Corner attributes, rebuilds radials, and optionally removes unused affected edges without silently deleting vertices.
- Edge split has a locked stable-ID contract and supports boundary/manifold/non-manifold topology.
- First higher-level modeling operation implemented: `extrudeFace(face, offset)`.
- Face extrusion supports isolated faces, faces attached to closed topology, and concave n-gons.
- Extrusion copies vertex/face attributes, inherits cap/side corner data deterministically, and returns explicit source-to-new-topology mappings.
- Extrusion is atomic in Phase 0/early Phase 3 via rollback to a pre-operation mesh snapshot on failure.
- Deterministic randomized kernel torture testing performs hundreds of create/split/remove/move steps with validation after every step.
- Behavior from `Vortex3dGm` has begun moving into native contract tests rather than source/architecture copies: persistent topology identity, explicit polygon loops, edge flags, face material data, and corner UV behavior are covered.
- CTest now registers six native suites: Document/Transaction, Mesh Kernel, Mesh Mutation, Face Extrude, Randomized Kernel, and Donor Contract.
- GCC, Clang, warnings-as-errors, AddressSanitizer, UndefinedBehaviorSanitizer, and the portable-core dependency scan pass on GitHub-hosted runners with all six suites enabled.
- Portable core remains free of Android/JNI/Vulkan/WebView/Emscripten/Three.js/platform-header dependencies.
- `.clang-format`, `.clang-tidy`, and `docs/STYLE.md` establish the code-quality policy.

## Document foundation

- [x] Scene and Collection data-blocks.
- [x] Shared mesh user-count/query API.
- [x] Make Unique behavior.
- [x] Document revision/change events.
- [x] Concrete command boundary for ordinary document changes.
- [x] Snapshot-backed transaction rollback for the still-small Document.
- [x] Invalid-ID and allocator behavior coverage.

> Whole-Document snapshots are temporary. Before large authored meshes enter document undo, history must move toward deltas/copy-on-write so `armeabi-v7a` remains viable.

## Mesh Kernel v2 foundation

- [x] Vertex/Edge/Face/Corner storage with stable IDs.
- [x] Generic domain-qualified attributes.
- [x] Structured topology diagnostics.
- [x] Quad, concave n-gon, cube, shared-edge and non-manifold fixtures.
- [x] Conservative removal primitives.
- [x] Deterministic attribute compaction.
- [x] Stable-ID edge split.
- [x] Shared-manifold and 3-face non-manifold split coverage.
- [x] Vertex interpolation and edge-attribute inheritance on split.
- [x] Deterministic randomized mutation validation.
- [x] Initial donor behavior contracts rewritten as native tests.
- [x] First composed modeling operation: face extrude.

**The Mesh Kernel v2 foundation gate is considered complete.** New topology primitives will now be added because real tools require them, not as speculative API surface.

## Portability gate

- [x] GCC + Clang CI.
- [x] Warnings-as-errors.
- [x] ASan/UBSan.
- [x] Formatting/static-analysis policy.
- [x] Portable-core dependency scanner.
- [x] No Android/JNI/Vulkan/WebView/Emscripten/Three.js/platform-header dependency in the portable core.
- [x] Full six-suite matrix green on GitHub-hosted runners.

## Next engineering target

The next slice is **mesh-level command/history infrastructure**:

1. define a mesh command result/mapping contract,
2. make vertex movement and face extrusion executable through commands rather than direct editor-facing mutation calls,
3. add undo/redo for mesh operations,
4. avoid permanent whole-mesh history snapshots by introducing reversible deltas or copy-on-write records,
5. torture undo/redo with deterministic randomized sequences,
6. then add the next modeling operation (likely inset or face/region duplication support) through the same command pipeline.

The key 32-bit rule is now active: correctness snapshots are fine for tests/temporary atomic rollback, but production history may not retain a full copy of a large mesh per edit.

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

The native kernel can now perform the **Extrude** part headlessly. The immediate objective is making that same operation command-driven and undoable before Android UI work begins.
