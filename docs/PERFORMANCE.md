# Vortex3D Performance Measurement Policy

## Rule zero

Correctness comes before performance, and measured performance comes before storage redesign.

The current mesh kernel deliberately favors clear invariants and stable IDs over cache-optimal storage. No container replacement is justified solely by intuition.

## Benchmark executable

Enable with:

```text
-DVORTEX_BUILD_BENCHMARKS=ON
```

The `vortex_mesh_bench` executable measures:

- vertex creation,
- edge creation,
- face creation,
- shared-edge quad-grid face creation,
- topology traversal,
- edge split,
- face extrusion,
- attribute lookup,
- attribute mutation,
- vertex movement,
- undo,
- redo,
- full validation.

It emits JSON with operation name, actual element count, elapsed milliseconds, a checksum, and whether the requested scale had to be capped.

## Scale profiles

Manual benchmark workflow profiles are available at approximately:

- 10,000 requested elements,
- 100,000 requested elements,
- 1,000,000 requested elements.

Edge creation now runs at the full requested scale through the maintained undirected-edge acceleration index, and the mesh benchmark includes a shared-edge quad grid so normal modeling topology exercises edge reuse directly. Some unrelated history/topology fixtures may still report `capped: true` when their own benchmark cost is intentionally bounded; each JSON result records that explicitly.

A lightweight smoke profile runs on normal CI and is stored as a GitHub Actions artifact for each commit. Full-scale profiles are explicit/manual to avoid turning normal correctness CI into an uncontrolled performance bill.

## Regression tracking

Benchmark JSON artifacts are retained per workflow run. Compare like-for-like compiler/runner/profile results before treating a change as a regression.

Do not use tiny timing differences on shared CI runners as hard pass/fail thresholds. A performance change becomes actionable when it is repeatable locally/device-side or large enough to remain visible across multiple comparable CI runs.

When Android host/device automation exists, representative ARMv7 and ARM64 device measurements should become the authoritative mobile baselines.

## Memory/layout reporting

The benchmark reports compiler-specific `sizeof` values for:

- Vertex,
- Edge,
- Face,
- Corner,
- AttributeLayer,
- `unordered_map` value payloads,
- pointer/bucket reference size.

These are not total allocator costs. Standard-library hash nodes add implementation-specific metadata, allocation rounding, and bucket storage. They are useful input to the storage audit, not fake precision.

## Optimization policy

Before changing the kernel storage model:

1. capture a benchmark JSON at relevant scales,
2. identify which operation and data structure dominates,
3. make one focused change,
4. rerun correctness + sanitizer + Android gates,
5. rerun comparable benchmarks,
6. keep the change only if the measured tradeoff is worthwhile.
