// selftests_mathv_snaptopoints.cpp — --selftest-mathv-snaptopoints (D role, fuzz driver).
// Fuzzes the GPU "snaptopoints" kernel against the R-authored CPU oracle (mathv_ref_snaptopoints.h,
// TRANSCRIBED from external/tixl HLSL) via the shared mathv harness (direct-kernel dispatch, §1.3).
// ParamDomain (external/tixl SHA 395c4c55): SnapToPoints.t3:9-19 DEFAULTS only (all 0.0), no .t3ui
// Min/Max -> §3.2 fallback domain [-4,4] for all three.
//
// TWO SRVs: this op pairs Points1[i] with Points2[i] index-wise. The generic 3-layer harness only
// carries ONE flat element vector, so this TU folds BOTH points' Position+FX1 into a single 8-wide
// element: in=[A.xyz,A.FX1, Snap.xyz,Snap.FX1], out=[Position.xyz,FX1] (outDim=4). inDim(8)!=
// outDim(4) so the generic identity sentinel cannot apply — see checkIdentityTooth below.
//
// FIVE EXTRA TEETH (each documented at its own check function): IDENTITY (BlendFactor=0+MaxAmount=0
// forces Position_out==A/FX1_out==A.FX1), BOUNDARY (smoothstep saturate band + UB/ULP loci below --
// ESCALATED to branchy), UNTOUCHED (Rotation/Color/Scale/FX2 passthrough), COUNTGUARD (Points2Count
// NAMED FORK pin), WQUIRK (BlendFactor=2 W-lerp extrapolation, pinned parity).
//
// AMBIGUITY (UB): smoothstep(edge0==edge1) is spec-UB in both HLSL (MS: min>=max) and MSL. Measured
// XS 2026-07-10: real GPU->0 at BlendFactor==0.0 vs the naive ref's ->1 when distance>Distance
// (0<->1 inversion, 1536/172032 samples, benign). Position is class-excluded there (snapUbLocus
// below, budget-free); FX1 unaffected. Absorbed-but-nonzero BlendFactor + near-saturate-edge/huge-param tail get a genuine Branchy exemption instead.
//
// ZONE: shell tier (mathv support). Crosses runtime only for SwPoint + the params ABI header.
#include "mathv_harness.h"
#include "mathv_ref_snaptopoints.h"
#include "runtime/selftest_registry.h"
#include "runtime/snaptopoints_params.h"
#include "runtime/tixl_point.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

using mathv::Comparator;
using mathv::EpsSpec;
using mathv::MathvCase;
using mathv::ParamDomain;
using mathv::Rng;

// direct-kernel dispatch adapter (turbulence_parity_golden.cpp:75-112 shape): 2 SRVs + 1 UAV.
struct SnapDispatch {
  MTL::Device* dev;
  MTL::CommandQueue* queue;
  MTL::ComputePipelineState* pso = nullptr;
  bool ok = false;

  SnapDispatch(MTL::Device* d, MTL::CommandQueue* q, MTL::Library* lib) : dev(d), queue(q) {
    MTL::Function* fn = lib->newFunction(NS::String::string("snaptopoints", NS::UTF8StringEncoding));
    if (!fn) return;
    NS::Error* err = nullptr;
    pso = dev->newComputePipelineState(fn, &err);
    fn->release();
    ok = pso != nullptr;
  }
  ~SnapDispatch() { if (pso) pso->release(); }
  SnapDispatch(const SnapDispatch&) = delete;

