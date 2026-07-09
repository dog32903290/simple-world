// selftests_mathv_addnoise.cpp — --selftest-mathv-addnoise (D role, fuzz driver).
// MATH_VERIFY_WORKFLOW.md 工單2 pilot: fuzz the GPU "addnoise" kernel (app/shaders/addnoise.metal)
// against the R-authored CPU oracle (app/src/mathv_ref_addnoise.h, TRANSCRIBED from external/tixl
// HLSL) via the shared mathv harness (direct-kernel dispatch, §1.3). AddNoise is the §2
// TRANSCENDENTAL pilot (sin/normalize/rsqrt): EpsSpec::transcendental() everywhere below.
// ── ParamDomain provenance (external/tixl SHA 395c4c55, AddNoise.t3/.t3ui) ─────────────────────
//   Strength(Amount) t3ui:31-40 Min=0.0 Max=2.0 -> authored range verbatim. RotationLookupDistance
//     t3ui:58-68 Min=0.001 ClampMin=true, Max unauthored -> lo=authored Min (not Default-4:
//     ClampMin forbids negative), hi=Default(0.25)+4 (§3.2 fallback). Frequency/Phase/
//     AmountDistribution/NoiseOffset: no Min/Max -> §3.2 Default±4 fallback (t3:25-27 Freq=1.0,
//     t3:36-39 Phase=0.0, t3:41-47 AmountDist=(1,1,1), t3:9-14 NoiseOffset=0). Variation (t3:5-7
//     Default=0.0, ±4) and StrengthFactor (t3:28-31 Default=0, .cs:11-35 FModes
//     enum{None=0,F1=1,F2=2}) are NOT in the 10-param PRIMARY table — see SCOPE below.
// ── SCOPE split across FIVE teeth ───────────────────────────────────────────────────────────────
// The generic 3-layer harness (mathv_harness.h) feeds `c.ref` (params, one-element-in) with NO
// per-element index — it cannot replicate AddNoise.hlsl:119's per-THREAD `hash41u(idx).xyz *
// Variation` (idx==buffer position==GPU thread id), nor carry a 2nd input channel (Rotation, FX1,
// FX2) alongside Position while keeping inDim==outDim for the identity layer. Same discipline as
// pilot #1 (WrapPointPosition's FX1/NaN-axis "beyond the generic harness" teeth): every axis the
// generic case can't structurally carry gets its own EQUAL-STRENGTH direct-dispatch tooth.
//   PRIMARY (generic 3-layer) — Position(3). Rotation input PINNED identity, StrengthMode PINNED 0
//     (weight=1), Variation PINNED 0 (hash41u(idx)*0==0 regardless of idx -> the harness's
//     idx-less `ref` lambda is faithful here; the ONLY injectBug tooth).
//   TOOTH ROTATION — direct dispatch, Rotation(4), random UNIT quaternion; RotationLookupDistance
//     now load-bearing (a no-op for Position).
//   TOOTH VARIATION/IDX — direct dispatch, N=257 spans 5 threadgroups (one partial); validates
//     hash41u(idx) wiring end to end.
//   TOOTH STRENGTHMODE — StrengthFactor {0,1,2} (§3.3 exhaustive) + {-1,5} (ref's documented
//     fallthrough-to-FX2 bonus), FX1/FX2 varied per point.
//   PROBE NAN-TRAP (discovery, §7, NOT a P1 gate) — RotationLookupDistance=0 (ref's AMBIGUITY /
//     self-check case 2: normalize(0,0,0)->NaN slips past the degeneracy guard, NaN comparisons
//     always false). Unmeasured until run; CLASS agreement recorded as-is.
// Position/Rotation are the only two channels the kernel writes (Color/Scale pass through
// unchanged) — no third channel to cover. ZONE: shell tier; crosses runtime only for SwPoint +
// the params ABI header.
#include "mathv_harness.h"
#include "mathv_ref_addnoise.h"
#include "runtime/addnoise_params.h"
#include "runtime/selftest_registry.h"
#include "runtime/tixl_point.h"

