// Host<->shader params for TiXL mesh/fx/mesh-UVs.hlsl (owner op: UVsViewer) — mathv transpiler-batch
// kernel INVENTORY (2026-07-10, MATH_VERIFY_WORKFLOW.md §10 wave-3). NOT wired to node_registry /
// t3_import — a verified-kernel-in-stock entry; connecting it to a stage is a separate future lane.
//
// Mirrors external/tixl .../Assets/shaders/3d/mesh/fx/mesh-UVs.hlsl. Owner: external/tixl/Operators/
// Lib/mesh/_/UVsViewer.t3 (a debug-visualization op: blends a mesh's vertex Position toward its UV
// unwrap, so TexCoord layout can be inspected in 3D — not production-critical, but a real .t3 op with
// a real kernel; mathv doesn't discriminate by production priority).
//
// ★NAMED FORK, FAITHFULLY PRESERVED (NOT a bug we fix — see MATH_VERIFY_WORKFLOW.md §10.3, transpiler
// inventory transcribes the RAW HLSL including its own bugs): the HLSL guard is
//   `if (i.x > resultCount) return;`                                          (.hlsl:24-25)
// — STRICTLY GREATER, not `>=`. This is an off-by-one in TiXL's own source: when ResultVertices'
// element count is NOT a multiple of the dispatch threadgroup size (64), the FIRST "padding" thread
// (index == resultCount) is NOT excluded by the guard and proceeds to read VerticesA[resultCount] and
// write ResultVertices[resultCount] — one element PAST the nominal count. When the count IS a multiple
// of 64, no padding thread exists at index==count (max launched index is count-1), so the bug is
// INERT for that case. This TU exploits both shapes deliberately (§10.3's "sample the diverging
// middle" discipline extended to a boundary): scenarios with resultCount%64==0 assert the one-past
// slot stays UNTOUCHED (pre-seeded sentinel survives); scenarios with resultCount%64!=0 assert the
// one-past slot IS processed and matches the ref run on resultCount+1 elements. Both host-side buffers
// are therefore always allocated with resultCount+1 elements of headroom so this off-by-one can never
// corrupt memory during the fuzz run itself.
//
// `countA`/`countB` (HLSL:22, VerticesA.GetDimensions) are DEAD in the original HLSL — declared,
// countA assigned but never read (only resultCount gates the loop), countB never even assigned. The
// raw glslang+spirv-cross output confirms this: only ONE spvBufferSizeConstants slot is referenced
// (ResultVertices' size), the VerticesA.GetDimensions call was dead-code-eliminated. `Count` below is
// the host-ABI replacement for that single live GetDimensions call (§10.2③/§10.5①) — it is
// ResultVertices' element count (== VerticesA's element count in any real caller; the kernel itself
// never enforces or reads VerticesA's own size).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct UvsViewerParams {
#ifdef __METAL_VERSION__
  uint Count;         // host-ABI: ResultVertices element count (replaces GetDimensions, §10.5①)
  float BlendFactor;  // HLSL cbuffer field
  float SwitchUV;     // HLSL cbuffer field
#else
  uint32_t Count;
  float BlendFactor;
  float SwitchUV;
#endif
};

// Binding numbers read off the ACTUAL glslang+spirv-cross raw output (§10.5③ — NOT declaration
// order: HLSL declares Params(b0) first, then VerticesA(t0), then ResultVertices(u0), but the
// compacted [[buffer(N)]] numbering came out ResultVertices=0/VerticesA=1/Params=2).
enum UvsViewerBinding {
  UVSVIEWER_ResultVertices = 0,  // device SwVertex*       (u0)
  UVSVIEWER_VerticesA      = 1,  // const device SwVertex* (t0)
  UVSVIEWER_Params         = 2,  // constant UvsViewerParams& (host ABI, not raw HLSL b0 layout)
};

#ifndef __METAL_VERSION__
static_assert(sizeof(UvsViewerParams) == 12, "UvsViewerParams must be 12 bytes (1 uint + 2 float)");
#endif
