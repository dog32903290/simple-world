// selftests_mathv_reorientlinepoints_probes.cpp — qSlerp clamp-hi/-lo branch coverage tooth,
// split out of selftests_mathv_reorientlinepoints.cpp 2026-07-10 (MASTER_PLAN.md 接力待辦 ㈢ D
// "clamp 補牙 6-6"): the PRIMARY random-line fuzz tooth's own header note (classifyQSlerp,
// selftests_mathv_reorientlinepoints_shared.h) says clamp-hi/-lo "have no simple closed-form trigger
// and are not engineered" — this file closes that gap with a deterministic construction instead of
// relying on random fuzz to land on an exact float boundary (measure-zero by construction).
//
// THE TRICK: qSlerp's clamp branches fire when cosHalfAngle=dot(a,b) is >=1.0 or <=-1.0 for UNIT
// quaternions a,b — i.e. a and b represent the identical rotation (b==a exactly, or b==-a exactly,
// since q and -q are the same physical rotation). Inside reorientLinePointsOne, `a` is the point's
// OWN current Rotation and `b = qAlignForward2(a, dir)` is DERIVED FROM `a` (via a's own "old up"
// continuity vector) — so qAlignForward2 is not a fixed formula independent of `a`; we cannot just
// pick a `b` we like. But `identity` (0,0,0,1) IS a fixed point of qAlignForward2(_, dir=(0,0,1)):
// mathv_ref_reorientlinepoints.h's self-check case B hand-derives qAlignForward2(identity,(0,0,1))
// == identity exactly (every intermediate value — cross/dot of axis-aligned unit vectors, sqrt(4)=2,
// 0.5/2=0.25 — is bit-exact in float, no rounding along the way). So: a straight line along +Z with
// the probed point's Rotation set to IDENTITY makes a==b==identity exactly -> cosHalfAngle=1.0
// (clamp-hi). Negating that same Rotation to (0,0,0,-1) (the SAME physical rotation, opposite sign)
// does not change `oldUp` (qRotateVec3's cross-product terms are all zero when q.xyz=(0,0,0), so the
// q.w sign never enters the computation) -> b is recomputed identically == identity == -a exactly ->
// cosHalfAngle=a.w*b.w+dot(a.xyz,b.xyz)=(-1)*1+0=-1.0 (clamp-lo). Both are exact float boundary hits
// by construction, not luck — verified against BOTH the GPU kernel and the CPU ref, with
// classifyQSlerp asserted to actually report the intended bucket (proving the branch really fired,
// not a near-miss landing in the acos path one ulp short of the boundary).
//
// ZONE: shell tier (app/src/ root, mathv support). Crosses runtime only for SwPoint + params ABI
// (via selftests_mathv_reorientlinepoints_shared.h).
#include "selftests_mathv_reorientlinepoints_shared.h"
#include <cstdio>

namespace sw {
namespace mathv_rlp_shared {

// Builds a 3-point straight +Z line ((0,0,0),(0,0,z1),(0,0,2*z1)) so the middle point's tangent
// (computed from its two live neighbours) is dir=(0,0,1) EXACTLY: v=(0,0,2*z1), l=sqrt(4*z1^2)=2*z1
// bit-exact for any z1 whose square doubles cleanly in float (z1=5 used below — 100.0f is exactly
// representable and IEEE-754 sqrt is correctly-rounded, so sqrt(100.0f)==10.0f bit-exact), giving
// dir=(0/l,0/l,l/l)=(0,0,1) bit-exact (no rounding in the division: 0/anything==0, l/l==1 for any
// nonzero finite l).
bool checkClampBranchTooth(const ReorientDispatch& disp, std::map<std::string, int>* slerpCov) {
  const float amount = 0.5f;  // arbitrary — clamp branches bypass Amount entirely (return `a` as-is).
  auto buildLine = [](float rw, float rx, float ry, float rz) {
    std::vector<SwPoint> line = {mkPt(0.0f, 0.0f, 0.0f, 1.0f), mkPt(0.0f, 0.0f, 5.0f, 1.0f),
                                 mkPt(0.0f, 0.0f, 10.0f, 1.0f)};
    line[1].Rotation = SW_FLOAT4{rx, ry, rz, rw};
    return line;
  };
  auto checkOne = [&](const char* label, const char* expectBucket, float rw) {
    std::vector<SwPoint> line = buildLine(rw, 0.0f, 0.0f, 0.0f);
    std::vector<SwPoint> gpuOut;
    bool dispatchOk = disp.dispatch(amount, line, gpuOut) && gpuOut.size() == 3;
    // ref: n+1 elements, padding[3].Scale.x=NaN (matches GPU's structurally-guarded semantics, same
    // convention as the main tooth — this probe isn't exercising the OOB quirk).
    std::vector<SwPoint> inPadded = {line[0], line[1], line[2], SwPoint{}};
    inPadded[3].Scale.x = std::numeric_limits<float>::quiet_NaN();
    SwPoint refOut = line[1];  // preseed: NO-WRITEBACK contract (unused here — this path always writes)
    mathv_ref::reorientLinePointsOne(inPadded.data(), &refOut, 1, 3, amount);
    // Recompute a/b exactly as reorientLinePointsOne does, purely for the classifier + the "did the
    // intended branch actually fire" sanity assertion (instrumentation only, never substitutes for
    // the compared GPU/ref values above).
    mathv_ref::quat::Quat a{0.0f, 0.0f, 0.0f, rw};
    mathv_ref::quat::Quat b = mathv_ref::qAlignForward2(a, {0.0f, 0.0f, 1.0f});
    const char* bucket = classifyQSlerp(a, b);
    if (slerpCov) ++(*slerpCov)[bucket];
    bool bucketOk = std::strcmp(bucket, expectBucket) == 0;
    // Expected output: `a` unchanged (clamp branches return `a` regardless of Amount/`b`/`t`).
    bool gpuOk = dispatchOk && std::fabs(gpuOut[1].Rotation.x - 0.0f) < 1e-6f &&
                std::fabs(gpuOut[1].Rotation.y - 0.0f) < 1e-6f &&
                std::fabs(gpuOut[1].Rotation.z - 0.0f) < 1e-6f &&
                std::fabs(gpuOut[1].Rotation.w - rw) < 1e-6f;
    bool refOk = std::fabs(refOut.Rotation.x - 0.0f) < 1e-6f &&
                std::fabs(refOut.Rotation.y - 0.0f) < 1e-6f &&
                std::fabs(refOut.Rotation.z - 0.0f) < 1e-6f &&
                std::fabs(refOut.Rotation.w - rw) < 1e-6f;
    printf("[mathv-reorientlinepoints-clamp-branch] %s: bucket=%s(expect=%s,ok=%s) gpu==a=%s "
           "ref==a=%s\n",
           label, bucket, expectBucket, bucketOk ? "yes" : "no", gpuOk ? "yes" : "no",
           refOk ? "yes" : "no");
    return dispatchOk && bucketOk && gpuOk && refOk;
  };
  bool okHi = checkOne("clamp-hi(identity==identity, cosHalfAngle=1.0)", "qslerp-clamp-hi", 1.0f);
  bool okLo = checkOne("clamp-lo(identity==-identity, cosHalfAngle=-1.0)", "qslerp-clamp-lo", -1.0f);
  return okHi && okLo;
}

}  // namespace mathv_rlp_shared
}  // namespace sw