#include <cmath>
#include <cstdio>
#include <cstring>
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

using HostParams = ::AddNoiseParams;  // host/MSL ABI struct (addnoise_params.h, global namespace)

// direct-kernel dispatch adapter (turbulence_parity_golden.cpp:75-112 shape): fill a SwPoint bag,
// setBytes the 64B AddNoiseParams, dispatch "addnoise", readback. `prm.Count` is overwritten with
// `in.size()` (callers need not set it).
struct AddNoiseDispatch {
  MTL::Device* dev;
  MTL::CommandQueue* queue;
  MTL::ComputePipelineState* pso = nullptr;
  bool ok = false;
  AddNoiseDispatch(MTL::Device* d, MTL::CommandQueue* q, MTL::Library* lib) : dev(d), queue(q) {
    MTL::Function* fn = lib->newFunction(NS::String::string("addnoise", NS::UTF8StringEncoding));
    if (!fn) return;
    NS::Error* err = nullptr;
    pso = dev->newComputePipelineState(fn, &err);
    fn->release();
    ok = pso != nullptr;
  }
  ~AddNoiseDispatch() { if (pso) pso->release(); }
  AddNoiseDispatch(const AddNoiseDispatch&) = delete;

  bool dispatch(HostParams prm, const std::vector<SwPoint>& in, std::vector<SwPoint>& out) const {
    if (!ok) return false;
    const uint32_t n = (uint32_t)in.size();
    out.clear();
    if (n == 0) return true;
    prm.Count = n;
    MTL::Buffer* srcBuf = dev->newBuffer(in.data(), (NS::UInteger)(n * sizeof(SwPoint)),
                                        MTL::ResourceStorageModeShared);
    MTL::Buffer* dstBuf =
        dev->newBuffer((NS::UInteger)(n * sizeof(SwPoint)), MTL::ResourceStorageModeShared);
    MTL::CommandBuffer* cmd = queue->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pso);
    enc->setBuffer(srcBuf, 0, ADDNOISE_SourcePoints);
    enc->setBuffer(dstBuf, 0, ADDNOISE_ResultPoints);
    enc->setBytes(&prm, sizeof(prm), ADDNOISE_Params);
    const uint32_t tg = 64;
    enc->dispatchThreadgroups(MTL::Size::Make((n + tg - 1) / tg, 1, 1), MTL::Size::Make(tg, 1, 1));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
    out.assign(n, SwPoint{});
    std::memcpy(out.data(), dstBuf->contents(), (size_t)n * sizeof(SwPoint));
    srcBuf->release(); dstBuf->release();
    return true;
  }
};

// 10-param table for the PRIMARY (Position-only) generic-harness case. P[0]=Amount is the one
// §1.5's injectBug perturbs (+1e-2 gpu-side) — Amount scales GetNoise's output CONTINUOUSLY
// (never a discrete-branch-only param), matching §1.5's rule.
const std::vector<ParamDomain>& paramTable() {
  static const std::vector<ParamDomain> t = {
      {"Amount", 0.0f, 2.0f, ParamDomain::Linear, "AddNoise.t3ui:31-40 Strength Min=0.0 Max=2.0"},
      {"Frequency", -3.0f, 5.0f, ParamDomain::Linear,
       "AddNoise.t3:25-27 Default=1.0, no Min/Max -> Default±4 fallback"},
      {"Phase", -4.0f, 4.0f, ParamDomain::Linear,
       "AddNoise.t3:36-39 Default=0.0, no Min/Max -> Default±4 fallback"},
      {"AmountDistributionX", -3.0f, 5.0f, ParamDomain::Linear,
       "AddNoise.t3:41-47 Default=(1,1,1), no Min/Max -> Default±4 fallback"},
      {"AmountDistributionY", -3.0f, 5.0f, ParamDomain::Linear, "ditto"},
      {"AmountDistributionZ", -3.0f, 5.0f, ParamDomain::Linear, "ditto"},
      {"RotationLookupDistance", 0.001f, 4.25f, ParamDomain::Linear,
       "AddNoise.t3ui:58-68 Min=0.001 ClampMin=true, Max unauthored -> lo=Min, hi=Default(0.25)+4"},
      {"NoiseOffsetX", -4.0f, 4.0f, ParamDomain::Linear,
       "AddNoise.t3:9-14 Default=(0,0,0), no Min/Max -> Default±4 fallback"},
      {"NoiseOffsetY", -4.0f, 4.0f, ParamDomain::Linear, "ditto"},
      {"NoiseOffsetZ", -4.0f, 4.0f, ParamDomain::Linear, "ditto"},
  };
  return t;
}

