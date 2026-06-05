# Native Metal Heap Residency Proof

NativeMetalHeapResidencyProof answers:

Can the allocator policy cross from JSON lifetime planning into real Metal heap
residency without replacing the policy ledger?

Acceptance line:

```text
allocator policy -> MTLHeap descriptor -> heap-backed textures -> residency ledger
```

## Required Claims

- realMetalHeapAllocator: true
- heapBackedTextures: true
- residencyLedgerClean: true
- policyLedgerStillSeparate: true

## Boundary

The existing resource lifetime policy still owns alias decisions, release
fences, and leak checks. This proof adds real Metal heap-backed texture allocation
for the admitted resource descriptors and records residency evidence.

This is not a complete heap allocator, cross-device residency manager, eviction
system, or backend-specific hazard tracker. It is the first product runtime line
where texture resources are actually allocated from an `MTLHeap`.