  // pts2.size() may be 0 or < pts1.size() (count-guard NAMED FORK). P={BlendFactor,Distance,MaxAmount}.
  bool dispatch(const std::vector<float>& P, const std::vector<SwPoint>& pts1,
                const std::vector<SwPoint>& pts2, std::vector<SwPoint>& out) const {
    if (!ok || P.size() != 3) return false;
    const uint32_t n = (uint32_t)pts1.size();
    out.clear();
    if (n == 0) return true;
    MTL::Buffer* p1Buf =
        dev->newBuffer(pts1.data(), (NS::UInteger)(n * sizeof(SwPoint)), MTL::ResourceStorageModeShared);
    const uint32_t n2 = (uint32_t)pts2.size();
    // Production fallback: reuse Points1 as the dummy Points2 buffer when empty (kernel never reads it).
    MTL::Buffer* p2Buf = (n2 > 0) ? dev->newBuffer(pts2.data(), (NS::UInteger)(n2 * sizeof(SwPoint)),
                                                    MTL::ResourceStorageModeShared) : nullptr;
    MTL::Buffer* dstBuf = dev->newBuffer((NS::UInteger)(n * sizeof(SwPoint)), MTL::ResourceStorageModeShared);
    SnapToPointsParams params{};
    params.Count = n;
    params.Points2Count = n2;
    params.BlendFactor = P[0];
    params.Distance = P[1];
    params.MaxAmount = P[2];
    MTL::CommandBuffer* cmd = queue->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pso);
    enc->setBuffer(p1Buf, 0, SNAPTOPOINTS_Points1);
    enc->setBuffer(p2Buf ? p2Buf : p1Buf, 0, SNAPTOPOINTS_Points2);
    enc->setBuffer(dstBuf, 0, SNAPTOPOINTS_Result);
    enc->setBytes(&params, sizeof(params), SNAPTOPOINTS_Params);
    const uint32_t tg = 64;
    enc->dispatchThreadgroups(MTL::Size::Make((n + tg - 1) / tg, 1, 1), MTL::Size::Make(tg, 1, 1));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
    out.assign(n, SwPoint{});
    std::memcpy(out.data(), dstBuf->contents(), (size_t)n * sizeof(SwPoint));
    p1Buf->release();
    if (p2Buf) p2Buf->release();
    dstBuf->release();
    return true;
  }
};

// UB LOCUS (file-header AMBIGUITY note): BlendFactor bit-exact 0.0 (denom literally 0, no rounding
// involved). Feeds the Position-only override below. NARROWER than edge0==edge1 on purpose --
// verified this fixer session (single-sample probe): BlendFactor=+1e-40 (absorbed into Distance by
// rounding, ALSO making edge0==edge1 post-rounding) does NOT reliably collapse like literal 0.0 --
// true hardware UB depends on pre-rounding history. Absorbed-but-nonzero falls through below.
bool snapUbLocus(float blendFactor, float /*distanceParam*/) {
  return blendFactor == 0.0f;
}

// Branchy candidacy: near-boundary ULP tail + absorbed-but-nonzero-BlendFactor UB residue (Position
// 0-2), or huge-|BlendFactor| amplification (FX1 lane 3) -- magnitude gated in snapEnvelopeOk below
// (huge-param precedent SnapPointsToGrid pilot #3). Literal BlendFactor==0 returns -1 (class-
// excluded above, an exact match needs no exemption).
float snapBranchDist(const std::vector<float>& P, const float* in, int lane) {
  if (lane == 3) return std::fabs(P[0]) > 1.0f ? 0.0f : -1.0f;
  if (lane < 0 || lane > 2) return -1.0f;
  if (P[0] == 0.0f) return -1.0f;
  const float bf = P[0], dist = P[1], edge0 = bf + dist, edge1 = dist, denom = edge1 - edge0;
  if (denom == 0.0f) return 0.0f;  // absorbed-but-nonzero BlendFactor -- true-UB residue, envelope-gated
  const float dx = in[0] - in[4], dy = in[1] - in[5], dz = in[2] - in[6];
  const float d = std::sqrt(dx * dx + dy * dy + dz * dz), t = (d - edge0) / denom;
  return std::isfinite(t) ? std::min(std::fabs(t), std::fabs(t - 1.0f)) : -1.0f;
}