// Builds BOTH the host (GPU ABI) and ref (CPU oracle) param structs from one call site so they
// can never silently drift out of field-for-field sync (§0: ABI names/offsets are the only legal
// shared artifact between adapter and ref).
struct DualParams { HostParams h{}; mathv_ref::AddNoiseParams r{}; };
DualParams makeParams(float amount, float freq, float phase, float variation, float adX,
                       float adY, float adZ, float rld, float noX, float noY, float noZ,
                       int32_t strengthMode) {
  DualParams d;
  d.h.StrengthMode = strengthMode; d.h.Amount = amount; d.h.Frequency = freq; d.h.Phase = phase;
  d.h.Variation = variation; d.h.RotationLookupDistance = rld;
  d.h.AmountDistributionX = adX; d.h.AmountDistributionY = adY; d.h.AmountDistributionZ = adZ;
  d.h.NoiseOffsetX = noX; d.h.NoiseOffsetY = noY; d.h.NoiseOffsetZ = noZ;
  d.r.strengthMode = strengthMode; d.r.amount = amount; d.r.frequency = freq; d.r.phase = phase;
  d.r.variation = variation; d.r.rotationLookupDistance = rld;
  d.r.amountDistX = adX; d.r.amountDistY = adY; d.r.amountDistZ = adZ;
  d.r.noiseOffsetX = noX; d.r.noiseOffsetY = noY; d.r.noiseOffsetZ = noZ;
  return d;
}
// PRIMARY case wiring: P[0..9] = {Amount,Frequency,Phase,AmountDist.xyz,RotationLookupDistance,
// NoiseOffset.xyz}; StrengthMode/Variation pinned 0 (see SCOPE note).
DualParams paramsFromPrimary(const std::vector<float>& P) {
  return makeParams(P[0], P[1], P[2], /*variation=*/0.0f, P[3], P[4], P[5], P[6], P[7], P[8], P[9],
                     /*strengthMode=*/0);
}

// Random UNIT quaternion (axis-angle) — qRotateVec3 (ref+kernel) is only a valid rotation for
// |q|==1 inputs.
struct QuatF { float x, y, z, w; };
QuatF randomUnitQuat(Rng& rng) {
  float ax, ay, az, len2;
  do {
    ax = rng.uniform(-1.0f, 1.0f); ay = rng.uniform(-1.0f, 1.0f); az = rng.uniform(-1.0f, 1.0f);
    len2 = ax * ax + ay * ay + az * az;
  } while (len2 < 1e-4f);
  float invLen = 1.0f / std::sqrt(len2);
  ax *= invLen; ay *= invLen; az *= invLen;
  float theta = rng.uniform(0.0f, 6.2831853f);
  float s = std::sin(theta * 0.5f), c = std::cos(theta * 0.5f);
  return {ax * s, ay * s, az * s, c};
}
// Random Position in the shared [-4,4]^3 input domain (used by every direct-dispatch tooth below).
SW_PACKED3 randomPos3(Rng& rng) {
  return SW_PACKED3{rng.uniform(-4.0f, 4.0f), rng.uniform(-4.0f, 4.0f), rng.uniform(-4.0f, 4.0f)};
}

