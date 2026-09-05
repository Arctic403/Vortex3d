# Mesh Storage Philosophy and Migration Path

## Current representation

`EditableMesh` currently keeps four core kinds of information per topology domain:

1. stable 64-bit public IDs,
2. ordered ID vectors,
3. ID -> packed-index lookup maps,
4. ID -> topology-record maps.

Edges additionally maintain a **derived undirected endpoint-pair -> `EdgeId` acceleration index**. This preserves the stable-ID/order/registry architecture while making `edgeBetween()` and duplicate-edge detection average O(1) instead of scanning the full edge order. The index is rebuilt from authored edge records after deserialization and `validateStrict()` checks it for missing, stale, or mismatched entries. It is never serialized as authored state.

This representation is intentionally redundant. It makes the mutation kernel easy to validate and gives stable identities independent from mutable packed order. The cost is extra hash nodes, buckets, indirection, allocations, and duplicate lookup state. That matters especially on 32-bit Android, so future storage replacement remains measurement-driven rather than assumed.

## What is measured now

`vortex_mesh_bench` reports actual compiler layout sizes for topology records and hash-map payloads. It also measures operation scaling at requested 10k/100k/1M profiles where practical.

Approximate hash storage must account for more than `sizeof(value_type)`:

```text
entry ~= value payload
       + hash/node links and metadata
       + allocator rounding
       + bucket-array share
```

The exact overhead is standard-library and architecture dependent, so a single hard-coded bytes-per-entry number would be misleading. ARMv7 and ARM64 should eventually have device-specific memory baselines.

## Stable-ID requirement

Any future storage change preserves this conceptual boundary:

```text
Stable public ID/handle
        |
        v
validated internal lookup
        |
        v
packed/cache-friendly topology storage
```

Packed indices are implementation details and may change during compaction. They never become serialized identity or durable editor references.

## Candidate directions to investigate

Only after benchmark evidence:

- packed structure-of-arrays or array-of-structures for hot topology,
- sparse-set-like ID -> slot lookup,
- generational internal slots while preserving separate stable public IDs,
- denser flat/open-addressed lookup tables if a suitable dependency policy is chosen,
- fewer duplicated order/index structures,
- reusable operation-local scratch buffers,
- `std::pmr` for temporary mutation workloads.

None of these is preselected. A generational slot is not automatically better than a hash lookup, and an ECS is not a requirement.

## Allocation plan

No custom allocator is introduced at this stage.

Near-term preparation:

- keep persistent identity independent from allocator address,
- keep operation temporaries local,
- prefer APIs that can later accept scratch/reusable buffers without changing semantics,
- benchmark allocation-heavy operations,
- evaluate `std::pmr::monotonic_buffer_resource` only for clearly temporary operation data,
- never let an arena own persistent topology accidentally.

## Decision gate for storage replacement

A storage redesign needs all of:

1. a measured memory or performance bottleneck,
2. a prototype with comparable operation benchmarks,
3. unchanged stable-ID behavior,
4. invariant/undo tests passing,
5. GCC/Clang + sanitizer gates passing,
6. ARMv7 + ARM64 NDK compilation passing,
7. documented migration/serialization impact.