// Ill-conditioned envelope (SnapPointsToGrid pilot #3 precedent): Position bounded by A<->SnapPoint
// widened by |MaxAmount|; FX1 bounded by A.FX1<->SnapPoint.FX1 widened by |BlendFactor| (:68 lerp).
bool snapEnvelopeOk(const std::vector<float>& P, const float* in, int lane, float gpu, float ref) {
  float a, sp, amt;
  if (lane == 3) { a = in[3]; sp = in[7]; amt = std::fabs(P[0]); }
  else if (lane >= 0 && lane <= 2) { a = in[lane]; sp = in[4 + lane]; amt = std::fmax(std::fabs(P[2]), 1.0f); }
  else return false;
  const float bound = std::fmax(std::fabs(a), std::fabs(sp)) + std::fabs(sp - a) * amt * 1.001f + 1.0f;
  return std::fabs(gpu) <= bound && std::fabs(ref) <= bound;
}

const std::vector<ParamDomain>& paramTable() {
  static const std::vector<ParamDomain> t = {
      {"BlendFactor", -4.0f, 4.0f, ParamDomain::Linear,
       "SnapToPoints.t3:9-11 BlendValue Default=0.0, no Min/Max -> §3.2 Default±4 fallback"},
      {"Distance", -4.0f, 4.0f, ParamDomain::Linear,
       "SnapToPoints.t3:12-15 Distance Default=0.0, no Min/Max -> Default±4 fallback"},
      {"MaxAmount", -4.0f, 4.0f, ParamDomain::Linear,
       "SnapToPoints.t3:16-19 MaxAmount Default=0.0, no Min/Max -> Default±4 fallback"},
  };
  return t;
}

// TOOTH IDENTITY: BlendFactor=0 (raw W-lerp t=0) + MaxAmount=0 (localBlendFactor forced 0) ->
// Position_out==A.Position, FX1_out==A.FX1 for ANY random A/SnapPoint pair.
bool checkIdentityTooth(const SnapDispatch& disp, float inLo, float inHi) {
  Comparator cmp("mathv-snaptopoints-identity", EpsSpec::exact(), 5);
  Rng rng(mathv::mathvSeed("snaptopoints-identity"));
  const size_t N = 512;
  std::vector<SwPoint> pts1(N), pts2(N), dst;
  for (size_t i = 0; i < N; ++i) {
    pts1[i].Position = {rng.uniform(inLo, inHi), rng.uniform(inLo, inHi), rng.uniform(inLo, inHi)};
    pts1[i].FX1 = rng.uniform(inLo, inHi);
    pts2[i].Position = {rng.uniform(inLo, inHi), rng.uniform(inLo, inHi), rng.uniform(inLo, inHi)};
    pts2[i].FX1 = rng.uniform(inLo, inHi);
  }
  const std::vector<float> P = {0.0f, 1.0f, 0.0f};
  if (!disp.dispatch(P, pts1, pts2, dst) || dst.size() != N) { printf("[mathv-snaptopoints-identity] dispatch FAILED\n"); return false; }
  for (size_t i = 0; i < N; ++i) {
    float ev[4] = {pts1[i].Position.x, pts1[i].Position.y, pts1[i].Position.z, pts1[i].FX1};
    const float gv[4] = {dst[i].Position.x, dst[i].Position.y, dst[i].Position.z, dst[i].FX1};
    for (int k = 0; k < 4; ++k) cmp.add(gv[k], ev[k], ev, 4, k, -1.0f, "identity");
  }
  cmp.print();
  return cmp.verdict();
}