// Shared direct-dispatch compare core for the three "beyond the generic harness" teeth: dispatch
// `src` once, compare Position(3)+Rotation(4)=7 lanes/element against mathv_ref::addNoiseOne
// called with the MATCHING idx (==buffer position==GPU thread id, addnoise.metal:105 `i.x`) and
// count==src.size() (addnoise.metal:106 guard). False on dispatch failure (visible, not skipped).
bool compareBatch(const AddNoiseDispatch& disp, Comparator& cmp, const HostParams& hprm,
                   const mathv_ref::AddNoiseParams& rprm, const std::vector<SwPoint>& src,
                   const char* batchTag) {
  std::vector<SwPoint> dst;
  const uint32_t n = (uint32_t)src.size();
  if (!disp.dispatch(hprm, src, dst) || dst.size() != n) return false;
  for (uint32_t i = 0; i < n; ++i) {
    SwPoint refOut{};
    mathv_ref::addNoiseOne(src[i], refOut, i, n, rprm);
    float in7[7] = {src[i].Position.x, src[i].Position.y, src[i].Position.z,
                    src[i].Rotation.x, src[i].Rotation.y, src[i].Rotation.z, src[i].Rotation.w};
    float g[7] = {dst[i].Position.x, dst[i].Position.y, dst[i].Position.z,
                  dst[i].Rotation.x, dst[i].Rotation.y, dst[i].Rotation.z, dst[i].Rotation.w};
    float r[7] = {refOut.Position.x, refOut.Position.y, refOut.Position.z,
                  refOut.Rotation.x, refOut.Rotation.y, refOut.Rotation.z, refOut.Rotation.w};
    for (int k = 0; k < 7; ++k) cmp.add(g[k], r[k], in7, 7, k, -1.0f, batchTag);
  }
  return true;
}

// ── TOOTH ROTATION: random Position + random UNIT-quaternion Rotation, RotationLookupDistance
// now load-bearing. StrengthMode/Variation still pinned 0 (own teeth). NOTE on identity:
// qFromMatrix3Precise recovers a quaternion up to a GLOBAL SIGN (q vs -q, same rotation matrix;
// Shepperd's method picks the branch's canonical sign from the matrix trace/diagonal, independent
// of the INPUT quaternion's sign) — so "Rotation_out==Rotation_in" is NOT a valid identity claim
// for ARBITRARY random rotations. It IS valid, hand-verified, at ref's self-check case 1
// (Rotation_in==(0,0,0,1) canonical identity, no sign flip possible) — checked GPU-side below too.
bool checkRotationTooth(const AddNoiseDispatch& disp) {
  Comparator cmp("mathv-addnoise-rotation", EpsSpec::transcendental(), 5);
  Rng rng(mathv::mathvSeed("addnoise-rotation"));
  const auto& dom = paramTable();
  const size_t N = 256;
  bool dispatchOk = true;
  for (int v = 0; v < 8; ++v) {
    float P[10];
    for (int k = 0; k < 10; ++k) P[k] = mathv::sampleUniform(rng, dom[k]);
    DualParams dp = makeParams(P[0], P[1], P[2], /*variation=*/0.0f, P[3], P[4], P[5], P[6], P[7],
                                P[8], P[9], /*strengthMode=*/0);
    std::vector<SwPoint> src(N);
    for (size_t i = 0; i < N; ++i) {
      src[i].Position = randomPos3(rng);
      QuatF q = randomUnitQuat(rng);
      src[i].Rotation = SW_FLOAT4{q.x, q.y, q.z, q.w};
      src[i].FX1 = 0.0f; src[i].FX2 = 0.0f;
    }
    if (!compareBatch(disp, cmp, dp.h, dp.r, src, "rotation-random")) dispatchOk = false;
  }
  // Hand-derived identity case (ref self-check case 1, mirrored GPU-side): Amount=0,
  // RotationLookupDistance=5, Rotation_in=(0,0,0,1) -> Rotation_out must be exactly (0,0,0,1) too
  // (see the sign-ambiguity note above for why this ONE point is legitimate).
  DualParams dp1 = makeParams(0.0f, 1.3f, 0.7f, 0.0f, 1.0f, 1.0f, 1.0f, 5.0f, 0.0f, 0.0f, 0.0f, 0);
  std::vector<SwPoint> src1(1), dst1;
  src1[0].Position = SW_PACKED3{2.0f, 3.0f, -1.0f};
  src1[0].Rotation = SW_FLOAT4{0.0f, 0.0f, 0.0f, 1.0f};
  bool case1Ok = disp.dispatch(dp1.h, src1, dst1) && dst1.size() == 1 &&
                 std::fabs(dst1[0].Rotation.x) < 1e-4f && std::fabs(dst1[0].Rotation.y) < 1e-4f &&
                 std::fabs(dst1[0].Rotation.z) < 1e-4f && std::fabs(dst1[0].Rotation.w - 1.0f) < 1e-4f;
  printf("[mathv-addnoise-rotation] case1(identity in -> identity out) -> %s\n",
         case1Ok ? "ok" : "RED");
  cmp.print();
  return dispatchOk && cmp.verdict() && case1Ok;
}

