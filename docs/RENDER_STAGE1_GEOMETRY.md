# Render Stage 1: Geometry Foundation

This branch ports the first Vulkan geometry slice from the Vortex3dGm Android renderer into the clean Vortex3D Android viewport without importing the old editor or web runtime.

Goals:
- preserve current SurfaceView/JNI/swapchain lifecycle authority
- add depth buffering
- add GPU vertex/index buffers
- add a minimal graphics pipeline
- draw one indexed test mesh
- keep ARMv7 and ARM64 compatibility

Non-goals for this stage:
- textures
- PBR materials
- overlays/grid/gizmos
- editor UI
- scene synchronization
