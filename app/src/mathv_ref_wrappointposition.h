#pragma once
// mathv_ref_wrappointposition — CPU scalar oracle for the WrapPointPosition kernel.
//
// TRANSCRIBED from external/tixl (SHA 395c4c55)
// Operators/Lib/Assets/shaders/points/modify/WrapPointPosition.hlsl — NOT derived from sw shaders.
//   cbuffer Params    :6-12   (Center, UseCamera, Size, WriteLineBreaks)
//   cbuffer Transforms:14-26  (only CameraToWorld's translation row is read, :45)
//   main() body       :38-87  -> mathvRefWrapPointPosition() / wrapPointPositionOne() below,
//                                 every statement carries its own :NN back-reference.
// Cross-checked against the C# op (parameter names/order) at
// external/tixl/Operators/Lib/point/transform/WrapPointPosition.cs:10-23 (GPoints/Position/Size/
// UseCameraPosition/AddLineBreaks) and the shared struct layout at
// external/tixl/Operators/Lib/Assets/shaders/shared/point.hlsl:9-17 (LegacyPoint: Position/W/...).
//
// PROVENANCE discipline (GOLDEN_STANDARD.md "P5-safe oracle 判準" / MATH_VERIFY_WORKFLOW.md §5):
// this file is a P5-safe oracle, not a P5 self-consistent trap — it is transcribed straight from
// the TiXL HLSL text above, never from sw's own `app/shaders/wrappoints.metal` (that file is
// intentionally NOT opened by the R role writing this header; the whole point of mathv is to fuzz
// the CPU ref against the *independent* MSL port, so any accidental peek would self-certify a bug
// instead of catching it). Zero metal include, zero app/shaders/ reference, zero sw math helper.
//
// FLOAT (not double) on purpose (MATH_VERIFY_WORKFLOW.md §2.3 branchy-class rule): every branch in
// this kernel (abs(p.x) > padded.x, saturate(), the isnan guard) is a float comparison on the GPU;
// a double CPU ref could land on the opposite side of a branch boundary than the float GPU for the
// same nominal input, which would look like a kernel bug that is actually an oracle-precision bug.
//
// ZONE: shell-tier mathv support (pure math; no runtime/platform/Metal dependency). Only
// app/src/runtime/tixl_point.h is included, for the SwPoint host struct (data layout, not math).
#include "runtime/tixl_point.h"

#include <cmath>
#include <cstddef>
#include <limits>

