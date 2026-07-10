// Host<->shader params for TiXL mesh/modify/mesh-TransformUVs.hlsl (owner op: TransformMeshUVs) —
// mathv transpiler-batch kernel INVENTORY (2026-07-10, MATH_VERIFY_WORKFLOW.md §10 wave-3). NOT wired
// to node_registry / t3_import — a verified-kernel-in-stock entry; connecting it to a stage is a
// separate future lane. Owner: external/tixl/Operators/Lib/mesh/modify/TransformMeshUVs.t3.
//
// ★MATRIX CONVENTION (the R workflow's #1 documented risk, MATH_VERIFY_WORKFLOW.md §5 "TransformFromClipSpace
// 教訓" + §10.5's new standalone-stride-test methodology note): unlike every other transpiler-batch op so
// far, this kernel's raw spirv-cross output uses MSL's native `float4x4 TransformMatrix` type + the `v *
// M` operator (`(float4(pos,1) * TransformMatrix).xyz`) — NOT a hand-rolled flat-float-array multiply
// like the PROVEN production kernel computeshaderstage_transformmesh.metal (which reads m[0..15] and
// computes `M·v` explicitly, m[r*4+3] = translation, "mem layout in hlsl constant buffer is row based").
//
// This op is NOT wired to production (no real TiXL C# host caller to match byte-for-byte) — so THIS
// header defines its own self-consistent host-ABI upload convention, derived algebraically (then
// EMPIRICALLY VERIFIED by the fuzz TU's dedicated matrixConvention tooth, per §10.5's new methodology
// note) to match the SAME logical convention as the proven production kernel, so `TransformMatrix` here
// means the same thing it does everywhere else in this codebase:
//   - Host writes 16 floats `m[0..15]` in ROW-MAJOR flat order: m[r*4+c] = matrix row r, column c.
//     Translation lives in column 3: m[3]/m[7]/m[11] (rows 0/1/2's 4th entry).
//   - Point transform (w=1): out[r] = sum_{c=0..3} m[r*4+c] * v[c], v=(x,y,z,1) — i.e. `M·v` (matrix
//     times COLUMN vector), matching computeshaderstage_transformmesh.metal's mulXformPoint exactly.
//   - ALGEBRA (why writing m[] row-major into an MSL `float4x4` field, then letting the transpiled
//     kernel's `v * M_metal` run, reproduces `M·v` above): MSL's `float4x4` reads its 64-byte backing
//     store as 4 SEQUENTIAL COLUMNS (column-major native layout). Writing my row-major m[0..15] into
//     that memory means Metal's column c == my row c, i.e. M_metal = transpose(my_M). MSL's `v * M`
//     operator computes the row-vector convention (out[j] = sum_i v[i]*M[i][j]); substituting
//     M=M_metal=my_M^T: out[j] = sum_i v[i]*my_M[j][i] = (my_M · v)[j]. So `v * M_metal` (spirv-cross's
//     emitted form) == `my_M · v` (this header's convention) for the SAME 16 floats — no numerical
//     transpose is needed anywhere in the adapter code, ONLY a row-major memcpy of m[0..15] into the
//     float4x4 field's backing bytes.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct TransformMeshUvsParams {
#ifdef __METAL_VERSION__
  float4x4 TransformMatrix;  // row-major m[0..15] memcpy'd verbatim -- see header ALGEBRA note
#else
  float m[16];
#endif
  float UseVertexSelection;
  float ToTexCoord2;
#ifdef __METAL_VERSION__
  uint Count;   // host-ABI (replaces SourceVerts.GetDimensions, §10.5①)
#else
  uint32_t Count;
#endif
};

// Binding numbers read off the ACTUAL glslang+spirv-cross raw output (§10.5③).
enum TransformMeshUvsBinding {
  TMUV_SourceVerts = 0,  // const device SwVertex*        (t0)
  TMUV_Params      = 1,  // constant TransformMeshUvsParams& (b0, extended with host-ABI Count)
  TMUV_ResultVerts = 2,  // device SwVertex*               (u0)
};

#ifndef __METAL_VERSION__
static_assert(sizeof(TransformMeshUvsParams) == 76,
              "TransformMeshUvsParams must be 76 bytes (64 matrix + 4 + 4 + 4)");
#endif
