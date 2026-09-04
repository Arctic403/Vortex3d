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
- Headless `Document` registry created with Mesh/Object data-blocks.
- Shared mesh references supported at the document level.
- Parent hierarchy cycle rejection implemented.
- Safe parent detachment on object deletion implemented.
- Referenced mesh deletion is rejected.
- Native CTest smoke test created.
- GitHub Actions Core CI added.
- Local configure/build/test sanity gate passes.

## Immediate next work

### Phase 0.1 — Harden IDs and Document

- [ ] Add unit-test framework or grow lightweight native test harness.
- [ ] Test ID uniqueness and invalid-ID behavior.
- [ ] Add object rename/update APIs through a first command boundary rather than uncontrolled setters.
- [ ] Add `Scene` and `Collection` skeletons.
- [ ] Add shared-mesh user-count/query API.
- [ ] Add Make Unique behavior with tests.
- [ ] Add document-level revision/change event model.

### Phase 0.2 — Prepare Mesh Kernel v2

- [ ] Create `EditableMesh` module separate from `Document`.
- [ ] Define topology storage slots/handles.
- [ ] Define generic AttributeSet API.
- [ ] Add validator result/diagnostic type.
- [ ] Port donor topology fixtures as behavior tests, not source-copy architecture.

### Phase 0.3 — Portability gate

- [ ] Add Clang and GCC CI coverage.
- [ ] Add sanitizer CI for host builds.
- [ ] Add formatting/static-analysis policy.
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
