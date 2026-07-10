#pragma once
// mathv_ref_reorientlinepoints — CPU scalar oracle for the ReorientLinePoints kernel.
//
// TRANSCRIBED from external/tixl (SHA 395c4c55)
// Operators/Lib/Assets/shaders/points/modify/ReorientLinePoints.hlsl — NOT derived from sw shaders.
//   cbuffer Params : register(b0) :6-13  (Center/Amount/UpVector/UseWAsWeight/Flip)
//   qAlignForward   :18-48   -- DEAD CODE, never called by main() (see DEAD-CODE note below)
//   qAlignForward2  :50-75   -> qAlignForward2() below (the ONLY variant main() calls, :150)
//   qAlignForward3  :77-103  -- DEAD CODE, never called by main() (see DEAD-CODE note below)
//   main()          :105-152 -> reorientLinePointsOne() / mathvRefReorientLinePoints() below
// Every statement below carries its own :NN back-reference.
//
// Also reads ReorientLinePoints.hlsl's own #include closure (opened only because
// ReorientLinePoints.hlsl itself pulls them in -- no sw shader file was opened to write this ref):
//   shared/point.hlsl :19-27 (struct Point layout; already ported host-side as SwPoint in
//     runtime/tixl_point.h, reused not re-derived).
//   shared/quat-functions.hlsl's qRotateVec3/qLookAt/qSlerp are NOT re-transcribed here: they are
//     already a qualifying P5-safe oracle at mathv_ref_shared_quat.h (TRANSCRIBED from the same
//     pinned SHA, quat-functions.hlsl :47-51/:103-160/:162-215), reused verbatim via
//     `sw::mathv_ref::quat::*` per MATH_VERIFY_WORKFLOW.md §1.4's "共享 oracle 復用" instruction --
//     this file supplies only the ReorientLinePoints-specific kernel body (qAlignForward2 + main())
//     around it. shared/hash-functions.hlsl and shared/noise-functions.hlsl are ALSO pulled in by
//     ReorientLinePoints.hlsl's #include list (:1-2) but neither hash41u nor any noise function is
//     ever called anywhere in this file -- dead #includes, nothing to transcribe.
//
// Cross-checked against the C# op at
// external/tixl/Operators/Lib/point/transform/ReorientLinePoints.cs:7-26 -- Amount/Center/UpVector/
// WIsWeight/Flip input slots line up 1:1 with the cbuffer field NAMES, but see the NOTED-QUIRK below:
// Center/UpVector/UseWAsWeight/Flip are all DEAD ABI -- declared in the cbuffer and wired from C#
// inputs, but never read anywhere in main() or either qAlignForward variant it calls. Only Amount is
// live. Transcribed verbatim (this is TiXL's own kernel, not a translation slip); flagged for S in
// case the pinned SHA is stale relative to a newer TiXL commit that starts using these fields.
//
// PROVENANCE (GOLDEN_STANDARD.md "P5-safe oracle 判準" / MATH_VERIFY_WORKFLOW.md §5): transcribed
// straight from the TiXL HLSL text above, never from any sw `app/shaders/*.metal` port
// (intentionally never opened while writing this header). Zero metal include, zero app/shaders/
// reference, zero sw math helper.
//
// FLOAT (not double) on purpose (§2.3 branchy-class rule): every branch here (the `l < 0.0001`
// degeneracy guard, `length(projUp) < 1e-5`, qLookAt's/qSlerp's internal branch ladders) is a float
// comparison on the GPU; a double CPU ref could land on the opposite side of a branch boundary than
// the float GPU for the same nominal input.
//
// ZONE: shell-tier mathv support (pure math; no runtime/platform/Metal dependency). Only
// runtime/tixl_point.h (SwPoint host struct) and the sibling mathv_ref_shared_quat.h are included.
#include "runtime/tixl_point.h"
#include "mathv_ref_shared_quat.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace sw {
namespace mathv_ref {

// --- cbuffer field table -----------------------------------------------------------------------
// cbuffer Params : register(b0) (:6-13, HLSL cbuffer packing, same float3+scalar-fills-slot pattern
//                                 documented in mathv_ref_wrappointposition.h):
//   offset  0  float3 Center           (:8)  slot0 lo -- DEAD, never read (see file header note)
//   offset 12  float  Amount           (:9)  slot0 hi -- the ONLY live field, used at :150
//   offset 16  float3 UpVector         (:10) slot1 lo -- DEAD
//   offset 28  float  UseWAsWeight     (:11) slot1 hi -- DEAD
//   offset 32  float  Flip             (:12) slot2    -- DEAD (own slot; no trailing field to share
//                                                         it, cbuffer total size rounds to 48B)
// All five fields are included in this params struct anyway for ABI/binding-table completeness (the
// GPU-side cbuffer layout a fuzz driver must byte-match), even though this oracle's math only ever
// reads `.amount`.
struct ReorientLinePointsParams {
  float centerX, centerY, centerZ;  // :8  Center      -- DEAD
  float amount;                     // :9  Amount      -- LIVE (:150)
  float upVectorX, upVectorY, upVectorZ;  // :10 UpVector  -- DEAD
  float useWAsWeight;                // :11 UseWAsWeight -- DEAD
  float flip;                        // :12 Flip         -- DEAD
};

// qAlignForward2 — ReorientLinePoints.hlsl :50-75. Aligns orientation quaternion `q`'s +Z forward
// towards `newForward`, using `q`'s OWN current up-vector (rotated (0,1,0)) as a continuity
// reference so successive points along a line don't flip their up-vector discontinuously as
// `newForward` sweeps around. DEAD-CODE note: ReorientLinePoints.hlsl declares two sibling
// functions with the identical stated purpose -- `qAlignForward` (:18-48, negates newForward first
// and calls qFromMatrix3Precise directly) and `qAlignForward3` (:77-103, same as qAlignForward but
// without the negation) -- neither is ever called; main() (:150) calls only `qAlignForward2`. Not
// modeled here (dead source doesn't affect kernel output; flagged for S/X in case a future TiXL
// edit switches main() to call one of the other two).
inline quat::Quat qAlignForward2(quat::Quat q, quat::Vec3 newForward) {
  newForward = quat::hlslNormalize3(newForward);  // :54

  // :57 -- old up from current orientation (+Y rotated by q).
  quat::Vec3 oldUp = quat::qRotateVec3({0.0f, 1.0f, 0.0f}, q);

  // :60 -- project old up onto the plane perpendicular to newForward.
  float d = quat::hlslDot3(oldUp, newForward);
  quat::Vec3 projUp{oldUp.x - newForward.x * d, oldUp.y - newForward.y * d,
                     oldUp.z - newForward.z * d};

  // :63-71 -- degeneracy handling: oldUp (nearly) parallel to newForward -> projUp collapses to
  // (near-)zero; pick an arbitrary reference axis (world X unless newForward is itself close to
  // world X, in which case fall back to world Y) and re-project THAT onto the perpendicular plane.
  if (quat::hlslLength3(projUp) < 1e-5f) {                                          // :63
    quat::Vec3 axis = std::fabs(newForward.x) < 0.9f ? quat::Vec3{1.0f, 0.0f, 0.0f}
                                                      : quat::Vec3{0.0f, 1.0f, 0.0f};  // :65
    projUp = quat::hlslNormalize3(axis);                                            // :65
    float d2 = quat::hlslDot3(projUp, newForward);
    projUp = quat::hlslNormalize3({projUp.x - newForward.x * d2, projUp.y - newForward.y * d2,
                                    projUp.z - newForward.z * d2});                  // :66
  } else {
    projUp = quat::hlslNormalize3(projUp);                                          // :70
  }

  // :74 -- rebuild quaternion with forward=newForward, up ~ projected up (NEGATED -- qLookAt's
  // `up` argument convention here is the opposite handedness of the plain projUp reference vector).
  return quat::qLookAt(newForward, {-projUp.x, -projUp.y, -projUp.z});
}

// reorientLinePointsOne — one thread of `main()` (:105-152), operating on a single point plus its
// two immediate line-neighbours.
//
// BUFFER-ALIASING / NO-WRITEBACK QUIRK (the single most important semantic fact about this kernel,
// AMBIGUITY for S/X): every early `return;` in main() (:112, :115, :133, :141) writes NOTHING to
// ResultPoints[i.x] -- unlike mathv_ref_wrappointposition.h / mathv_ref_addnoise.h's early-return
// branches (which explicitly do `out = in;`), this HLSL leaves the output buffer slot completely
// untouched on those paths. On real hardware this is only well-defined because ReorientLinePoints
// is always dispatched with SourcePoints and ResultPoints bound to the SAME underlying buffer (an
// in-place ping-pong), so "don't write" reads back as "unchanged" on the next access. This oracle
// reproduces that literally: `out[idx]` is simply never assigned on these paths. Callers MUST seed
// `out` (e.g. `out[i] = in[i]` before calling, or pass `in == out`) to get the in-place-buffer
// semantics the GPU relies on; a caller who leaves `out` uninitialized on these paths will observe
// stale/garbage data at `out[idx]`, exactly mirroring what a real GPU dispatch would show if
// SourcePoints/ResultPoints were ever (incorrectly) bound to two DIFFERENT un-preseeded buffers.
//
// OOB-READ QUIRK (AMBIGUITY, NAMED FORK -- genuine TiXL kernel bug, not a translation artifact):
// the next-neighbour guard at :126 is `if (index <= numStructs - 1 && ...)`. Given the earlier
// `index >= numStructs` guard (:111) already established `index < numStructs`, the condition
// `index <= numStructs - 1` is ALGEBRAICALLY ALWAYS TRUE (it is exactly `index < numStructs`, which
// already held) -- the evident intent was `index < numStructs - 1` (i.e. "index is not the last
// element"), guarding the `SourcePoints[index + 1]` read just below it. As written, when
// `index == numStructs - 1` (the LAST point), the guard does NOT block the read, and
// `SourcePoints[numStructs]` -- one past the declared buffer size -- is read: an out-of-bounds
// StructuredBuffer access, undefined per the D3D/Metal spec (real GPUs typically return zero-fill
// for slack capacity past `Count` but within the allocated buffer, which is presumably why this has
// never visibly misbehaved in practice). A CPU host array cannot safely mirror an OOB read past its
// own allocation, so this oracle's contract requires the caller to allocate `in` with `count + 1`
// elements: index `count` is a caller-supplied PADDING point mirroring whatever the GPU's backing
// buffer slack would contain at that address. `mathvRefReorientLinePoints()` below documents this
// requirement on its own signature; a fuzz driver wanting byte-exact GPU/CPU agreement at this
// specific boundary sample must feed the SAME padding value into both the real GPU buffer's slack
// slot and this oracle's `in[count]`.
inline void reorientLinePointsOne(const SwPoint* in, SwPoint* out, uint32_t index,
                                   uint32_t numStructs, float amount) {
  // :111-112 -- dispatch-rounding guard, same AMBIGUITY class as mathv_ref_wrappointposition.h's:
  // unreachable through mathvRefReorientLinePoints()'s own loop (index always < numStructs==count
  // there); a GPU fuzz driver probing over-dispatch must set this up itself, out of scope here.
  if (index >= numStructs) {
    return;  // NO WRITE -- see NO-WRITEBACK QUIRK above.
  }

  // :114-115 -- a point flagged NaN in Scale.x is a "line break" sentinel (same convention family as
  // FX1==NaN in mathv_ref_wrappointposition.h's line-break write); reorientation skips it entirely.
  if (std::isnan(in[index].Scale.x)) {
    return;  // NO WRITE.
  }

  // :117-124 -- find neighbours. prevIndex only decrements if there IS a previous slot (index>0)
  // AND it is not itself a line-break sentinel.
  uint32_t prevIndex = index;
  uint32_t nextIndex = index;

  if (index > 0 && !std::isnan(in[index - 1].Scale.x)) {  // :121
    prevIndex = index - 1;                                 // :123
  }

  // :126-129 -- next-neighbour bump. See OOB-READ QUIRK above: this condition is always true when
  // reached, so `in[index + 1]` (the caller-supplied padding slot when index==numStructs-1) is
  // always consulted.
  if (index <= numStructs - 1 && !std::isnan(in[index + 1].Scale.x)) {  // :126
    nextIndex = index + 1;                                              // :128
  }

  // :132-133 -- nothing to align against (both neighbour searches landed back on `index` itself,
  // e.g. an isolated single point flanked by line-break sentinels on both sides).
  if (prevIndex == nextIndex) {
    return;  // NO WRITE.
  }

  // :135 -- direction vector between the two neighbours (NOT index's own position).
  quat::Vec3 v{in[nextIndex].Position.x - in[prevIndex].Position.x,
               in[nextIndex].Position.y - in[prevIndex].Position.y,
               in[nextIndex].Position.z - in[prevIndex].Position.z};

  // :138-141 -- degenerate near-coincident neighbours -> direction undefined, skip.
  float l = quat::hlslLength3(v);
  if (l < 0.0001f) {
    return;  // NO WRITE.
  }

  quat::Vec3 dir{v.x / l, v.y / l, v.z / l};  // :143

  // :144-150 -- copy the WHOLE point, then blend only .Rotation toward the realigned target by
  // `Amount` (qSlerp(current, target, Amount)). All non-Rotation fields pass through unchanged.
  SwPoint p = in[index];                                    // :144
  quat::Quat r{p.Rotation.x, p.Rotation.y, p.Rotation.z, p.Rotation.w};  // :146
  quat::Quat target = qAlignForward2(r, dir);
  quat::Quat blended = quat::qSlerp(r, target, amount);      // :150

  *out = p;
  out->Rotation.x = blended.x;
  out->Rotation.y = blended.y;
  out->Rotation.z = blended.z;
  out->Rotation.w = blended.w;
  // :151 -- ResultPoints[i.x] = p; (Position/Color/Scale/FX1/FX2 already carried via `*out = p`).
}

// mathvRefReorientLinePoints — full-buffer CPU oracle (:105-107 dispatch loop over `count` points).
//
// `in` MUST have `count + 1` valid elements -- see OOB-READ QUIRK above; `in[count]` is the
// caller-chosen padding point consulted only when reorienting the LAST real point (index==count-1).
// `out` MUST have `count` elements and must already hold the desired "unchanged" value at every
// index the kernel may skip (see NO-WRITEBACK QUIRK) -- typically the caller pre-seeds
// `out[i] = in[i]` for i in [0,count), or aliases `out == in` directly (safe: every write path reads
// `in[index]`/`in[prevIndex]`/`in[nextIndex]` into locals/copies before writing `*out`).
inline void mathvRefReorientLinePoints(const SwPoint* in, SwPoint* out, size_t count, float amount) {
  for (size_t i = 0; i < count; ++i) {
    reorientLinePointsOne(in, &out[i], static_cast<uint32_t>(i), static_cast<uint32_t>(count),
                          amount);
  }
}

// --- self-check: hand-derived sanity values (independent of the code above; re-derive by hand
// from the HLSL text, not by reading this file's own logic back) ------------------------------
//
// Shared hand-derivation for cases A/C/E below (3 collinear points, index=1 or 2 has neighbours
// exactly `d` apart along +X, so dir==(1,0,0)):
//   qAlignForward2(identity=(0,0,0,1), newForward=(1,0,0)):
//     newForward normalized = (1,0,0). oldUp = qRotateVec3((0,1,0),identity) = (0,1,0) (identity
//     rotation leaves any vector unchanged -- q.xyz=0 -> t=2*cross(0,v)=0 -> v+1*0+cross(0,0)=v).
//     d=dot((0,1,0),(1,0,0))=0 -> projUp=(0,1,0)-0=(0,1,0); length=1, not <1e-5 -> normalize->(0,1,0).
//     qLookAt(forward=(1,0,0), up=-(0,1,0)=(0,-1,0)):
//       right=normalize(cross((1,0,0),(0,-1,0)))=normalize((0*0-0*(-1), 0*0-1*0, 1*(-1)-0*0))
//            =normalize((0,0,-1))=(0,0,-1).
//       up=normalize(cross((1,0,0),(0,0,-1)))=normalize((0*(-1)-0*0, 0*0-1*(-1), 1*0-0*0))
//         =normalize((0,1,0))=(0,1,0).
//       m00=0,m01=0,m02=-1, m10=0,m11=1,m12=0, m20=1,m21=0,m22=0.
//       num8=m00+m11+m22=0+1+0=1>0 -> num=sqrt(1+1)=sqrt(2); q.w=sqrt(2)/2≈0.70710678;
//       num=0.5/sqrt(2)≈0.35355339; q.x=(m12-m21)*num=0; q.y=(m20-m02)*num=(1-(-1))*num=2*num
//         ≈0.70710678; q.z=(m01-m10)*num=0.
//     => target ≈ (0, 0.70710678, 0, 0.70710678) (already unit: 0.5+0.5=1).
inline bool mathvRefReorientLinePointsSelfCheck() {
  auto approxEq = [](float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; };
  const float kInvSqrt2 = 0.70710678f;
  bool ok = true;

  {  // case A: Amount=0 -> qSlerp(a,b,0)==a exactly (blendB==sin(halfAngle*0)*x==0 regardless of b),
     // so rotation passes through unchanged even though target != identity.
    SwPoint pts[4] = {};
    pts[0].Position = {0.0f, 0.0f, 0.0f};
    pts[1].Position = {5.0f, 0.0f, 0.0f};
    pts[1].Rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    pts[2].Position = {10.0f, 0.0f, 0.0f};
    pts[3].Scale.x = std::numeric_limits<float>::quiet_NaN();  // unused pad for this call
    SwPoint out{};
    reorientLinePointsOne(pts, &out, 1, 3, 0.0f);
    ok &= approxEq(out.Rotation.x, 0.0f) && approxEq(out.Rotation.y, 0.0f) &&
          approxEq(out.Rotation.z, 0.0f) && approxEq(out.Rotation.w, 1.0f) &&
          approxEq(out.Position.x, 5.0f);
  }
  {  // case B: qAlignForward2 in isolation, forward=(0,0,1) (z-axis) with q=identity -> num8=3>0
     // branch of qLookAt yields q=(0,0,0,1) exactly (see file header hand-derivation).
    quat::Quat q = qAlignForward2({0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f});
    ok &= approxEq(q.x, 0.0f) && approxEq(q.y, 0.0f) && approxEq(q.z, 0.0f) && approxEq(q.w, 1.0f);
  }
  {  // case C: same 3-point setup as A but Amount=1 -> qSlerp(a,b,1)==b exactly -> output == target.
    SwPoint pts[4] = {};
    pts[0].Position = {0.0f, 0.0f, 0.0f};
    pts[1].Position = {5.0f, 0.0f, 0.0f};
    pts[1].Rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    pts[2].Position = {10.0f, 0.0f, 0.0f};
    SwPoint out{};
    reorientLinePointsOne(pts, &out, 1, 3, 1.0f);
    ok &= approxEq(out.Rotation.x, 0.0f) && approxEq(out.Rotation.y, kInvSqrt2) &&
          approxEq(out.Rotation.z, 0.0f) && approxEq(out.Rotation.w, kInvSqrt2);
  }
  {  // case D: isnan(Scale.x) sentinel -> NO WRITE at all (out must stay whatever it was, NOT
     // overwritten to match `in` -- proves this is a true no-writeback, not an implicit passthrough).
    SwPoint in{};
    in.Position = {1.0f, 2.0f, 3.0f};
    in.Scale.x = std::numeric_limits<float>::quiet_NaN();
    SwPoint out{};
    out.Position = {-9.0f, -9.0f, -9.0f};  // sentinel value distinct from `in`
    reorientLinePointsOne(&in, &out, 0, 1, 1.0f);
    ok &= approxEq(out.Position.x, -9.0f) && approxEq(out.Position.y, -9.0f) &&
          approxEq(out.Position.z, -9.0f);
  }
  {  // case E: OOB-READ QUIRK at the LAST real point (index==numStructs-1==2, numStructs=3) with a
     // NaN-sentinel pad slot in[3] -> nextIndex bump condition false -> falls back to
     // prevIndex=1/nextIndex=2 (backward difference), same target math as case C (dir==(1,0,0)).
    SwPoint pts[4] = {};
    pts[0].Position = {0.0f, 0.0f, 0.0f};
    pts[1].Position = {5.0f, 0.0f, 0.0f};
    pts[2].Position = {10.0f, 0.0f, 0.0f};
    pts[2].Rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    pts[3].Scale.x = std::numeric_limits<float>::quiet_NaN();  // pad slot: "no more real data"
    SwPoint out{};
    reorientLinePointsOne(pts, &out, 2, 3, 1.0f);
    ok &= approxEq(out.Rotation.x, 0.0f) && approxEq(out.Rotation.y, kInvSqrt2) &&
          approxEq(out.Rotation.z, 0.0f) && approxEq(out.Rotation.w, kInvSqrt2) &&
          approxEq(out.Position.x, 10.0f);
  }

  return ok;
}

}  // namespace mathv_ref
}  // namespace sw