// ── TOOTH VARIATION/IDX: N=257 spans 5 threadgroups of 64 (incl. one partial group, idx 256 valid
// / 257..319 guarded off by addnoise.metal:106) — exercises hash41u(idx) across a real multi-group
// dispatch, which the generic harness's idx-less `ref` lambda cannot. compareBatch's per-i ref
// call already threads idx==buffer-position==GPU-thread-id, so this tooth's job is just: build a
// Variation!=0 buffer and let compareBatch prove the idx chain end to end.
bool checkVariationIdxTooth(const AddNoiseDispatch& disp) {
  Comparator cmp("mathv-addnoise-variation-idx", EpsSpec::transcendental(), 5);
  Rng rng(mathv::mathvSeed("addnoise-variation-idx"));
  const size_t N = 257;
  DualParams dp = makeParams(/*amount=*/1.0f, /*freq=*/1.7f, /*phase=*/0.3f, /*variation=*/2.5f,
                              1.0f, 1.0f, 1.0f, 0.25f, 0.0f, 0.0f, 0.0f, /*strengthMode=*/0);
  std::vector<SwPoint> src(N);
  for (size_t i = 0; i < N; ++i) {
    src[i].Position = randomPos3(rng);
    src[i].Rotation = SW_FLOAT4{0.0f, 0.0f, 0.0f, 1.0f};
    src[i].FX1 = 0.0f; src[i].FX2 = 0.0f;
  }
  bool dispatchOk = compareBatch(disp, cmp, dp.h, dp.r, src, "variation-idx");
  cmp.print();
  return dispatchOk && cmp.verdict();
}

// ── TOOTH STRENGTHMODE: {0,1,2} = §3.3 exhaustive enum sweep (AddNoise.cs FModes); {-1,5} = ref's
// documented "not clamped, falls through to the FX2 arm exactly like modes>=2" fidelity bonus
// (mathv_ref_addnoise.h :276-277). FX1/FX2 vary per point so weight selection is actually live.
bool checkStrengthModeTooth(const AddNoiseDispatch& disp) {
  Comparator cmp("mathv-addnoise-strengthmode", EpsSpec::transcendental(), 5);
  Rng rng(mathv::mathvSeed("addnoise-strengthmode"));
  const size_t N = 128;
  const int modes[] = {0, 1, 2, -1, 5};
  bool dispatchOk = true;
  for (int mode : modes) {
    DualParams dp = makeParams(1.0f, 1.4f, 0.2f, /*variation=*/0.0f, 1.0f, 1.0f, 1.0f, 0.4f, 0.0f,
                                0.0f, 0.0f, mode);
    std::vector<SwPoint> src(N);
    for (size_t i = 0; i < N; ++i) {
      src[i].Position = randomPos3(rng);
      src[i].Rotation = SW_FLOAT4{0.0f, 0.0f, 0.0f, 1.0f};
      src[i].FX1 = rng.uniform(-2.0f, 2.0f); src[i].FX2 = rng.uniform(-2.0f, 2.0f);
    }
    char tag[32];
    std::snprintf(tag, sizeof tag, "strengthmode-%d", mode);
    if (!compareBatch(disp, cmp, dp.h, dp.r, src, tag)) dispatchOk = false;
  }
  cmp.print();
  return dispatchOk && cmp.verdict();
}