// TOOTH BOUNDARY: smoothstep saturate band + this kernel's division risk (denom==-BlendFactor).
// branchy (file-header AMBIGUITY): probe (0,3,4) lands on the UB locus (snapUbLocus); (1e-6,5,5)
// lands on the ULP tail (snapBranchDist/snapEnvelopeOk).
bool checkBoundaryTooth(const SnapDispatch& disp) {
  EpsSpec eps = EpsSpec::branchy();
  eps.exemptMax = 0.05f;  // measured: N=54 small-sample tooth, 1 exempt sample alone is 1/54=1.85%
                          // > the primary case's default 1% cap (172032 samples, ample margin there).
  Comparator cmp("mathv-snaptopoints-boundary", eps, 8);
  bool dispatchOk = true;
  auto probe = [&](float bf, float dist, float amt, float actualDist, const char* batch) {
    mathv_ref::SnapToPointsParams prm{bf, dist, amt};
    SwPoint a{}, sp{}, refOut{};
    a.Position = {0.0f, 0.0f, 0.0f}; a.FX1 = 10.0f;
    sp.Position = {actualDist, 0.0f, 0.0f}; sp.FX1 = 20.0f;
    mathv_ref::snapToPointsOne(a, sp, refOut, prm);
    std::vector<SwPoint> p1{a}, p2{sp}, gdst;
    if (!disp.dispatch({bf, dist, amt}, p1, p2, gdst) || gdst.size() != 1) { dispatchOk = false; return; }
    float ev[3] = {bf, dist, actualDist};
    const std::vector<float> P = {bf, dist, amt};
    const float in8[8] = {a.Position.x,  a.Position.y,  a.Position.z,  a.FX1,
                           sp.Position.x, sp.Position.y, sp.Position.z, sp.FX1};
    // UB LOCUS class-exclusion (snapUbLocus, budget-free -- see AMBIGUITY note above).
    float refPosX = snapUbLocus(bf, dist) ? a.Position.x : refOut.Position.x;
    const float bd = snapBranchDist(P, in8, 0);
    auto envOk = [&](float g, float r) { return snapEnvelopeOk(P, in8, 0, g, r); };
    cmp.add(gdst[0].Position.x, refPosX, ev, 3, 0, bd, batch, envOk);
    cmp.add(gdst[0].FX1, refOut.FX1, ev, 3, 1, -1.0f, batch);  // FX1: normal assertion, no UB
  };
  // (a) saturate-transition band: BlendFactor=2,Distance=3 -> edge0=5,edge1=3, fine fractional sweep.
  for (float f : {0.0f, 0.01f, 0.05f, 0.1f, 0.25f, 0.5f, 0.75f, 0.9f, 0.95f, 0.99f, 1.0f}) probe(2.0f, 3.0f, 1.0f, 3.0f + f * (5.0f - 3.0f), "band-smoothstep");
  // (b) near-zero BlendFactor at fixed distance(4)!=Distance(3): denominator division sensitivity.
  for (float b : {0.0f, 1e-6f, -1e-6f, 1e-4f, -1e-4f, 1e-2f, -1e-2f, 0.1f, -0.1f}) probe(b, 3.0f, 1.0f, 4.0f, "near-zero-blendfactor");
  // (c) distance==Distance: numerator ALSO ->0 at BlendFactor==0 (0/0->NaN->hlslSaturate forces 0).
  for (float b : {0.0f, 1e-6f, -1e-6f, 1e-4f, -1e-4f, 1e-2f, 1.0f}) probe(b, 5.0f, 1.0f, 5.0f, "distance-equals-Distance-slice");
  cmp.print();
  return dispatchOk && cmp.verdict();
}

