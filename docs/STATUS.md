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
- Native CTest smoke coverage expanded across Scene/Collection/Make Unique/change tracking/transactions.
- GitHub Actions Core CI added.
- Local configure/build/test sanity gate passes for Phase 0.1.

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

- [ ] Create `EditableMesh` module separate from `Document`.
- [ ] Define Vertex/Edge/Face/Corner storage with stable IDs.
- [ ] Define generic AttributeSet domains (`Vertex`, `Edge`, `Face`, `Corner`).
- [ ] Add topology validator result/diagnostic type.
- [ ] Add first trusted topology primitive and behavior tests.
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
