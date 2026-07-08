// selftests_retire_kernelprobe — R4 (compute-stage kernel PORTED) probe helper for the retirement sweep.
//
// R1/R2/R3 verify a compound imports as a non-atomic蔥 graph with every child MAPPED — structurally
// READY. But structural READY ≠ COOKABLE: a compound can route a ComputeShaderStage through a kernel
// that was never ported to MSL. At cook, buffer_ops_computeshaderstage.cpp's kernelNameFor() returns
// the raw HLSL path (no mapping), cachedComputePSO misses, and the dispatch is skipped → the UAV is
// never written → wrong output (退場後 ②parity RED). R4 catches that "kernel not ported at all" gap.
//
// Split out of selftests_retire.cpp (rule-4 line ratchet) as PURE READ-ONLY judgment over the imported
// SymbolLibrary + the on-disk shader set. Never mutates the lib / registry / kernelNameFor — only reads.
#pragma once

#include <string>

namespace sw {

struct SymbolLibrary;

// Resolve a ComputeShaderStage KernelName (a folded ComputeShader.Source HLSL path, or an already-MSL
// kernel name) to its MSL kernel name — a READ-ONLY MIRROR of buffer_ops_computeshaderstage.cpp's
// kernelNameFor(). KEEP IN SYNC: a mapped path returns the ported kernel; an UNMAPPED path returns the
// path unchanged (still carrying '/' or ".hlsl"), which is how the caller tells ported from unported.
std::string probeResolveComputeKernel(const std::string& source);

// True iff `kernelName` (an already-resolved MSL kernel name) has a committed app/shaders/<name>.metal
// source (the file the CMake glob compiles into shaders.metallib). Necessary condition for a real PSO.
bool probeComputeKernelMetalExists(const std::string& kernelName);

// R4 core: walk `lib` from root `symId` (recursing nested compound children) and return the resolved
// SOURCE of the FIRST ComputeShaderStage child whose kernel is NOT ported — i.e. probeResolveComputeKernel
// left it an unmapped path, OR the mapped kernel has no committed .metal. Returns "" when every compute
// child is ported (or the compound has none). NECESSARY, NOT SUFFICIENT: a ported kernel can still cook
// wrong (deeper forks, e.g. mesh stride 64→80) — that residue stays the retire §5 ②parity gate's job.
std::string probeFirstUnportedComputeKernel(const SymbolLibrary& lib, const std::string& symId);

// Registered selftest (--selftest-probe-r4): welds the R4 contract (structural-READY ≠ cookable) end to
// end. injectBug points the negative leg at an all-ported compound so the RED path is a real --bite tooth.
int runProbeR4Golden(bool injectBug);

}  // namespace sw
