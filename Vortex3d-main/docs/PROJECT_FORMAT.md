# Native Project Format Contract

> **Implemented preview:** schema v1 is now live through `ProjectCodec`. It is a deterministic binary authored-state container with exact stable IDs, generic attributes, hierarchy/shared-mesh references, bounded decoding, payload length, and integrity checksum. Runtime identity, undo history, evaluated data, and caches remain non-persistent. The schema is still pre-1.0 and may migrate.

Schema v1 now writes the standard FNV-1a 64-bit checksum while accepting the earlier v0.2 preview checksum variant for backward compatibility. Encoding uses a single output buffer and const topology/attribute views to avoid avoidable whole-project temporary copies on memory-constrained 32-bit hosts.

## Purpose

A Vortex project file preserves editable authoring state. It is not merely an exported runtime mesh container.

The format must be versioned before the first user project is considered stable.

## Required top-level metadata

Every project contains at minimum:

- file magic / format identifier,
- format version,
- minimum compatible reader version when needed,
- application version that wrote the file,
- project/document ID,
- endianness/encoding rules if binary,
- integrity metadata/checksums as the container design matures.

## Persistent content

The native project format is expected to preserve:

- scenes,
- objects and hierarchy,
- collections,
- shared mesh data-blocks,
- stable topology IDs,
- editable polygon/n-gon topology,
- generic attributes,
- materials,
- image/asset references or embedded assets,
- modifier stacks,
- future procedural graphs,
- Vortex game-asset metadata,
- units/project settings that affect authoring meaning.

Editor-only ephemeral state should be stored separately or in an explicitly optional workspace/session section.

## IDs and references

Persisted IDs use fixed-width integer encoding. Never serialize raw pointers, `size_t`, object addresses, or container indices as durable references.

References must be validated on load. Invalid or cyclic references should produce controlled diagnostics rather than undefined behavior.

## Container strategy

Schema v1 currently uses a compact custom binary container. Future container changes remain possible through schema migration and must continue to be evaluated against:

- fast incremental/atomic save behavior,
- schema migration,
- corruption isolation,
- partial/lazy loading potential,
- 32-bit memory constraints,
- Android implementation simplicity,
- desktop portability,
- inspectability/debug tooling,
- long-term dependency/licensing risk.

The logical schema contract matters before the physical encoding.

## Migration

Every persisted schema has an explicit integer version.

Loading follows:

```text
Read header
-> validate format
-> decode schema N
-> migrate N -> N+1 -> ... -> current
-> validate document invariants
-> expose document to editor
```

Never mutate the user's original file merely because it was opened. A migrated document is written when the user saves, preferably through a new atomic replacement.

## Save safety

Normal save should aim for:

1. serialize to a temporary target,
2. flush/close successfully,
3. validate basic container integrity,
4. atomically replace the previous project where the platform allows it.

Autosave/recovery data must not silently overwrite the user's last explicit save.

## Recovery

Recovery should eventually track:

- last explicit save identity/revision,
- autosave timestamp,
- document schema/app version,
- enough integrity information to reject truncated recovery data,
- optional operation journal/deltas if that becomes preferable to full autosaves.

## Import/export distinction

- `.vortex` (working name) = native editable authoring project.
- `.glb` / `.gltf` = interchange/runtime delivery.
- Additional import/export formats are adapters.

An import adapter creates/updates Vortex authoring structures. An export adapter consumes document/evaluated structures. Neither external format defines the internal architecture.

## Test fixtures

Before declaring the project format stable, maintain fixture files for:

- empty project,
- shared mesh data,
- hierarchy,
- n-gon mesh + attributes,
- modifier stack,
- missing external asset,
- deliberately corrupted reference,
- each previous schema version,
- large project under 32-bit memory testing.
