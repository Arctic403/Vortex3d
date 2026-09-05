# Pre-Phase 6 Readiness Audit

Date: 2026-09-04

Phase 5 is complete and has been verified on the 32-bit Android target. This audit checks the engine/editor ownership boundary, Android input/JNI path, Vulkan selection path, project persistence implications, undo architecture, and the renderer changes required before interactive object transforms are added.

## Result

The foundation is ready for Phase 6. No rewrite is required.

The audit did find several small correctness/hardening gaps in the Stage 5B bridge. They are fixed in the pre-Phase-6 hardening branch before transform work begins:

- `ViewportHost` now verifies that each object snapshot is extracted from the mesh actually referenced by that persistent `ObjectId`.
- extracted snapshots are verified to belong to the host `Document` and requested `MeshId` before crossing into Vulkan-facing state.
- object selection changes now roll the editor selection back if the renderer rejects the corresponding active-object overlay update, preventing editor/renderer selection divergence.
- the Stage 5B render batch now rejects mixed-document snapshots, missing mesh identity, non-finite gizmo origins, and geometry that would overflow the renderer's 32-bit index space.

These are hardening fixes, not architecture changes.

## Verified ownership boundary

The current path is correct:

```text
Android touch
    -> ViewportHost
        Document
        EditorHistory
        EditorContext
    -> MeshEvaluator
    -> RenderExtractor
    -> VulkanViewport derived render/pick state
```

`Document` remains authored truth. `EditorContext` owns active selection. Vulkan owns only rebuildable GPU/render/pick caches. Stable `ObjectId` and source topology IDs cross the render boundary; pointers, packed authoring indices, and Vulkan handles do not.

## Phase 6 engine contract

Phase 6 should add object transforms to portable authored engine state, not to Android or Vulkan state.

The intended object transform is local-to-parent state with identity defaults:

```text
translation = (0, 0, 0)
rotationRadians = (0, 0, 0)
scale = (1, 1, 1)
```

Because `ObjectBlock` already supports `parentId`, Phase 6 must define and test parent-chain world transform composition instead of treating every object transform as world-space state. Existing parent-cycle prevention remains the structural safety boundary.

Transform mutation should go through `Document` APIs and document commands so object revision/change events and `EditorHistory` stay authoritative. Gizmo drag previews may use transient tool state, but commit/cancel must resolve to an undoable authored command rather than writing Vulkan state directly.

## Undo/redo contract

`EditorHistory` already provides the correct unified chronological history. Phase 6 should add a compact object-transform document delta/command rather than snapshotting a `Document` or duplicating history inside the viewport.

Required tests include:

- transform command apply;
- undo and redo;
- no-op transform behavior;
- invalid/non-finite transform rejection;
- history ordering with existing document and mesh commands;
- parented-object transform behavior.

## Project format contract

The current project codec schema writes object ID, name, mesh, parent, and revision only. Adding persistent transform fields is a file-format change.

Phase 6 must therefore update the project schema deliberately and add round-trip/corruption coverage. Existing schema-v1 files must not silently decode with shifted fields. Prefer an explicit new schema version with a compatibility read path that supplies identity transforms for v1 objects.

## Renderer contract for transforms

Stage 5B deliberately batches static object geometry into one draw. That is sufficient for selection but should not become the long-term transform model.

Phase 6 should keep evaluated mesh vertices in object-local space and attach a derived model/world matrix to each visible object draw item. A shared GPU vertex/index allocation may still be used, but the renderer needs per-object draw ranges so one object's transform can change without rebaking unrelated mesh vertices.

Picking and selection overlays must use the same object transform as rendering. The renderer may cache inverse model matrices or other derived values, but authored translation/rotation/scale stays in the engine.

The existing command-buffer re-record path is acceptable for the first transform milestone on the tiny test scene. It is known performance debt; a later per-frame camera/object buffer can remove frequent re-recording without changing authored ownership.

## Phase 6 entry gate

Phase 6 may begin when this hardening PR is green on:

- repository policy and portable-boundary checks;
- GCC and Clang host tests;
- ASan + UBSan;
- clang-tidy;
- ARMv7 and ARM64 Android native builds;
- split/universal debug APK packaging;
- benchmark smoke.

The first Phase 6 implementation slice should be engine-owned object transform state + validation + undo/redo + project persistence tests. Renderer/gizmo interaction should build on that state only after the portable contract is green.