// ── PROBE NAN-TRAP (discovery — §7 route, not a P1 gate): RotationLookupDistance=0, Amount=0,
// same point as ref self-check case 2. ref is HAND-PROVEN all-NaN Rotation (AMBIGUITY note:
// normalize(0,0,0)->NaN slips past `all(abs(ez)<1e-6)`, NaN comparisons always false). Whether
// Metal fast-math preserves that is unmeasured until run. We record the observed CLASS match
// as-is; a disagreement is a fast-math/HLSL-ambiguity finding for S/X (§7), not papered over here.
bool checkNanTrapProbe(const AddNoiseDispatch& disp) {
  DualParams dp = makeParams(0.0f, 1.3f, 0.7f, 0.4f, 1.0f, 1.0f, 1.0f, /*rld=*/0.0f, 0.0f, 0.0f,
                              0.0f, /*strengthMode=*/0);
  SwPoint in{}, refOut{};
  in.Position = SW_PACKED3{2.0f, 3.0f, -1.0f};
  in.Rotation = SW_FLOAT4{0.0f, 0.0f, 0.0f, 1.0f};
  mathv_ref::addNoiseOne(in, refOut, 0, 1, dp.r);
  std::vector<SwPoint> src(1, in), dst;
  bool dispatched = disp.dispatch(dp.h, src, dst) && dst.size() == 1;
  auto allNan = [](const SW_FLOAT4& q) {
    return std::isnan(q.x) && std::isnan(q.y) && std::isnan(q.z) && std::isnan(q.w);
  };
  bool refIsNan = allNan(refOut.Rotation);
  bool gpuIsNan = dispatched && allNan(dst[0].Rotation);
  printf("[mathv-addnoise-nan-trap] ref.Rotation=(%.6g,%.6g,%.6g,%.6g)[NaN=%s] gpu=%s[NaN=%s]\n",
         refOut.Rotation.x, refOut.Rotation.y, refOut.Rotation.z, refOut.Rotation.w,
         refIsNan ? "yes" : "no", dispatched ? "dispatched" : "DISPATCH-FAILED",
         gpuIsNan ? "yes" : "no");
  bool consistent = dispatched && (refIsNan == gpuIsNan);
  printf("[mathv-addnoise-nan-trap] verdict: %s\n",
         consistent ? "CONSISTENT -> pin candidate for a future regular tooth"
                    : "DIVERGENT -> evidence pack, route to S/X per §7 (not an eps/ref fix here)");
  return consistent;
}

}  // namespace

