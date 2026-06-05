# Native Resource Lifetime Policy Proof

NativeResourceLifetimePolicyProof answers:

Can the live runtime reason about transient aliasing, persistent feedback
resources, release fences, barriers, and leak reports beyond simple
allocate/reuse bookkeeping?

Acceptance line:

```text
RenderGraph lifetimes -> allocator policy -> alias plan -> release fences -> leak report
```

## Required Claims

- aliasPlannerRan: true
- releaseFencesTracked: true
- leakReportClean: true
- realGpuHeapAllocator: false

## Policy Boundary

- Transient resources may alias when their lifetimes do not overlap and their
  descriptor family matches.
- Persistent feedback history must not alias transient render targets.
- Rebinding an aliased heap slot emits an alias barrier.
- Writes read by later passes emit hazard barriers.
- Every live resource needs a release fence or it is reported as a leak.

## Boundaries

- This is not a real GPU heap allocator.
- This is not Metal heap residency management.
- This is not a replacement for backend-specific hazard tracking.
