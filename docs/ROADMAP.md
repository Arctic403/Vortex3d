# Vortex3D Native Roadmap

This roadmap is ordered by dependency, not by what looks coolest on screen. Each foundation milestone has an exit gate. Do not advance by hiding broken foundations under more features.

## Foundation Milestone 0 — Native Core Bootstrap

**Goal:** prove Vortex exists independently of Android, browsers, and rendering.

Deliverables:

- C++20 static/shared library target.
- CMake build.
- Strong stable ID types.
- `Document` skeleton.
- Minimal object and mesh data-block registries.
- Native smoke tests through CTest.
- Zero Android/Web/Three.js/WASM dependencies in `vortex_core`.

Exit gate:

- Clean configure/build/test on a normal host.
- Create a document, allocate IDs, add/remove basic data-blocks, and validate ownership.
- Deterministic tests pass.

## Foundation Milestone 1 — Document v2

**Goal:** establish durable project state.

Deliverables:

- `Document`, `Scene`, `Object`, `Mesh`, `Material`, `Image`, `Collection`.
- 64-bit stable handles.
- Shared mesh data-blocks.
- Object hierarchy with cycle rejection.
- Explicit persistent vs editor/session state.
- Revision/change notification foundation.

Exit gate:

- Two objects can share one mesh.
- Make Unique correctly forks a mesh data-block.
- Hierarchy survives save/load round trip in tests.

## Foundation Milestone 2 — Mesh Kernel v2

**Goal:** make editable polygon topology trustworthy.

Deliverables:

- Vertex / Edge / Face / Corner domains.
- Radial edge-loop connectivity.
- N-gon support.
- Stable topology IDs.
- Generic attribute system.
- Mesh invariant validator.
- Trusted low-level topology mutation primitives.

Exit gate:

- Create and validate triangle, quad, n-gon, manifold and selected non-manifold cases.
- Topology-preserving operations retain IDs.
- Broken radial/face loops are caught by tests.

## Foundation Milestone 3 — Commands, Transactions, Undo

**Goal:** establish the only supported mutation pipeline.

Deliverables:

- Command interface.
- Atomic transactions and rollback.
- Undo/redo stack.
- Delta records for small edits.
- Snapshot/copy-on-write path for complex edits.
- Change/invalidation events.

Initial commands:

- create/delete object,
- set transform,
- link/make unique mesh,
- move vertices,
- assign material.

Exit gate:

- 100+ randomized undo/redo cycles return the document to byte/semantic-equivalent state where expected.
- Failed commands leave the document unchanged.

## Foundation Milestone 4 — Evaluation Graph

**Goal:** separate authored geometry from generated geometry.

Deliverables:

- Evaluated mesh type.
- Modifier interface.
- Dependency tracking.
- Dirty/revision propagation.
- Cache ownership rules.

First modifiers:

1. Transform
2. Mirror
3. Triangulate
4. Recalculate Normals
5. Bevel
6. Subdivision

Exit gate:

- Editing source geometry invalidates only required outputs.
- Toggling/removing modifiers never damages source topology.
- Repeated evaluation is deterministic.

## Foundation Milestone 5 — Native Project Format

**Goal:** own our authoring data safely.

Deliverables:

- Versioned project container.
- Schema version.
- Deterministic serialization where practical.
- Migration framework.
- Atomic save.
- Autosave/recovery journal design.
- Corruption detection.

Exit gate:

- Save -> kill -> reopen restores exact authored scene semantics.
- Older fixture schemas migrate forward in automated tests.

## Foundation Milestone 6 — Android Host

**Goal:** run the real engine in an APK with no WebView dependency.

Deliverables:

- Native Android project.
- Kotlin lifecycle/platform bridge.
- CMake/NDK integration.
- `armeabi-v7a` + `arm64-v8a` ABI builds.
- Storage Access Framework project open/save.
- app-private autosave/recovery.
- device capability and memory reporting.

Exit gate:

- Same core tests run for both ABIs in CI/build tooling where possible.
- App creates, saves, reopens a project using the native core.

## Foundation Milestone 7 — Vulkan Viewport

**Goal:** display evaluated scenes without coupling rendering to authoring.

Deliverables:

- Vulkan instance/device/surface/swapchain lifecycle.
- Capability negotiation rather than hard-coded feature assumptions.
- Camera + grid.
- evaluated mesh upload.
- depth buffer.
- basic PBR/material path.
- overlay/wireframe path.
- integer ID picking.
- GPU resource cache keyed by engine revisions.

Exit gate:

- Rotate/pan/zoom around multi-object scenes.
- Editing an object updates only affected GPU resources.
- Surface loss/orientation/background-resume paths survive reliably.

## Foundation Milestone 8 — Editor Framework

**Goal:** make native Vortex pleasant to operate.

Deliverables:

- Object/Edit modes.
- Selection controller.
- touch orbit/pan/zoom.
- move/rotate/scale gizmos.
- snapping framework.
- active/hover/selection overlays.
- command palette/tool registry.

Exit gate:

- No editor action directly mutates document internals.
- Every user-visible mutation appears as a valid undo step.

## Foundation Milestone 9 — Modeling Tool Set v1

**Goal:** become genuinely useful for game-asset modeling.

Priority tools:

1. Extrude
2. Inset
3. Merge
4. Dissolve
5. Fill
6. Subdivide / loop cut
7. Bevel
8. Bridge
9. normals/shading controls
10. Mirror workflow

Exit gate:

- Build a representative low-poly game prop entirely inside Vortex.
- Undo/redo and save/load survive the full workflow.

## Foundation Milestone 10 — UV, Materials, Import/Export

Deliverables:

- seams,
- generic UV layers,
- basic unwrap operations,
- image assets,
- metallic/roughness material model,
- hardened GLB import/export,
- round-trip validation fixtures.

Exit gate:

- Create asset -> UV/material -> GLB export -> re-import -> semantic validation.

## Foundation Milestone 11 — Procedural / Node Foundation

Deliverables:

- typed node sockets,
- dependency graph reuse,
- cached node outputs,
- partial invalidation,
- modifier stack compatibility.

Do not build this before the evaluator is proven.

## Foundation Milestone 12 — Plugin / Automation / AI Surface

Deliverables:

- query API for scene/document state,
- reflected command schema,
- safe command execution boundary,
- macro/script layer,
- plugin registration,
- AI planner integration using the same command system.

AI never receives a secret mutation backdoor.

# First meaningful product milestone

The first target that proves the architecture is real:

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

When this works entirely through the native architecture, Vortex3D has become a real modeling engine rather than a wrapped web editor.