// TOOTH UNTOUCHED: Rotation/Color/Scale/FX2 passthrough on the NORMAL math path.
bool checkUntouchedFieldsTooth(const SnapDispatch& disp) {
  Comparator cmp("mathv-snaptopoints-untouched", EpsSpec::exact(), 8);
  const size_t N = 16;
  Rng rng(mathv::mathvSeed("snaptopoints-untouched"));
  std::vector<SwPoint> pts1(N), pts2(N), dst;
  for (size_t i = 0; i < N; ++i) {
    pts1[i].Position = {rng.uniform(-4.0f, 4.0f), rng.uniform(-4.0f, 4.0f), rng.uniform(-4.0f, 4.0f)};
    pts1[i].FX1 = rng.uniform(-4.0f, 4.0f);
    pts1[i].Rotation = {9.0f + (float)i, 8.0f, 7.0f, 6.0f};  // sentinels matching NEITHER A nor Snap
    pts1[i].Color = {5.0f, 4.0f + (float)i, 3.0f, 2.0f}; pts1[i].Scale = {1.5f, 2.5f, 3.5f + (float)i};
    pts1[i].FX2 = 42.0f + (float)i;
    pts2[i].Position = {rng.uniform(-4.0f, 4.0f), rng.uniform(-4.0f, 4.0f), rng.uniform(-4.0f, 4.0f)};
    pts2[i].FX1 = rng.uniform(-4.0f, 4.0f);
  }
  if (!disp.dispatch({0.5f, 1.0f, 1.0f}, pts1, pts2, dst) || dst.size() != N) {
    printf("[mathv-snaptopoints-untouched] dispatch FAILED\n"); return false;
  }
  for (size_t i = 0; i < N; ++i) {
    float ev[1] = {(float)i};
    const SwPoint& g = dst[i]; const SwPoint& a = pts1[i];
    const float gv[12] = {g.Rotation.x, g.Rotation.y, g.Rotation.z, g.Rotation.w, g.Color.x, g.Color.y, g.Color.z, g.Color.w, g.Scale.x, g.Scale.y, g.Scale.z, g.FX2};
    const float rv[12] = {a.Rotation.x, a.Rotation.y, a.Rotation.z, a.Rotation.w, a.Color.x, a.Color.y, a.Color.z, a.Color.w, a.Scale.x, a.Scale.y, a.Scale.z, a.FX2};
    for (int k = 0; k < 12; ++k) cmp.add(gv[k], rv[k], ev, 1, k, -1.0f, "untouched");
  }
  cmp.print();
  return cmp.verdict();
}

// TOOTH COUNTGUARD: NAMED FORK pin, pinned against the shader's OWN documented clamp formula.
bool checkCountGuardTooth(const SnapDispatch& disp) {
  Comparator cmp("mathv-snaptopoints-countguard", EpsSpec::exact(), 8);
  bool dispatchOk = true;
  const size_t N = 8;
  Rng rng(mathv::mathvSeed("snaptopoints-countguard"));
  std::vector<SwPoint> pts1(N);
  for (size_t i = 0; i < N; ++i) {
    pts1[i].Position = {rng.uniform(-4.0f, 4.0f), rng.uniform(-4.0f, 4.0f), rng.uniform(-4.0f, 4.0f)};
    pts1[i].FX1 = rng.uniform(-4.0f, 4.0f);
    pts1[i].Rotation = {1.0f, 2.0f, 3.0f, 4.0f};
    pts1[i].Color = {5.0f, 6.0f, 7.0f, 8.0f}; pts1[i].Scale = {9.0f, 10.0f, 11.0f}; pts1[i].FX2 = 12.0f;
  }
  // sub-case A: Points2Count==0 -> Result[i]==Points1[i] EXACTLY, ALL fields.
  {
    std::vector<SwPoint> pts2Empty, dst;
    if (!disp.dispatch({1.0f, 1.0f, 1.0f}, pts1, pts2Empty, dst) || dst.size() != N) { dispatchOk = false; } else {
      for (size_t i = 0; i < N; ++i) {
        float ev[1] = {(float)i};
        const float gv[5] = {dst[i].Position.x, dst[i].Position.y, dst[i].Position.z, dst[i].FX1, dst[i].FX2};
        const float rv[5] = {pts1[i].Position.x, pts1[i].Position.y, pts1[i].Position.z, pts1[i].FX1, pts1[i].FX2};
        for (int k = 0; k < 5; ++k) cmp.add(gv[k], rv[k], ev, 1, k, -1.0f, "countguard-empty");
      }
    }
  }
  // sub-case B: 0 < Points2Count < Count -> i>=Points2Count clamps to Points2[Points2Count-1].
  {
    const size_t N2 = 3;
    std::vector<SwPoint> pts2(N2);
    for (size_t i = 0; i < N2; ++i) {
      pts2[i].Position = {rng.uniform(-4.0f, 4.0f), rng.uniform(-4.0f, 4.0f), rng.uniform(-4.0f, 4.0f)};
      pts2[i].FX1 = rng.uniform(-4.0f, 4.0f);
    }
    const std::vector<float> P = {1.5f, 2.0f, 1.0f};
    mathv_ref::SnapToPointsParams prm{P[0], P[1], P[2]}; std::vector<SwPoint> dst;
    if (!disp.dispatch(P, pts1, pts2, dst) || dst.size() != N) { dispatchOk = false; } else {
      for (size_t i = 0; i < N; ++i) {
        const SwPoint& snapSrc = pts2[(i < N2) ? i : (N2 - 1)];  // the fork's OWN clamp formula
        SwPoint refOut{};
        mathv_ref::snapToPointsOne(pts1[i], snapSrc, refOut, prm);
        float ev[1] = {(float)i};
        const float gv[4] = {dst[i].Position.x, dst[i].Position.y, dst[i].Position.z, dst[i].FX1};
        const float rv[4] = {refOut.Position.x, refOut.Position.y, refOut.Position.z, refOut.FX1};
        for (int k = 0; k < 4; ++k) cmp.add(gv[k], rv[k], ev, 1, k, -1.0f, "countguard-clamp");
      }
    }
  }
  cmp.print();
  return dispatchOk && cmp.verdict();
}