namespace sw {
namespace mathv_ref {

// --- cbuffer field table -----------------------------------------------------------------------
// cbuffer Params : register(b0)  (:6-12, HLSL cbuffer packing: float3 rounds up to a 16B slot
//                                  unless a trailing scalar fits in the leftover 4B)
//   offset  0  float3 Center            (:8)  -- slot0 lo
//   offset 12  float  UseCamera         (:9)  -- slot0 hi (packs into Center's float4 slot)
//   offset 16  float3 Size              (:10) -- slot1 lo
//   offset 28  float  WriteLineBreaks   (:11) -- slot1 hi
// cbuffer Transforms : register(b1) (:14-26) -- only CameraToWorld is read, and only its
//   translation row (:45 `CameraToWorld._m30/_m31/_m32`); nothing else in Transforms is touched
//   by this kernel, so mathv does not need a matrix type — the adapter just hands over the 3
//   translation scalars directly.
struct WrapPointPositionParams {
  float centerX, centerY, centerZ;         // :8  Center
  float useCamera;                         // :9  UseCamera  (> 0.5 selects camera branch, :44)
  float sizeX, sizeY, sizeZ;               // :10 Size
  float writeLineBreaks;                   // :11 WriteLineBreaks (> 0.5 branch, :77)
  float camToWorldTx, camToWorldTy, camToWorldTz;  // :45 CameraToWorld._m30/_m31/_m32
                                                    // (translation row of a row-major, row-vector
                                                    // TiXL matrix; only read when useCamera > 0.5f)
};

namespace detail {
inline float hlslSaturate(float x) {  // HLSL saturate(): clamp to [0,1] (:83)
  return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}
inline float sqrtNegOne() {  // HLSL `sqrt(-1)` (:50, :78): IEEE sqrt of a negative -> NaN.
  return std::numeric_limits<float>::quiet_NaN();
}
}  // namespace detail

// wrapPointPositionOne — one thread of `main()` (:38-87), operating on a single point.
//
// `idx` / `numStructs` reproduce the dispatch's `i.x` / `GetDimensions(numStructs, stride)` (:41,
// :48) so the `idx >= numStructs` guard (:49-52) can be exercised verbatim if a caller ever wants
// to probe over-dispatch. mathvRefWrapPointPosition() below always calls this with
// `idx < numStructs == count`, since a host array is sized to exactly `count` points and has no
// concept of "extra" GPU threads from a numthreads(64,1,1) rounding — see AMBIGUITY note below.
inline void wrapPointPositionOne(const SwPoint& in, SwPoint& out, uint32_t idx, uint32_t numStructs,
                                  const WrapPointPositionParams& prm) {
  // :43-46 -- float3 center = Center; if (UseCamera > 0.5) center = CameraToWorld translation;
  float centerX = prm.centerX, centerY = prm.centerY, centerZ = prm.centerZ;
  if (prm.useCamera > 0.5f) {
    centerX = prm.camToWorldTx;
    centerY = prm.camToWorldTy;
    centerZ = prm.camToWorldTz;
  }

  // :48-52 -- GetDimensions guard. AMBIGUITY: this writes ResultPoints[i.x] for i.x >= numStructs,
  // i.e. an out-of-declared-range index; on the GPU this is only reachable because dispatch rounds
  // up to multiples of numthreads(64,1,1) and the backing buffer is presumably over-allocated to
  // absorb it. A host CPU ref operating on an exactly `count`-sized array has no such padding slot
  // to write into, so this branch is transcribed for fidelity/documentation but is unreachable
  // through mathvRefWrapPointPosition()'s loop (idx always < numStructs == count there). A GPU
  // fuzz driver that wants to probe this specific edge must dispatch >count threads against a
  // padded buffer itself; that is out of scope for this CPU ref.
  if (idx >= numStructs) {
    out = in;
    out.FX1 = detail::sqrtNegOne();  // LegacyPoint.W == SwPoint.FX1 (@12, see point.hlsl :12/:22)
    return;
  }

  // :54 -- float3 p = ResultPoints[i.x].Position - center;
  float px = in.Position.x - centerX;
  float py = in.Position.y - centerY;
  float pz = in.Position.z - centerZ;

  // :56-60 -- NOTED-QUIRK, transcribed verbatim (not "fixed"): the HLSL guard reads
  //   isnan(p.x + p.y + p.x)
  // i.e. it sums p.x TWICE and never looks at p.z. A NaN in p.z alone will NOT trip this guard on
  // the real TiXL kernel, and a matching sw port must reproduce that exact asymmetry for mathv
  // parity to mean anything (the whole point of transcription is to catch a divergent port, not
  // to pre-correct the thing being tested).
  if (std::isnan(px + py + px)) {
    out.Position.x = centerX - prm.sizeX * 0.2f;  // :58 center - Size * 0.2
    out.Position.y = centerY - prm.sizeY * 0.2f;
    out.Position.z = centerZ - prm.sizeZ * 0.2f;
    out.FX1 = 0.010f;  // :57
    // Only Position/W(FX1) are touched by this branch's return path (:59); every other point
    // field (Rotation/Color/Scale/FX2) passes through unchanged, matching the HLSL (which only
    // ever writes ResultPoints[i.x].Position and ResultPoints[i.x].W in this whole kernel).
    out.Rotation = in.Rotation;
    out.Color = in.Color;
    out.Scale = in.Scale;
    out.FX2 = in.FX2;
    return;
  }

  // :62 -- NOTED-QUIRK, transcribed verbatim: `float3 Padding = Size.x * 0.1;` broadcasts a
  // SCALAR (Size.x only) to all three axes of Padding -- Size.y and Size.z never contribute to
  // the padding band, even for the y/z wrap tests below. This is presumably an authoring
  // shorthand in the original op (assuming near-cubic Size), but it is what the kernel actually
  // does, so it is what this oracle does too.
  const float padding = prm.sizeX * 0.1f;

  // :64-65 -- float3 halfSize = Size/2; float3 padded = halfSize + Padding;
  const float halfX = prm.sizeX * 0.5f, halfY = prm.sizeY * 0.5f, halfZ = prm.sizeZ * 0.5f;
  const float paddedX = halfX + padding, paddedY = halfY + padding, paddedZ = halfZ + padding;

  // :67-71 -- float3 offsetFactor = 0; per-axis: if(abs(p.n) > padded.n) offsetFactor.n = p.n<0?1:-1;
  float offX = 0.0f, offY = 0.0f, offZ = 0.0f;
  if (std::fabs(px) > paddedX) offX = (px < 0.0f) ? 1.0f : -1.0f;
  if (std::fabs(py) > paddedY) offY = (py < 0.0f) ? 1.0f : -1.0f;
  if (std::fabs(pz) > paddedZ) offZ = (pz < 0.0f) ? 1.0f : -1.0f;

  // :73-74 -- float3 wrappedP = p + Size * offsetFactor; ResultPoints[i.x].Position = wrappedP + center;
  const float wrappedX = px + prm.sizeX * offX;
  const float wrappedY = py + prm.sizeY * offY;
  const float wrappedZ = pz + prm.sizeZ * offZ;
  out.Position.x = wrappedX + centerX;
  out.Position.y = wrappedY + centerY;
  out.Position.z = wrappedZ + centerZ;
  out.Rotation = in.Rotation;
  out.Color = in.Color;
  out.Scale = in.Scale;
  out.FX2 = in.FX2;

  // :77-86 -- if(WriteLineBreaks>0.5 && any-axis-wrapped) W = sqrt(-1); else W = product of
  // per-axis saturate((halfSize - abs(wrappedP)) * 10).
  const float wrapMagnitudeSum = std::fabs(offX) + std::fabs(offY) + std::fabs(offZ);
  if (prm.writeLineBreaks > 0.5f && wrapMagnitudeSum != 0.0f) {
    out.FX1 = detail::sqrtNegOne();  // :78
  } else {
    const float distX = halfX - std::fabs(wrappedX);  // :82
    const float distY = halfY - std::fabs(wrappedY);
    const float distZ = halfZ - std::fabs(wrappedZ);
    const float minDistX = detail::hlslSaturate(distX * 10.0f);  // :83
    const float minDistY = detail::hlslSaturate(distY * 10.0f);
    const float minDistZ = detail::hlslSaturate(distZ * 10.0f);
    out.FX1 = minDistX * minDistY * minDistZ;  // :84-85
  }
}

// mathvRefWrapPointPosition — full-buffer CPU oracle (:38-40 dispatch loop over `count` points).
// `in` and `out` may alias the same array (HLSL operates in-place on ResultPoints; the loop here
// reads `in[i]` before writing `out[i]`, which is safe even if `in == out`).
inline void mathvRefWrapPointPosition(const SwPoint* in, SwPoint* out, size_t count,
                                       const WrapPointPositionParams& prm) {
  for (size_t i = 0; i < count; ++i) {
    wrapPointPositionOne(in[i], out[i], static_cast<uint32_t>(i), static_cast<uint32_t>(count), prm);
  }
}

// --- self-check: hand-derived sanity values (independent of the code above; re-derive by hand
// from the HLSL text, not by reading this file's own logic back) ------------------------------
//
// All cases use Center=(0,0,0), Size=(10,10,10) unless noted, so halfSize=(5,5,5),
// Padding=Size.x*0.1=1, padded=(6,6,6).
//
// 1) Position=(2,2,2), WriteLineBreaks=0 (below padded=6 on every axis -> no wrap):
//    p=(2,2,2); offsetFactor=(0,0,0); wrappedP=(2,2,2); Position_out=(2,2,2) (unchanged).
//    distToEdge=(3,3,3); minDist=saturate(30,30,30)=(1,1,1); W_out = 1*1*1 = 1.
//
// 2) Position=(15,0,0), WriteLineBreaks=0 (x wraps clean onto the AABB edge):
//    p=(15,0,0); abs(15)>6 and 15 is not <0 -> offsetFactor.x=-1.
//    wrappedP=(15-10,0,0)=(5,0,0); Position_out=(5,0,0).
//    distToEdge=(5-5,5-0,5-0)=(0,5,5); minDist=saturate(0,50,50)=(0,1,1); W_out = 0.
//
// 3) Same as (2) but WriteLineBreaks=1 (>0.5): a wrap happened (offsetFactor sum != 0) ->
//    W_out = sqrt(-1) = NaN. Position_out is still (5,0,0) (Position math doesn't depend on
//    WriteLineBreaks, only the :77 branch on W does).
//
// 4) Position=(NaN,0,0), Center=(0,0,0), Size=(10,10,10):
//    p=(NaN,0,0); isnan(p.x+p.y+p.x)=isnan(NaN)=true ->
//    Position_out = center - Size*0.2 = (-2,-2,-2); W_out = 0.010.
//
// 5) UseCamera=1, camToWorldT=(3,4,5), Center=(1,1,1) (ignored because UseCamera>0.5),
//    Position=(3,4,5) (== camera position), WriteLineBreaks=0:
//    center=(3,4,5); p=Position-center=(0,0,0); offsetFactor=(0,0,0); Position_out=(3,4,5)
//    (unchanged); distToEdge=(5,5,5); minDist=(1,1,1); W_out = 1.
inline bool mathvRefWrapPointPositionSelfCheck() {
  auto approxEq = [](float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; };
  bool ok = true;

  {  // case 1
    SwPoint in{}, out{};
    in.Position = {2.0f, 2.0f, 2.0f};
    WrapPointPositionParams p{0, 0, 0, 0.0f, 10, 10, 10, 0.0f, 0, 0, 0};
    wrapPointPositionOne(in, out, 0, 1, p);
    ok &= approxEq(out.Position.x, 2.0f) && approxEq(out.Position.y, 2.0f) &&
          approxEq(out.Position.z, 2.0f) && approxEq(out.FX1, 1.0f);
  }
  {  // case 2
    SwPoint in{}, out{};
    in.Position = {15.0f, 0.0f, 0.0f};
    WrapPointPositionParams p{0, 0, 0, 0.0f, 10, 10, 10, 0.0f, 0, 0, 0};
    wrapPointPositionOne(in, out, 0, 1, p);
    ok &= approxEq(out.Position.x, 5.0f) && approxEq(out.Position.y, 0.0f) &&
          approxEq(out.Position.z, 0.0f) && approxEq(out.FX1, 0.0f);
  }
  {  // case 3
    SwPoint in{}, out{};
    in.Position = {15.0f, 0.0f, 0.0f};
    WrapPointPositionParams p{0, 0, 0, 0.0f, 10, 10, 10, 1.0f, 0, 0, 0};
    wrapPointPositionOne(in, out, 0, 1, p);
    ok &= approxEq(out.Position.x, 5.0f) && std::isnan(out.FX1);
  }
  {  // case 4
    SwPoint in{}, out{};
    in.Position = {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f};
    WrapPointPositionParams p{0, 0, 0, 0.0f, 10, 10, 10, 0.0f, 0, 0, 0};
    wrapPointPositionOne(in, out, 0, 1, p);
    ok &= approxEq(out.Position.x, -2.0f) && approxEq(out.Position.y, -2.0f) &&
          approxEq(out.Position.z, -2.0f) && approxEq(out.FX1, 0.010f);
  }
  {  // case 5
    SwPoint in{}, out{};
    in.Position = {3.0f, 4.0f, 5.0f};
    WrapPointPositionParams p{1, 1, 1, 1.0f, 10, 10, 10, 0.0f, 3, 4, 5};
    wrapPointPositionOne(in, out, 0, 1, p);
    ok &= approxEq(out.Position.x, 3.0f) && approxEq(out.Position.y, 4.0f) &&
          approxEq(out.Position.z, 5.0f) && approxEq(out.FX1, 1.0f);
  }

  return ok;
}

}  // namespace mathv_ref
}  // namespace sw
