// selftests_mathv_addnoise.cpp — --selftest-mathv-addnoise (D role, fuzz driver).
// MATH_VERIFY_WORKFLOW.md 工單2 pilot: fuzz the GPU "addnoise" kernel (app/shaders/addnoise.metal)
// against the R-authored CPU oracle (app/src/mathv_ref_addnoise.h, TRANSCRIBED from external/tixl
// HLSL) via the shared mathv harness (direct-kernel dispatch, §1.3). AddNoise is the §2
// TRANSCENDENTAL pilot (sin/normalize/rsqrt): EpsSpec::transcendental() everywhere below.
// ── ParamDomain provenance (external/tixl SHA 395c4c55, AddNoise.t3/.t3ui) ─────────────────────
//   Amount t3ui:31-40 Min=0.0 Max=2.0 verbatim. RotationLookupDistance t3ui:58-68 Min=0.001
//   ClampMin=true (lo=Min, hi=Default(0.25)+4). Frequency/Phase/AmountDistribution/NoiseOffset: no
//   Min/Max -> §3.2 Default±4 fallback. Variation/StrengthFactor NOT in the 10-param PRIMARY table.
// ── SCOPE split across FIVE teeth ───────────────────────────────────────────────────────────────
// The generic 3-layer harness has no per-element index (can't replicate AddNoise.hlsl:119's
// per-THREAD `hash41u(idx)`) and no 2nd input channel alongside Position — every axis it can't
// structurally carry gets its own EQUAL-STRENGTH direct-dispatch tooth (pilot #1 discipline):
//   PRIMARY (3-layer) — Position(3). Rotation/StrengthMode/Variation all PINNED (their own teeth);
//     the ONLY injectBug tooth.
//   TOOTH ROTATION — Rotation(4), random UNIT quaternion; RotationLookupDistance load-bearing.
//   TOOTH VARIATION/IDX — N=257 spans 5 threadgroups; validates hash41u(idx) end to end.
//   TOOTH STRENGTHMODE — {0,1,2} exhaustive + {-1,5} fallthrough-to-FX2 bonus.
//   TOOTH NAN-TRAP (mathv fixer pilot #2 S/X verdict — promoted from discovery probe to a REGULAR
//     PINNED tooth): RotationLookupDistance=0 hand-proven all-NaN Rotation (ref self-check case 2,
//     NOTED-QUIRK, not a NAMED-FORK). REACHABLE despite t3ui's `Min=0.001 ClampMin=true` (S-audit:
//     that clamp is editor-UI-only; a curve/connection can still feed 0). Both sides now MUST agree.
// KNOWN LOW-RISK GAP (X channel-coverage note): Color/Scale/FX1/FX2 are struct-copy passthrough,
// never explicitly asserted by any tooth above. Flagged, not gated — deferred.
// ZONE: shell tier; crosses runtime only for SwPoint + the params ABI header.
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
      {"Frequency", -3.0f, 5.0f, ParamDomain::Linear, "AddNoise.t3:25-27 Default=1.0 -> Default±4"},
      {"Phase", -4.0f, 4.0f, ParamDomain::Linear, "AddNoise.t3:36-39 Default=0.0 -> Default±4"},
      {"AmountDistributionX", -3.0f, 5.0f, ParamDomain::Linear,
       "AddNoise.t3:41-47 Default=(1,1,1) -> Default±4"},
      {"AmountDistributionY", -3.0f, 5.0f, ParamDomain::Linear, "ditto"},
      {"AmountDistributionZ", -3.0f, 5.0f, ParamDomain::Linear, "ditto"},
      {"RotationLookupDistance", 0.001f, 4.25f, ParamDomain::Linear,
       "AddNoise.t3ui:58-68 Min=0.001 ClampMin=true -> lo=Min, hi=Default(0.25)+4"},
      {"NoiseOffsetX", -4.0f, 4.0f, ParamDomain::Linear, "AddNoise.t3:9-14 Default=0 -> Default±4"},
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

// Shared direct-dispatch compare core: dispatch `src` once, compare Position(3)+Rotation(4)=7
// lanes/element against mathv_ref::addNoiseOne (idx==buffer position==GPU thread id).
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

// ── TOOTH ROTATION: random Position + random UNIT-quaternion Rotation, RotationLookupDistance now
// load-bearing. NOTE: qFromMatrix3Precise recovers q up to a GLOBAL SIGN (Shepperd's method), so
// "Rotation_out==Rotation_in" is only a valid identity claim at ref self-check case 1 (canonical
// identity input, no sign-flip ambiguity) — checked GPU-side below.
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

// ── TOOTH VARIATION/IDX: N=257 spans 5 threadgroups of 64 (one partial, addnoise.metal:106
// guarded) — exercises hash41u(idx) across a real multi-group dispatch end to end.
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

// ── TOOTH STRENGTHMODE: {0,1,2} = §3.3 exhaustive enum sweep; {-1,5} = ref's documented
// fallthrough-to-FX2 fidelity bonus (mathv_ref_addnoise.h :276-277). FX1/FX2 vary per point.
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

