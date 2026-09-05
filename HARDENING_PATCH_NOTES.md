# Vortex3D Hardening Patch — 2026-09-05

This patch is intentionally surgical. It preserves the existing authored/evaluated/render architecture and addresses the five pre-modeling-loop risks identified during the clean-slate review.

## Mesh lookup scaling

- Added a derived undirected `(VertexId, VertexId) -> EdgeId` acceleration index to `EditableMesh`.
- `edgeBetween()` / duplicate-edge lookup is now average O(1) instead of a full ordered-edge scan.
- The index is updated on edge create/delete/split and rebuilt from authored edge records after deserialization.
- `validateStrict()` now detects missing/stale/mismatched acceleration-index entries.
- Added deliberate corruption coverage for the acceleration index.
- Added an uncapped shared-edge quad-grid creation benchmark.

Release benchmark on the patch host at requested scale 10,000:

- `edge_create`: 10,000 edges in ~1.76 ms, uncapped.
- `quad_grid_face_create`: 10,000 shared-edge quads / 20,200 edges in ~12.58 ms, uncapped.

These are host regression measurements, not Android-device guarantees.

## Project truth / docs drift

- Current-facing docs now identify project schema v3 as current.
- README no longer hard-codes the native suite count; CTest owns that truth.
- `docs/ROADMAP.md` uses `Foundation Milestone` naming to avoid colliding with historical Phase-6 transform delivery labels.
- Added `scripts/check_project_truth.py` and a CI job that verifies current schema markers and rejects a hard-coded current test count.

## Gizmo v2 hardening

- Added `vortex.gizmo.randomized.smoke`.
- 2,000 deterministic randomized cases cover parent rotation, non-uniform and negative parent scale, reflections, world/local quaternion composition, translation inversion and gizmo-basis orthonormality.
- Singular parent linear transforms are verified to reject translation composition cleanly.
- Android ARMv7 device torture requirements are now explicit in `android/README.md`.

## Vulkan hardening

- Static scene vertex/index buffers and the static grid now upload through host-visible staging buffers into device-local memory.
- Dynamic selection/gizmo overlay storage remains host-visible by design.
- Buffer-allocation failure paths now clean up partially-created Vulkan resources.
- The command pool enables per-command-buffer reset.
- Input-driven command refresh resets and re-records the existing swapchain command buffers in place instead of freeing/reallocating them every time.

A later renderer step can move camera/object state to per-frame buffers to reduce command re-recording further; that larger architecture change is deliberately deferred until profiling justifies it.

## Verification completed in this patch environment

- GCC warnings-as-errors host build: passed.
- GCC CTest: 30/30 passed.
- Clang warnings-as-errors host build: passed.
- Clang CTest: 30/30 passed.
- Clang ASan + UBSan CTest: 30/30 passed.
- Repository policy check: passed.
- Portable-core boundary check: passed.
- Project-truth check: passed.
- Release mesh/evaluation benchmark build and smoke/run: passed.

## Still requires external device/CI verification

This environment does not contain an Android SDK/NDK, so the modified Vulkan adapter could not be cross-compiled here. The repository's existing CI is already configured to build both `armeabi-v7a` and `arm64-v8a`; run that exact-head CI before merging. Gizmo System v2 also still requires the documented real ARMv7 interaction torture pass.

## Scope freeze after this patch

Do not expand dependency-graph/procedural/extension infrastructure for the next product slice unless a bug requires it. The next major milestone should be the usable modeling loop: Edit Mode -> topology selection -> Extrude/Inset -> transform selection -> Undo/Redo -> save/reopen.
