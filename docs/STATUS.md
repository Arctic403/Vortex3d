# Vortex3D Native Status

Last updated: 2026-09-03

## Current phase

**Phase 0 — Native Core Bootstrap**

## Completed

- Clean native repository established.
- MIT license retained.
- Architecture blueprint written.
- Staged roadmap written.
- Foundation engineering rules written.
- Vortex3dGm donor strategy documented.
- Android dual-ABI (`armeabi-v7a` + `arm64-v8a`) contract documented.
- Mesh kernel contract documented.
- Native project-format contract documented.
- Research/source index written.
- CMake C++20 `vortex_core` target created.
- Strong typed 64-bit ID foundation created.
- Headless `Document` registry created.
- `Scene`, root `Collection`, nested Collection, Object, and Mesh data-blocks implemented.
- Shared mesh references and public mesh user-count query implemented.
- Make Unique clones shared Mesh data and rebinds only the requested Object.
- Parent hierarchy cycle rejection implemented.
- Safe parent detachment on object deletion implemented.
- Object deletion safely removes Collection membership.
- Referenced mesh deletion is rejected.
- Document-level monotonic revisions and queryable change events implemented.
- First `Command` interface implemented.
- Snapshot-backed `Transaction` boundary implemented with commit, explicit rollback, failed-command rollback, and destructor rollback.
- `EditableMesh` v2 module created outside the Document layer.
- Stable Vertex/Edge/Face/Corner identities implemented.
- Face boundary `next/prev` loops implemented.
- Edge radial Corner cycles implemented, including shared-edge faces.
- Generic typed AttributeSet implemented for Vertex/Edge/Face/Corner domains.
- Initial attributes implemented: position, crease, sharp, seam, material index, UV, corner normal.
- Mesh validator now reports structured diagnostics for topology and attribute invariant failures.
- First trusted topology primitives implemented: add vertex, find/create edge, add polygon face.
- N-gon authoring topology is supported by `addFace`.
- Native CTest coverage now includes Document/Transaction and Mesh Kernel suites.
- Local configure/build/test sanity gate passes with 2/2 tests.
- GitHub Actions Core CI added.

## Immediate next work

### Phase 0.1 — Document hardening

- [x] Grow lightweight native test harness.
- [ ] Add explicit ID uniqueness and invalid-ID behavior tests.
- [ ] Add rename/update commands so ordinary mutations start moving behind Command APIs.
- [x] Add `Scene` and `Collection` skeletons.
- [x] Add shared-mesh user-count/query API.
- [x] Add Make Unique behavior with tests.
- [x] Add document-level revision/change event model.
- [x] Add first Command/Transaction mutation boundary.

> The current Transaction implementation intentionally snapshots the small Phase-0 Document. Before large mesh payloads land, this will evolve toward delta/copy-on-write undo storage so 32-bit Android memory stays under control.

### Phase 0.2 — Prepare Mesh Kernel v2

- [x] Create `EditableMesh` module separate from `Document`.
- [x] Define Vertex/Edge/Face/Corner storage with stable IDs.
- [x] Define generic AttributeSet domains (`Vertex`, `Edge`, `Face`, `Corner`).
- [x] Add topology validator result/diagnostic type.
- [x] Add first trusted topology primitives and behavior tests.
- [ ] Add explicit non-manifold (3+ faces around one edge) fixture.
- [ ] Add concave n-gon and cube fixtures.
- [ ] Add remove topology primitive(s) with attribute compaction rules.
- [ ] Port donor topology fixtures as behavior tests, not source-copy architecture.

### Phase 0.3 — Portability gate

- [ ] Add Clang and GCC CI coverage.
- [ ] Add ASan/UBSan host test job.
- [ ] Add formatting/static-analysis policy.
- [ ] Confirm no platform headers/dependencies leak into `vortex_core`.
- [ ] Introduce Android only after the portable core boundary remains clean.

## First architecture milestone

The first major native proof remains:

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