int runMathvAddNoiseSelfTest(bool injectBug) {
  ParityHarness h;
  if (!h.ok()) {
    printf("[selftest-mathv-addnoise] FAIL: no metallib\n");
    return 1;
  }
  AddNoiseDispatch disp(h.dev, h.queue, h.lib);
  if (!disp.ok) {
    printf("[selftest-mathv-addnoise] FAIL: no addnoise kernel\n");
    return 1;
  }

  MathvCase c;
  c.opName = "addnoise";
  c.params = paramTable();
  c.inDim = c.outDim = 3;           // Position only (Rotation/idx/StrengthMode get their own teeth)
  c.eps = EpsSpec::transcendental();  // §2.2 — sin/noise/normalize class
  c.inputLo = -4.0f; c.inputHi = 4.0f;
  // identity sentinel: Amount=0 -> GetNoise always (0,0,0) (self-check case 1) -> Position_out ==
  // Position_in exactly, whatever the rest are (scaled by Amount==0 or unread by Position).
  c.identityParams = {{0.0f, 1.3f, 0.7f, 1.0f, 1.0f, 1.0f, 0.25f, 0.0f, 0.0f, 0.0f}};
  c.gpu = [&disp](const std::vector<float>& P, const std::vector<float>& in, std::vector<float>& out) {
    DualParams dp = paramsFromPrimary(P);
    std::vector<SwPoint> src(in.size() / 3), dst;
    for (size_t i = 0; i < src.size(); ++i) {
      src[i].Position = SW_PACKED3{in[i * 3 + 0], in[i * 3 + 1], in[i * 3 + 2]};
      src[i].Rotation = SW_FLOAT4{0.0f, 0.0f, 0.0f, 1.0f};  // pinned — TOOTH ROTATION covers this
      src[i].FX1 = 0.0f; src[i].FX2 = 0.0f;                 // unread — StrengthMode pinned 0
    }
    if (!disp.dispatch(dp.h, src, dst) || dst.size() != src.size()) return false;
    out.resize(dst.size() * 3);
    for (size_t i = 0; i < dst.size(); ++i) {
      out[i*3+0] = dst[i].Position.x; out[i*3+1] = dst[i].Position.y; out[i*3+2] = dst[i].Position.z;
    }
    return true;
  };
  c.ref = [](const std::vector<float>& P, const float* in, float* out) {
    DualParams dp = paramsFromPrimary(P);
    SwPoint pin{}, pout{};
    pin.Position = SW_PACKED3{in[0], in[1], in[2]};
    pin.Rotation = SW_FLOAT4{0.0f, 0.0f, 0.0f, 1.0f};
    mathv_ref::addNoiseOne(pin, pout, /*idx=*/0, /*numStructs=*/1, dp.r);
    out[0] = pout.Position.x; out[1] = pout.Position.y; out[2] = pout.Position.z;
  };

  // KNOWN FINDING (measured, two seeds): PRIMARY is RED on the no-bug leg. mathv_harness.h's own
  // grid layer unconditionally probes denormal params (±1e-40, mathv_input.h finiteSpecials())
  // regardless of domain; GPU FTZ vs this ref's gradual-underflow on a denormal param nudges the
  // snoise lookup enough to flip a simplex cell at a small FIXED subset of grid positions: 25-31/
  // 198912 scalars miss (~0.013-0.016%), ALL in "grid-param", ZERO across the 32768-sample RANDOM
  // layer (both seeds) — the denormal-input "downstream divergence" mathv_compare.h's own doc
  // anticipates, not a ref/kernel bug. Comparator's Transcendental class has no per-sample
  // exemption (only Branchy consults branchDist); extending it is shared infra, out of scope
  // here. Left RED with full evidence (cmp.print() inside runMathvFuzz) for S/X triage.
  bool passPos = mathv::runMathvFuzz(c, injectBug);
  if (injectBug) return mathv::mathvVerdictToExit(passPos, true, "addnoise");

  bool passRotation = checkRotationTooth(disp);
  bool passVariation = checkVariationIdxTooth(disp);
  bool passStrength = checkStrengthModeTooth(disp);
  bool nanConsistent = checkNanTrapProbe(disp);

  ParityReport rep("selftest-mathv-addnoise");
  rep.expectTrue("position(3-layer fuzz)", passPos, passPos ? 1.0 : 0.0);
  rep.expectTrue("rotation(direct random-quat)", passRotation, passRotation ? 1.0 : 0.0);
  rep.expectTrue("variationIdx(idx-threaded hash41u)", passVariation, passVariation ? 1.0 : 0.0);
  rep.expectTrue("strengthMode(enum 0/1/2 + fallthrough)", passStrength, passStrength ? 1.0 : 0.0);
  rep.expectTrue("nanTrapProbe(RLD=0 discovery, §7)", nanConsistent, nanConsistent ? 1.0 : 0.0);
  return rep.finish();
}

// order 1002: appends right after mathv-wrappointposition (1001).
REGISTER_SELFTESTS(/*orderBase=*/1002, {"mathv-addnoise", runMathvAddNoiseSelfTest});

}  // namespace sw