// TOOTH WQUIRK: oracle self-check case 2 replayed against the REAL GPU (PINNED PARITY).
bool checkWQuirkProbe(const SnapDispatch& disp) {
  mathv_ref::SnapToPointsParams prm{2.0f, 3.0f, 1.0f};
  SwPoint a{}, sp{}, refOut{};
  a.Position = {0.0f, 0.0f, 0.0f}; a.FX1 = 10.0f;
  sp.Position = {4.0f, 0.0f, 0.0f}; sp.FX1 = 20.0f;
  mathv_ref::snapToPointsOne(a, sp, refOut, prm);
  std::vector<SwPoint> p1{a}, p2{sp}, dst;
  if (!disp.dispatch({2.0f, 3.0f, 1.0f}, p1, p2, dst) || dst.size() != 1) {
    printf("[mathv-snaptopoints-wquirk] dispatch FAILED\n"); return false;
  }
  printf("[mathv-snaptopoints-wquirk] BlendFactor=2 (extrapolating W): gpu Position=(%.6g,%.6g,%.6g) "
         "FX1=%.6g | ref Position=(%.6g,%.6g,%.6g) FX1=%.6g (both sides extrapolate past FX1=20)\n",
         dst[0].Position.x, dst[0].Position.y, dst[0].Position.z, dst[0].FX1, refOut.Position.x,
         refOut.Position.y, refOut.Position.z, refOut.FX1);
  Comparator cmp("mathv-snaptopoints-wquirk", EpsSpec::exact(), 5);
  float ev[2] = {a.FX1, sp.FX1};
  cmp.add(dst[0].Position.x, refOut.Position.x, ev, 2, 0, -1.0f, "wquirk");
  cmp.add(dst[0].FX1, refOut.FX1, ev, 2, 1, -1.0f, "wquirk");
  cmp.print();
  return cmp.verdict();
}

}  // namespace