// ── TOOTH NAN-TRAP (REGULAR PINNED-PARITY tooth, see file header) — RotationLookupDistance=0,
// Amount=0, ref self-check case 2 (mathv_ref_addnoise.h :368-385): normalize(0,0,0)->NaN slips past
// the degeneracy guard. Both sides MUST agree (all-NaN Rotation + Position passthrough); a RED means
// a real regression against the pinned NOTED-QUIRK fork, not a §7 discovery route anymore.
bool checkNanTrapTooth(const AddNoiseDispatch& disp) {
  Comparator cmp("mathv-addnoise-nan-trap", EpsSpec::exact(), 5);
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
  printf("[mathv-addnoise-nan-trap] ref NaN=%s gpu NaN=%s dispatched=%s\n", refIsNan ? "yes" : "no",
         gpuIsNan ? "yes" : "no", dispatched ? "yes" : "no");
  // pinned assertions (NOTED-QUIRK, not a NAMED-FORK): ref+gpu Rotation both all-NaN, AND Position
  // untouched (offset stayed exactly 0 on both sides — Amount=0 -> GetNoise==(0,0,0) regardless).
  float posIn[3] = {in.Position.x, in.Position.y, in.Position.z};
  cmp.add(dst[0].Position.x, refOut.Position.x, posIn, 3, 0, -1.0f, "nan-trap-position");
  cmp.add(dst[0].Position.y, refOut.Position.y, posIn, 3, 1, -1.0f, "nan-trap-position");
  cmp.add(dst[0].Position.z, refOut.Position.z, posIn, 3, 2, -1.0f, "nan-trap-position");
  cmp.print();
  bool pinned = dispatched && refIsNan && gpuIsNan && cmp.verdict();
  printf("[mathv-addnoise-nan-trap] verdict: %s\n", pinned ? "PINNED" : "RED -- regression");
  return pinned;
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
  // Ill-conditioned-lookup exemption wiring (mathv_compare.h §A criteria; MATH_VERIFY_WORKFLOW.md §2
  // 2b). branchDist doubles as the Transcendental candidate flag (criterion 1): PRIMARY's sole live
  // GetNoise() call has variationOffset==(0,0,0) (Variation pinned 0) so noiseLookup ==
  // (0.91*(Position+NoiseOffset)+Phase)*Frequency exactly (AddNoise.hlsl:31). P layout here is
  // {Amount,Frequency,Phase,AmountDist.xyz,RotationLookupDistance,NoiseOffset.xyz}: P[1]=Frequency,
  // P[2]=Phase, P[7..9]=NoiseOffset.xyz. >=4096 -> ulp there is macroscopic next to a simplex cell's
  // O(1) width -> fast-math re-association can legally pick a different cell.
  c.branchDist = [](const std::vector<float>& P, const float* in, int /*lane*/) {
    float maxAbs = 0.0f;
    for (int axis = 0; axis < 3; ++axis) {
      float noiseLookup = (0.91f * (in[axis] + P[7 + axis]) + P[2]) * P[1];
      maxAbs = std::fmax(maxAbs, std::fabs(noiseLookup));
    }
    return maxAbs >= 4096.0f ? (maxAbs - 4096.0f) : -1.0f;  // >=0 candidate / <0 not, shared channel
  };
  // Envelope (criterion 3): both sides consume the bit-identical Position_in, so `gpu/ref - in[lane]`
  // recovers each side's noise OFFSET (what getNoise()'s formula actually bounds — the raw
  // Position_out is dominated by Position_in, domain [-4,4], and is not what this bounds). weight==1
  // pinned in PRIMARY; 2.0 is headroom over snoise's ordinary ~[-1,1] range.
  c.illConditionedEnvelope = [](const std::vector<float>& P, const float* in, int lane, float gpu,
                                float ref) {
    float bound = std::fabs(P[0] / 10.0f) * std::fabs(P[3 + lane]) * /*weight=*/1.0f * 2.0f;
    return std::fabs(gpu - in[lane]) <= bound && std::fabs(ref - in[lane]) <= bound;
  };

  // KNOWN FINDING, RESOLVED (S-verdict 2026-07-10): PRIMARY's earlier RED (25-31/198912 scalars,
  // ALL "grid-param", ZERO across the RANDOM layer) root-causes to Frequency=±1e6 (mathv_input.h's
  // own special value) driving the noise-lookup coordinate ill-conditioned, NOT the denormal-param
  // probe originally suspected (S: 0 misses isolating the denormal alone; X bisect independently
  // confirmed Frequency=±1e6 as sole trigger). Not a ref/MSL bug — the exemption above now bounds it.
  bool passPos = mathv::runMathvFuzz(c, injectBug);
  if (injectBug) return mathv::mathvVerdictToExit(passPos, true, "addnoise");

  bool passRotation = checkRotationTooth(disp);
  bool passVariation = checkVariationIdxTooth(disp);
  bool passStrength = checkStrengthModeTooth(disp);
  bool nanPinned = checkNanTrapTooth(disp);

  ParityReport rep("selftest-mathv-addnoise");
  rep.expectTrue("position(3-layer fuzz)", passPos, passPos ? 1.0 : 0.0);
  rep.expectTrue("rotation(direct random-quat)", passRotation, passRotation ? 1.0 : 0.0);
  rep.expectTrue("variationIdx(idx-threaded hash41u)", passVariation, passVariation ? 1.0 : 0.0);
  rep.expectTrue("strengthMode(enum 0/1/2 + fallthrough)", passStrength, passStrength ? 1.0 : 0.0);
  rep.expectTrue("nanTrap(pinned parity, RLD=0 — RED=regression)", nanPinned, nanPinned ? 1.0 : 0.0);
  return rep.finish();
}

// order 1002: appends after mathv-wrappointposition (1001).
REGISTER_SELFTESTS(/*orderBase=*/1002, {"mathv-addnoise", runMathvAddNoiseSelfTest});

}  // namespace sw
