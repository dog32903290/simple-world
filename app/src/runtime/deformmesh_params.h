// Host<->shader params for TiXL mesh/modify/mesh-Deform.hlsl (owner op: DeformMesh) — mathv
// transpiler-batch kernel INVENTORY (2026-07-10, MATH_VERIFY_WORKFLOW.md §10 wave-3). NOT wired to
// node_registry / t3_import — a verified-kernel-in-stock entry; connecting it to a stage is a
// separate future lane. Owner: external/tixl/Operators/Lib/mesh/modify/DeformMesh.t3.
//
// Mirrors external/tixl .../Assets/shaders/3d/mesh/mesh-Deform.hlsl's cbuffer Params (:5-20) FIELD
// FOR FIELD, INCLUDING the explicit padding fields spirv-cross emitted for HLSL's cbuffer packing
// rules (a vec3/vec4 must start on a 16-byte boundary; 7 leading scalars (28B) get one padding float
// to reach the 32B boundary before Pivot; each subsequent packed_float3 gets its own trailing padding
// float to refill its 16B slot before the next field) — UNLIKE the storage-buffer packed_float3 bug
// (§10.5⑥⑦), spirv-cross's CONSTANT BUFFER layout handling is correct here (verified by reading the
// raw transpile output — Pivot/TwistPivot both got packed_float3 + an explicit padding float, not the
// "only the last field" bug), so this struct is transcribed verbatim, no fix needed. `Count` is
// APPENDED at the end (host-ABI addition, §10.2③/§10.5①, replaces SourceVerts.GetDimensions) — safe
// because nothing follows it in the original 13-field cbuffer, so no earlier offset is disturbed.
//
// TaperAxis/TwistAxis are float-typed enum-like selectors (`switch(int(TaperAxis))` / `switch(int(
// TwistAxis))` in the HLSL) with cases {0,1,2}. TaperAxis's `default:` case is WELL-DEFINED (falls
// through to the pre-taper value, HLSL:105-106 has no `default:` label but falls out of the switch
// with `tapered` still holding its pre-switch value — spirv-cross correctly modeled this as
// `_805 = _461` i.e. identity passthrough). TwistAxis's `default:` is GENUINELY UNDEFINED in the HLSL
// (the switch has cases 0/1/2 only, `twisted` is read after the switch with NO assignment on any
// untaken path -- an uninitialized-variable read, HLSL spec UB) — spirv-cross's SPIR-V lowering
// resolved this deterministically to a ZERO-filled float3 (`constant float3 _808 = {}`), which is ONE
// valid resolution of the UB but not a claim about real HLSL/DX11 runtime behavior on other backends
// (§9 "HLSL 本體歧義（UB/死碼/DX intrinsic）" trap). This mathv case PINS the transpiler's own resolution
// (TwistAxis outside {0,1,2} -> post-twist position is exactly TwistPivot, since zero-vector + TwistPivot
// = TwistPivot) as the AMBIGUITY-PINNED expected behavior for THIS transpiled kernel — not a production
// semantics claim; see selftests_mathv_deformmesh.cpp's dedicated "twistAxisOutOfRangeUB" tooth.
//
// UNREACHABLE-FROM-AUTHORED-CONTENT (S-review batch-3, closing the loop so this doesn't get re-dug):
// the out-of-{0,1,2} case this pin covers can NEVER occur from a real authored .t3 — TwistAxis is
// declared `InputSlot<int>` with `MappedType = typeof(SetAxis)` (DeformMesh.cs:47-48), where SetAxis
// is the 3-value enum `{X, Y, Z}` (DeformMesh.cs:57-62, ordinals 0/1/2), AND the .t3ui inspector pins
// `Min: 0, Max: 2, ClampMin: true, ClampMax: true` on that same slot (DeformMesh.t3ui:91-94) — a
// combo-box UI with clamped bounds that make 3/4/-1/etc unreachable through the editor. So the pin
// above is exercised ONLY by this file's synthetic mathv fuzz (which drives TwistAxis outside the
// authored-legal range on purpose, to nail the transpiler's own UB resolution), never by production
// content. KEEP the pin as-is (S decision: maintain, not remove) — it is the correct behavior for the
// unreachable case IF the kernel is ever driven off-spec, and removing it would just make that path
// silently undefined again.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
  #define DM_PACKED3 packed_float3
  #define DM_FLOAT2  float2
#else
  #include <cstddef>
  #include <cstdint>
  struct dm_packed3 { float x, y, z; };
  struct dm_float2  { float x, y; };
  #define DM_PACKED3 dm_packed3
  #define DM_FLOAT2  dm_float2
#endif

struct DeformMeshParams {
  float UseVertexSelection;  // @0
  float Spherize;            // @4
  float Radius;              // @8
  float TaperAmount;         // @12
  float TwistAmount;         // @16
  float TaperAxis;           // @20  (int(TaperAxis): 0/1/2, default=identity passthrough)
  float TwistAxis;           // @24  (int(TwistAxis): 0/1/2, default=AMBIGUITY-PINNED, see header)
  float _padding;            // @28
  DM_PACKED3 Pivot;          // @32  (12 bytes)
  float _padding1;           // @44
  DM_PACKED3 TwistPivot;     // @48  (12 bytes)
  float _padding2;           // @60
  DM_FLOAT2 Taper2;          // @64  (8 bytes)
#ifdef __METAL_VERSION__
  uint Count;                // @72  host-ABI (replaces SourceVerts.GetDimensions, §10.5①)
#else
  uint32_t Count;            // @72
#endif
};

// Binding numbers read off the ACTUAL glslang+spirv-cross raw output (§10.5③).
enum DeformMeshBinding {
  DEFORMMESH_Params      = 0,  // constant DeformMeshParams& (b0, extended with host-ABI Count)
  DEFORMMESH_SourceVerts = 1,  // const device SwVertex*     (t0)
  DEFORMMESH_ResultVerts = 2,  // device SwVertex*           (u0)
};

#ifndef __METAL_VERSION__
static_assert(sizeof(DeformMeshParams) == 76, "DeformMeshParams must be 76 bytes (72 original + 4 Count)");
static_assert(offsetof(DeformMeshParams, Pivot) == 32, "Pivot must start at cbuffer's 32B boundary");
static_assert(offsetof(DeformMeshParams, TwistPivot) == 48, "TwistPivot must start at cbuffer's 48B boundary");
static_assert(offsetof(DeformMeshParams, Taper2) == 64, "Taper2 must start at cbuffer's 64B boundary");
static_assert(offsetof(DeformMeshParams, Count) == 72, "Count is the host-ABI tail field");
#endif
