# Vortex3dGm Donor Audit

The previous `Arctic403/Vortex3dGm` repository is a donor/reference implementation. It is not the architectural base of this repository.

## Keep / port the ideas

The existing C++ core already demonstrates several decisions that should survive into Vortex3D Native:

- persistent logical IDs for vertices, edges, faces, and loops/corners,
- edge radial connectivity,
- object-to-mesh data-block separation,
- linked mesh sharing and Make Unique behavior,
- source vs evaluated geometry distinction,
- mesh/object revision tracking,
- mesh validation,
- edit-session concepts,
- native C++ smoke testing,
- primitive generation,
- production GLB round-trip/validation lessons,
- diagnostic/validator mindset.

## Refactor rather than copy blindly

### Current monolithic `Core`

The donor core combines document storage, mesh operations, object operations, edit-session state, serialization-to-JSON helpers, and public API surface in one class.

Native Vortex should split this into focused modules such as:

```text
Document
DocumentRegistry / DataBlocks
MeshKernel
AttributeSet
CommandSystem
TransactionManager
UndoStack
Evaluator
Serialization
ImportExport
```

### Naked integer IDs

Upgrade to strongly typed 64-bit handles. Avoid APIs that can accidentally pass a face ID where a mesh ID is expected.

### CSV/string bridge APIs

The donor uses CSV/string boundaries because it had to cross WASM/TypeScript interfaces. Native internal APIs should use spans, vectors, structured views, and typed values. Text/JSON belongs at debugging or external boundaries.

### Hard-coded mesh attributes

The donor has dedicated `uv0`, `uv1`, normals, seam/sharp/crease fields. Preserve the semantics but move toward generic typed attribute domains.

### Mixed authored and compatibility buffers

The donor keeps source buffers, topology, and evaluated buffers together. Native Vortex should make the ownership/lifetime boundaries explicit:

- `EditableMesh`
- `EvaluatedMesh`
- renderer-owned GPU cache

## Replace

Do not port these as foundation dependencies:

- Three.js renderer ownership,
- DOM-bound UI assumptions,
- WebView shell assumptions,
- OPFS/IndexedDB/localStorage project storage,
- Emscripten/Embind as the primary engine boundary,
- browser touch/pointer event implementation,
- web deployment infrastructure.

Some may remain useful in the old web frontend or future optional browser bindings.

## Migration method

For each donor subsystem:

1. Write down its observable behavior.
2. Add/port tests that define that behavior.
3. Implement the native module cleanly against the new architecture.
4. Compare behavior/results with donor fixtures where useful.
5. Only then consider the donor subsystem retired.

This prevents both extremes: throwing away good work and dragging old architecture into the new engine.
