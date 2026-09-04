# Architecture Research Sources

These are the primary references used to shape Vortex3D Native. They are references for architecture and behavior, not code-copy sources.

## Blender

- BMesh design: https://developer.blender.org/docs/features/objects/mesh/bmesh/
- Dependency graph: https://developer.blender.org/docs/features/core/depsgraph/
- Operators: https://developer.blender.org/docs/features/interface/operators/
- Undo: https://developer.blender.org/docs/features/core/undo/
- License: https://www.blender.org/about/license/

Key lessons applied:

- editable topology needs dedicated topology structures rather than render triangles,
- authored and evaluated data should be separated,
- UI operators should delegate to reusable business logic,
- undo is infrastructure, not a late UI feature.

## Autodesk Maya

- DAG basics: https://help.autodesk.com/cloudhelp/2026/ENU/Maya-Basics/files/GUID-5029CF89-D420-4236-A7CF-884610828B70.htm
- Evaluation concepts: https://help.autodesk.com/cloudhelp/2027/ENU/Maya-Customizing/files/GUID-190D97E7-9AC0-4D67-8A07-1AF3A9DBAF15.htm

Key lessons applied:

- scene hierarchy and dependency/evaluation relationships solve different problems,
- evaluation scheduling should follow explicit dependencies.

## FreeCAD

- Core/App architecture: https://freecad.github.io/SourceDoc/d4/d68/group__CORE.html

Key lesson applied:

- application/domain logic should remain usable without the GUI layer.

## Open CASCADE Technology

- Documentation hub: https://dev.opencascade.org/doc/overview/html/

Key lessons applied:

- document transactions, undo/redo, topology identity, and robust geometric data management belong in the foundation.

## OpenUSD

- Documentation: https://openusd.org/release/index.html

Key lessons applied:

- durable scene description benefits from stable paths/identity, composition concepts, and separation between authored description and downstream consumers.

Vortex is not adopting USD as its internal editing model at this stage; it is an architectural reference and possible future interchange/integration target.

## glTF 2.0

- Specification: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html

Key lesson applied:

- glTF/GLB is an excellent runtime/interchange representation, but it does not replace a full authoring-project format with editable topology, command history, modifier state, and Vortex-specific metadata.

## MaterialX

- Project/specification: https://materialx.org/

Key lesson applied:

- a future material/node system should prefer typed, portable graph concepts rather than renderer-specific material state.

## Android / NDK / Vulkan

- Android NDK: https://developer.android.com/ndk
- Vulkan native engine support: https://developer.android.com/games/develop/vulkan/native-engine-support
- Storage Access Framework: https://developer.android.com/guide/topics/providers/document-provider

Key lessons applied:

- portable C/C++ belongs behind a narrow Android host boundary,
- Vulkan capabilities must be queried rather than assumed,
- Android already provides a proper user-document/file-provider model.

## Licensing rule

Vortex3D Native is currently MIT licensed. Reference projects with incompatible/copyleft licensing are used for learning and architectural study only. Do not paste or mechanically translate GPL Blender implementation code into this repository unless the project's licensing strategy is explicitly changed first.
