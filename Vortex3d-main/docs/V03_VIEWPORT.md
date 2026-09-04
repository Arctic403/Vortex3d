# Vortex3D v0.3 Native Viewport

## Goal

v0.3 starts the Vortex-owned renderer/viewport without changing ownership of authored data. The first milestone is intentionally small: prove a real Android `SurfaceView` can be driven by a custom Vulkan backend on both ARMv7 and ARM64, survive surface recreation, and present frames continuously.

## Architecture boundary

```text
Document / EditableMesh authored truth
        |
        v
MeshEvaluator
        |
        v
RenderExtractor
        |
        v
future VortexViewportScene
        |
        v
Vortex Android Vulkan backend
        |
        v
Android SurfaceView
```

The portable engine still contains no Android or Vulkan dependency. Vulkan objects, Android surfaces, swapchains, queues, synchronization primitives, and lifecycle handling live under the Android renderer boundary.

## Bootstrap implemented in this patch

- Kotlin `SurfaceView` host with a tiny diagnostic overlay and UI-thread JNI lifecycle serialization.
- JNI renderer lifetime handle rather than a process-global singleton.
- Android `ANativeWindow` acquisition/release with explicit ownership.
- Vulkan instance and Android surface creation.
- physical-device selection with swapchain-extension and graphics/present queue checks.
- logical device and queue creation with separate graphics/present-family support.
- FIFO swapchain creation with capability/format/extent negotiation.
- color-only render pass, image views, framebuffers, command buffers, semaphores, and one in-flight fence.
- lifecycle-gated `Choreographer` frame loop that clears/presents on display vsync and stops before surface teardown.
- swapchain recreation on resize/out-of-date/suboptimal results.
- surface detach/recreate handling without placing Vulkan state in `Document`.
- runtime diagnostics exposing GPU name, Vulkan API version, surface extent, and 32/64-bit process width.

No external rendering engine or editor framework is introduced by this bootstrap.

## Deliberately not implemented yet

This patch does not claim to be the finished renderer. The next increments are:

1. renderer-owned depth buffer,
2. immutable `ViewportMesh` GPU upload,
3. camera matrices and touch orbit/pan/zoom,
4. one Vortex cube rendered from `Document -> MeshEvaluator -> RenderExtractor`,
5. multi-object transforms and revision-keyed GPU resource caching,
6. integer object-ID picking,
7. grid/selection/overlay paths,
8. transform gizmos,
9. material/PBR pipeline.

## ARMv7 rule

The Vulkan bootstrap must continue to build in the existing `armeabi-v7a` 32-bit CI job. Renderer allocations, frame resources, and future staging/cache budgets must be explicit and bounded. No pointer-sized value becomes persistent identity.
