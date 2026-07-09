#pragma once
// mathv_ref_snaptopoints — CPU scalar oracle for the SnapToPoints kernel.
//
// TRANSCRIBED from external/tixl (SHA 395c4c55)
// Operators/Lib/Assets/shaders/points/modify/SnapToPoints.hlsl — NOT derived from sw shaders.
//   cbuffer Params : register(b0) :6-11  (BlendFactor/Distance/MaxAmount)
//   main()                       :18-28 -> snapToPointsOne() / mathvRefSnapToPoints() below,
//                                            every statement carries its own :NN back-reference.
// Every statement below carries its own :NN back-reference. The file is 30 lines total (incl.
// blank trailing line), zero branches, zero includes actually exercised by main() (see next para).
//
// The shader's own #include closure (:1-3 hash-functions.hlsl / point.hlsl / quat-functions.hlsl):
// only shared/point.hlsl (LegacyPoint struct, :14-16) is actually used by main(); hash-functions.hlsl
// and quat-functions.hlsl are included but nothing in main() calls a hash or quat helper — dead
// includes, not transcribed (same "dead include" pattern already documented in
// mathv_ref_snappointstogrid.h for shared/noise-functions.hlsl).
//
// Cross-checked against external/tixl/Operators/Lib/point/transform/SnapToPoints.cs:10-26 and the
// companion SnapToPoints.t3 graph (same directory): C# input names `PointsA_`/`PointsB_` are the
// HLSL `Points1`/`Points2`; `BlendValue` is HLSL `BlendFactor` (cbuffer field renamed at the C#
// binding layer — NOT the same identifier as the shader's own local variable also named
// `blendFactor`, see NAMED-FORK-BY-SHADOWING below). AMBIGUITY (dead input): `BlendMode` (C# `int`,
// wired through an `IntToFloat` node in the .t3 graph) has NO connection into the `FloatsToBuffer`
// node that builds this shader's cbuffer (traced in the .t3 `Connections` list) — it is computed
// and then discarded by the graph itself, never reaching this HLSL. Not modeled here.
//
// PROVENANCE (GOLDEN_STANDARD.md "P5-safe oracle 判準" / MATH_VERIFY_WORKFLOW.md §5): transcribed
// straight from the TiXL HLSL text above, never from any sw `app/shaders/*.metal` port
// (intentionally never opened while writing this header, per the Tier-L R-role isolation rule).
// Zero metal include, zero app/shaders/ reference, zero sw math helper.
//
// FLOAT (not double) on purpose (§2.3 branchy-class rule): smoothstep()'s internal saturate() is a
// float clamp on the GPU; a double ref could round differently at the clamp boundary.
//
// TIER-L classification (MATH_VERIFY_WORKFLOW.md line 85: "HLSL <60 行且無超越函式/無多分支"):
// this kernel is 11 lines of executable body (:21-27), has ZERO branches (no if/else, no ladder —
// straight-line code), and its only library calls are length() (Euclidean norm, algebraic not
// transcendental), smoothstep() and lerp() (both elementary polynomial/rational forms, same
// classification bucket as the already-Tier-L SnapPointsToGrid pilot, which additionally carries a
// 4-way Mode branch and bias/pow calls that this kernel entirely lacks). -> TIER-L.
//
// ZONE: shell-tier mathv support (pure math; no runtime/platform/Metal dependency). Only
// app/src/runtime/tixl_point.h is included, for the SwPoint host struct (data layout, not math).
#include "runtime/tixl_point.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace sw {
namespace mathv_ref {

// --- cbuffer field table -----------------------------------------------------------------------
// cbuffer Params : register(b0) (:6-11): 3 floats = 12B, fits in a single 16B slot, no padding.
//   offset 0  float BlendFactor  (:8)  -- cbuffer field. NAMED-FORK-BY-SHADOWING: main() (:24) also
//                                          declares a LOCAL variable named `blendFactor` (lowercase
//                                          b) holding an unrelated, smoothstep-derived value. The
//                                          two are DIFFERENT numbers used for DIFFERENT output
//                                          channels (:26 vs :27) -- see snapToPointsOne() below.
//   offset 4  float Distance     (:9)
//   offset 8  float MaxAmount    (:10)
struct SnapToPointsParams {
  float blendFactor;  // :8  BlendFactor (raw cbuffer value -- feeds :27's W-lerp directly, UNSCALED)
  float distance;     // :9  Distance
  float maxAmount;    // :10 MaxAmount
};

namespace detail {

inline float hlslSaturate(float x) {  // HLSL saturate(): clamp to [0,1] (inside smoothstep, :24).
  // Same NAMED FORK already pinned in mathv_ref_snappointstogrid.h / mathv_ref_wrappointposition.h:
  // real Metal/DX saturate(NaN) == 0 (neither x<0 nor x>1 is true for NaN under IEEE comparison
  // rules, so a naive `x<0?0:(x>1?1:x)` already returns x==NaN unclamped -- this explicit ladder
  // instead forces the DX/Metal-observed 0, matching hardware rather than the naive C++ ternary).
  if (!(x > 0.0f)) return 0.0f;  // x<=0 or x==NaN (NaN>0.0f is false) -> 0
  return (x < 1.0f) ? x : 1.0f;
}

// hlslSmoothstep(edge0, edge1, x) -- HLSL smoothstep, transcribed as the literal formula every HLSL
// compiler/driver expands it to (Microsoft docs): t = saturate((x-edge0)/(edge1-edge0)); return
// t*t*(3-2*t). NOTE this formula is used AS-IS even when edge0 > edge1 (this kernel's actual call
// site, :24, passes edge0=BlendFactor+Distance and edge1=Distance -- edge0 > edge1 whenever
// BlendFactor > 0). HLSL's own docs call min>=max "undefined", but the naive expansion is exactly
// what real compilers emit, and it degrades gracefully here: t decreases as x increases past edge0,
// i.e. a monotonically-decreasing falloff, which is the intentional "snap strength fades out beyond
// Distance+BlendFactor" behavior of this op (not a bug -- see self-check case 2/3 below).
inline float hlslSmoothstep(float edge0, float edge1, float x) {
  const float t = hlslSaturate((x - edge0) / (edge1 - edge0));  // :24 inlined
  return t * t * (3.0f - 2.0f * t);
}

}  // namespace detail

// snapToPointsOne — one thread of `main()` (:18-28), operating on a single point pair.
//
// `a` == Points1[i.x] (:21), `snapPoint` == Points2[i.x] (:22). `out` is ResultPoints[i.x] (:26-27),
// passed by reference: this function writes ONLY `out.Position` and `out.FX1` (LegacyPoint.W ==
// SwPoint.FX1 @12, see point.hlsl :12/:22 and the identical note in mathv_ref_wrappointposition.h).
// NOTHING ELSE ON `out` IS TOUCHED -- unlike WrapPointPosition (which operates in-place on a single
// aliased buffer, so untouched fields trivially "pass through" as the same memory), SnapToPoints
// has THREE DISTINCT buffers (Points1 SRV t0, Points2 SRV t1, ResultPoints UAV u0, :14-16): the
// kernel never reads or writes ResultPoints[i.x].Rotation/.Color/.Stretch/.Selected at all. What
// ends up in those fields on the real GPU depends entirely on whatever the surrounding TiXL
// buffer-management graph seeded ResultPoints with before dispatch (out of scope for this HLSL
// text and for this CPU ref -- AMBIGUITY, not modeled; the self-check below proves the "untouched"
// contract by pre-seeding `out` with sentinel values and asserting they survive unchanged).
inline void snapToPointsOne(const SwPoint& a, const SwPoint& snapPoint, SwPoint& out,
                             const SnapToPointsParams& prm) {
  // :23 -- float distance = length(A.Position - SnapPoint.Position);
  const float dx = a.Position.x - snapPoint.Position.x;
  const float dy = a.Position.y - snapPoint.Position.y;
  const float dz = a.Position.z - snapPoint.Position.z;
  const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

  // :24 -- float blendFactor = smoothstep(BlendFactor + Distance, Distance, distance) * MaxAmount;
  // LOCAL variable, shadows the cbuffer field of the same name -- only used for :26 (Position).
  const float edge0 = prm.blendFactor + prm.distance;
  const float edge1 = prm.distance;
  const float localBlendFactor = detail::hlslSmoothstep(edge0, edge1, distance) * prm.maxAmount;

  // :26 -- ResultPoints[i.x].Position = lerp(A.Position, SnapPoint.Position, blendFactor);
  // HLSL lerp(a,b,t) = a + t*(b-a), NOT clamped to t in [0,1] -- localBlendFactor is already
  // saturate()-derived (via smoothstep) times MaxAmount, so it lands in [0, MaxAmount]; if
  // MaxAmount > 1 the position can extrapolate past SnapPoint (transcribed as-is, no extra clamp).
  out.Position.x = a.Position.x + localBlendFactor * (snapPoint.Position.x - a.Position.x);
  out.Position.y = a.Position.y + localBlendFactor * (snapPoint.Position.y - a.Position.y);
  out.Position.z = a.Position.z + localBlendFactor * (snapPoint.Position.z - a.Position.z);

  // :27 -- ResultPoints[i.x].W = lerp(A.W, SnapPoint.W, BlendFactor);
  // NAMED-FORK-BY-SHADOWING (transcribed verbatim, this is the whole point of this file): this
  // line reads the RAW CBUFFER `BlendFactor` (prm.blendFactor), NOT `localBlendFactor` computed
  // one line above. The W channel therefore completely ignores distance/smoothstep/MaxAmount --
  // it is a flat, un-clamped lerp by whatever BlendFactor the artist dialed in (can be < 0 or > 1,
  // producing extrapolation beyond snapPoint.W in either direction). This is almost certainly an
  // authoring hand-slip in TiXL (identifier collision between cbuffer field and local variable),
  // but per P5-safe transcription discipline it is preserved exactly, not "fixed".
  out.FX1 = a.FX1 + prm.blendFactor * (snapPoint.FX1 - a.FX1);
}

// mathvRefSnapToPoints — full-buffer CPU oracle (dispatch loop over `count` points, no bounds guard
// in the HLSL to replicate -- :18-28 has no GetDimensions/idx check at all, unlike
// WrapPointPosition; a host array sized to exactly `count` has no over-dispatch concept anyway).
inline void mathvRefSnapToPoints(const SwPoint* a, const SwPoint* snapPoints, SwPoint* out,
                                  size_t count, const SnapToPointsParams& prm) {
  for (size_t i = 0; i < count; ++i) {
    snapToPointsOne(a[i], snapPoints[i], out[i], prm);
  }
}

// --- self-check: hand-derived sanity values (independent of the code above; re-derive by hand
// from the HLSL text, not by reading this file's own logic back) ------------------------------
//
// 1) BlendFactor=0 (degenerate denominator: edge0==edge1==Distance -> t=saturate(x/0)):
//    Distance=5, MaxAmount=1, A.Position=(0,0,0), SnapPoint.Position=(3,0,0) -> distance=3.
//    edge0=0+5=5, edge1=5. t=saturate((3-5)/(5-5))=saturate(-2/+0)=saturate(-inf)=0.
//    smooth=0; localBlendFactor=0*1=0. Position_out = A = (0,0,0) (unchanged).
//    W: lerp(A.W, SnapPoint.W, BlendFactor=0) = A.W (also unchanged, coincidentally same 0 here).
//
// 2) THE quirk, demonstrated (local vs raw BlendFactor differ): BlendFactor=2, Distance=3,
//    MaxAmount=1, A.Position=(0,0,0), SnapPoint.Position=(4,0,0) -> distance=4.
//    edge0=2+3=5, edge1=3 (edge0>edge1 -- the "reversed" smoothstep call, :24).
//    t=saturate((4-5)/(3-5))=saturate(-1/-2)=saturate(0.5)=0.5.
//    smooth=0.5*0.5*(3-1)=0.25*2=0.5. localBlendFactor=0.5*1=0.5.
//    Position_out = lerp((0,0,0),(4,0,0),0.5) = (2,0,0).
//    W: A.FX1=10, SnapPoint.FX1=20. W_out = lerp(10,20, BlendFactor=2 [RAW, not 0.5!])
//       = 10 + 2*(20-10) = 30 -- extrapolated PAST SnapPoint.FX1 (20), because the W-lerp t=2 is
//       unclamped and totally decoupled from the Position-lerp t=0.5 computed one line above.
//
// 3) Same params as (2) but distance=10 (far beyond edge0=5 -> Position doesn't snap at all):
//    t=saturate((10-5)/(3-5))=saturate(5/-2)=saturate(-2.5)=0 -> localBlendFactor=0 ->
//    Position_out = A (unchanged). W_out is STILL 30 (identical to case 2) -- proves the W channel
//    is fully independent of `distance`/the snap falloff entirely, only case (2)'s A/SnapPoint.FX1
//    and the raw BlendFactor matter.
//
// 4) Untouched-fields contract: pre-seed `out` with sentinel Rotation/Color/Scale/FX2 (values that
//    match NEITHER `a` nor `snapPoint`), call snapToPointsOne(), assert those 4 fields on `out` are
//    bit-identical to the sentinels afterward (function must never touch them).
inline bool mathvRefSnapToPointsSelfCheck() {
  auto approxEq = [](float x, float y, float eps = 1e-5f) { return std::fabs(x - y) <= eps; };
  bool ok = true;

  {  // case 1
    SwPoint a{}, sp{}, out{};
    a.Position = {0.0f, 0.0f, 0.0f};
    sp.Position = {3.0f, 0.0f, 0.0f};
    SnapToPointsParams prm{0.0f, 5.0f, 1.0f};
    snapToPointsOne(a, sp, out, prm);
    ok &= approxEq(out.Position.x, 0.0f) && approxEq(out.Position.y, 0.0f) &&
          approxEq(out.Position.z, 0.0f) && approxEq(out.FX1, 0.0f);
  }
  {  // case 2 -- the quirk
    SwPoint a{}, sp{}, out{};
    a.Position = {0.0f, 0.0f, 0.0f};
    a.FX1 = 10.0f;
    sp.Position = {4.0f, 0.0f, 0.0f};
    sp.FX1 = 20.0f;
    SnapToPointsParams prm{2.0f, 3.0f, 1.0f};
    snapToPointsOne(a, sp, out, prm);
    ok &= approxEq(out.Position.x, 2.0f) && approxEq(out.Position.y, 0.0f) &&
          approxEq(out.Position.z, 0.0f) && approxEq(out.FX1, 30.0f);
  }
  {  // case 3 -- distance decoupling
    SwPoint a{}, sp{}, out{};
    a.Position = {0.0f, 0.0f, 0.0f};
    a.FX1 = 10.0f;
    sp.Position = {10.0f, 0.0f, 0.0f};  // distance=10
    sp.FX1 = 20.0f;
    SnapToPointsParams prm{2.0f, 3.0f, 1.0f};
    snapToPointsOne(a, sp, out, prm);
    ok &= approxEq(out.Position.x, 0.0f) && approxEq(out.FX1, 30.0f);
  }
  {  // case 4 -- untouched fields
    SwPoint a{}, sp{}, out{};
    a.Position = {0.0f, 0.0f, 0.0f};
    sp.Position = {1.0f, 0.0f, 0.0f};
    out.Rotation = {9.0f, 8.0f, 7.0f, 6.0f};
    out.Color = {5.0f, 4.0f, 3.0f, 2.0f};
    out.Scale = {1.5f, 2.5f, 3.5f};
    out.FX2 = 42.0f;
    SnapToPointsParams prm{0.5f, 1.0f, 1.0f};
    snapToPointsOne(a, sp, out, prm);
    ok &= approxEq(out.Rotation.x, 9.0f) && approxEq(out.Rotation.y, 8.0f) &&
          approxEq(out.Rotation.z, 7.0f) && approxEq(out.Rotation.w, 6.0f) &&
          approxEq(out.Color.x, 5.0f) && approxEq(out.Color.y, 4.0f) &&
          approxEq(out.Color.z, 3.0f) && approxEq(out.Color.w, 2.0f) &&
          approxEq(out.Scale.x, 1.5f) && approxEq(out.Scale.y, 2.5f) &&
          approxEq(out.Scale.z, 3.5f) && approxEq(out.FX2, 42.0f);
  }

  return ok;
}

}  // namespace mathv_ref
}  // namespace sw