int runMathvSnapToPointsSelfTest(bool injectBug) {
  ParityHarness h;
  if (!h.ok()) { printf("[selftest-mathv-snaptopoints] FAIL: no metallib\n"); return 1; }
  SnapDispatch disp(h.dev, h.queue, h.lib);
  if (!disp.ok) { printf("[selftest-mathv-snaptopoints] FAIL: no snaptopoints kernel\n"); return 1; }

  MathvCase c;
  c.opName = "snaptopoints";
  c.params = paramTable();
  c.inDim = 8;   // [A.x,A.y,A.z,A.FX1, Snap.x,Snap.y,Snap.z,Snap.FX1]
  c.outDim = 4;  // [Position.x,y,z, FX1]
  // measured: XS band sweep 2026-07-10 -- see file-header AMBIGUITY note + snapUbLocus/snapBranchDist.
  c.eps = EpsSpec::branchy();
  c.inputLo = -4.0f; c.inputHi = 4.0f;
  c.noIdentityReason = "inDim(8)!=outDim(4) -- generic identity layer requires equal dims; "
      "full-domain identity verified in checkIdentityTooth below.";
  c.gpu = [&disp](const std::vector<float>& P, const std::vector<float>& in, std::vector<float>& out) {
    const size_t n = in.size() / 8;
    std::vector<SwPoint> pts1(n), pts2(n), dst;
    for (size_t i = 0; i < n; ++i) {
      const float* e = &in[i * 8];
      pts1[i].Position = {e[0], e[1], e[2]};
      pts1[i].FX1 = e[3];
      pts2[i].Position = {e[4], e[5], e[6]};
      pts2[i].FX1 = e[7];
    }
    if (!disp.dispatch(P, pts1, pts2, dst) || dst.size() != n) return false;
    out.resize(n * 4);
    for (size_t i = 0; i < n; ++i) {
      out[i * 4 + 0] = dst[i].Position.x; out[i * 4 + 1] = dst[i].Position.y;
      out[i * 4 + 2] = dst[i].Position.z; out[i * 4 + 3] = dst[i].FX1;
    }
    return true;
  };
  c.ref = [](const std::vector<float>& P, const float* in, float* out) {
    mathv_ref::SnapToPointsParams prm{P[0], P[1], P[2]};
    SwPoint a{}, sp{}, o{};
    a.Position = {in[0], in[1], in[2]}; a.FX1 = in[3];
    sp.Position = {in[4], in[5], in[6]}; sp.FX1 = in[7];
    mathv_ref::snapToPointsOne(a, sp, o, prm);
    // UB LOCUS class-exclusion (snapUbLocus, AMBIGUITY note above): pin Position to the MEASURED
    // GPU convention -- exact match, no exempt budget touched.
    if (snapUbLocus(P[0], P[1])) {
      out[0] = a.Position.x; out[1] = a.Position.y; out[2] = a.Position.z;
    } else {
      out[0] = o.Position.x; out[1] = o.Position.y; out[2] = o.Position.z;
    }
    out[3] = o.FX1;
  };
  c.branchDist = snapBranchDist;
  c.illConditionedEnvelope = snapEnvelopeOk;

  bool passPrimary = mathv::runMathvFuzz(c, injectBug);
  if (injectBug) return mathv::mathvVerdictToExit(passPrimary, true, "snaptopoints");

  bool passIdentity = checkIdentityTooth(disp, c.inputLo, c.inputHi);
  bool passBoundary = checkBoundaryTooth(disp);
  bool passUntouched = checkUntouchedFieldsTooth(disp);
  bool passCountGuard = checkCountGuardTooth(disp);
  bool passWQuirk = checkWQuirkProbe(disp);

  ParityReport rep("selftest-mathv-snaptopoints");
  const std::pair<const char*, bool> checks[] = {
      {"primary(3-layer fuzz Position+FX1, branchy: UB class-exclusion + ULP exempt)", passPrimary},
      {"identity(full-domain BlendFactor=0/MaxAmount=0 tooth)", passIdentity},
      {"boundary(smoothstep band + near-zero-BlendFactor division sensitivity, branchy)", passBoundary},
      {"untouched(Rotation/Color/Scale/FX2 passthrough contract)", passUntouched},
      {"countguard(NAMED FORK pin: Points2Count==0 + clamp-to-last)", passCountGuard},
      {"wquirk(BlendFactor=2 W-lerp extrapolation, pinned parity)", passWQuirk},
  };
  for (const auto& ck : checks) rep.expectTrue(ck.first, ck.second, ck.second ? 1.0 : 0.0);
  return rep.finish();
}

// order 1004: appends right after mathv-snappointstogrid (1003).
REGISTER_SELFTESTS(/*orderBase=*/1004, {"mathv-snaptopoints", runMathvSnapToPointsSelfTest});

}  // namespace sw
