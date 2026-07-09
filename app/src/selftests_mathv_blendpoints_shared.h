#pragma once
// selftests_mathv_blendpoints_shared.h — shared dispatch adapter + CPU-ref bridge for the
// BlendPoints mathv TU (Tier-H: quat slerp + dual point-buffer combine, §1.4 pre-emptive split —
// the teeth list below doesn't fit one <400-line TU, same reasoning as
// selftests_mathv_snappointstogrid_shared.h). Both selftests_mathv_blendpoints.cpp (PRIMARY fuzz +
// identity-boundary tooth + registration) and selftests_mathv_blendpoints_probes.cpp (BlendMode/
// Scatter/NoBlend-NaN/PairingCounts/OffByOne teeth) include this header instead of forking two
// copies of the dispatch plumbing.
//
// Ordinary (non-anonymous) namespace on purpose (mathv_snap_shared precedent): this header must be
// textually identical across the two TUs that include it.
#include "mathv_harness.h"
#include "mathv_ref_blendpoints.h"
#include "runtime/blendpoints_params.h"
#include "runtime/tixl_point.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace sw {
namespace mathv_bp_shared {

// direct-kernel dispatch adapter (turbulence_parity_golden.cpp:75-112 shape, dual-SRV variant):
// fill PointsA/PointsB MTLBuffers, setBytes the 32B BlendPointsParams (CountA/CountB overwritten
// from the array sizes), dispatch "blendpoints", readback. `dispatchThreads`/`resultBufElems` are
// independently controllable (default = a.size()) so the off-by-one boundary probe can
// OVER-DISPATCH more GPU threads than the Result buffer's "intended" size while still allocating
// enough backing memory to observe what (if anything) got written past it.
struct BlendPointsDispatch {
  MTL::Device* dev;
  MTL::CommandQueue* queue;
  MTL::ComputePipelineState* pso = nullptr;
  bool ok = false;

  BlendPointsDispatch(MTL::Device* d, MTL::CommandQueue* q, MTL::Library* lib) : dev(d), queue(q) {
    MTL::Function* fn = lib->newFunction(NS::String::string("blendpoints", NS::UTF8StringEncoding));
    if (!fn) return;
    NS::Error* err = nullptr;
    pso = dev->newComputePipelineState(fn, &err);
    fn->release();
    ok = pso != nullptr;
  }
  ~BlendPointsDispatch() {
    if (pso) pso->release();
  }
  BlendPointsDispatch(const BlendPointsDispatch&) = delete;

  bool dispatch(BlendPointsParams prm, const std::vector<SwPoint>& a, const std::vector<SwPoint>& b,
                uint32_t dispatchThreads, uint32_t resultBufElems, std::vector<SwPoint>& out,
                SwPoint resultSentinel = SwPoint{}) const {
    if (!ok) return false;
    prm.CountA = (uint32_t)a.size();
    prm.CountB = (uint32_t)b.size();
    MTL::Buffer* aBuf =
        a.empty() ? dev->newBuffer(sizeof(SwPoint), MTL::ResourceStorageModeShared)
                  : dev->newBuffer(a.data(), (NS::UInteger)(a.size() * sizeof(SwPoint)),
                                   MTL::ResourceStorageModeShared);
    MTL::Buffer* bBuf =
        b.empty() ? dev->newBuffer(sizeof(SwPoint), MTL::ResourceStorageModeShared)
                  : dev->newBuffer(b.data(), (NS::UInteger)(b.size() * sizeof(SwPoint)),
                                   MTL::ResourceStorageModeShared);
    std::vector<SwPoint> initResult(resultBufElems, resultSentinel);
    MTL::Buffer* rBuf = dev->newBuffer(initResult.data(), (NS::UInteger)(resultBufElems * sizeof(SwPoint)),
                                       MTL::ResourceStorageModeShared);
    MTL::CommandBuffer* cmd = queue->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pso);
    enc->setBuffer(aBuf, 0, BLENDPOINTS_PointsA);
    enc->setBuffer(bBuf, 0, BLENDPOINTS_PointsB);
    enc->setBuffer(rBuf, 0, BLENDPOINTS_Result);
    enc->setBytes(&prm, sizeof(prm), BLENDPOINTS_Params);
    const uint32_t tg = 64;
    enc->dispatchThreadgroups(MTL::Size::Make((dispatchThreads + tg - 1) / tg, 1, 1),
                              MTL::Size::Make(tg, 1, 1));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
    out.assign(resultBufElems, SwPoint{});
    std::memcpy(out.data(), rBuf->contents(), (size_t)resultBufElems * sizeof(SwPoint));
    aBuf->release();
    bBuf->release();
    rBuf->release();
    return true;
  }

  // Ordinary (aligned, no over-dispatch) convenience overload: dispatchThreads == resultBufElems ==
  // a.size() (matches blendpoints.metal:48's hardcoded resultCount=CountA).
  bool dispatch(BlendPointsParams prm, const std::vector<SwPoint>& a, const std::vector<SwPoint>& b,
                std::vector<SwPoint>& out) const {
    return dispatch(prm, a, b, (uint32_t)a.size(), (uint32_t)a.size(), out);
  }
};

// SwPoint <-> flat float16 packing (Position.xyz, FX1, Rotation.xyzw, Color.xyzw, Scale.xyz, FX2 —
// exact tixl_point.h field order) shared by every tooth that feeds the generic per-element (P,in,
// out) harness shape or wants a plain float vector for evidence printing.
inline void packPoint(const SwPoint& p, float* out16) {
  out16[0] = p.Position.x; out16[1] = p.Position.y; out16[2] = p.Position.z;
  out16[3] = p.FX1;
  out16[4] = p.Rotation.x; out16[5] = p.Rotation.y; out16[6] = p.Rotation.z; out16[7] = p.Rotation.w;
  out16[8] = p.Color.x; out16[9] = p.Color.y; out16[10] = p.Color.z; out16[11] = p.Color.w;
  out16[12] = p.Scale.x; out16[13] = p.Scale.y; out16[14] = p.Scale.z;
  out16[15] = p.FX2;
}
inline SwPoint unpackPoint(const float* in16) {
  SwPoint p{};
  p.Position = SW_PACKED3{in16[0], in16[1], in16[2]};
  p.FX1 = in16[3];
  p.Rotation = SW_FLOAT4{in16[4], in16[5], in16[6], in16[7]};
  p.Color = SW_FLOAT4{in16[8], in16[9], in16[10], in16[11]};
  p.Scale = SW_PACKED3{in16[12], in16[13], in16[14]};
  p.FX2 = in16[15];
  return p;
}

// Random UNIT quaternion (axis-angle) — qSlerp is the ordinary-usage physical contract only for
// |q|==1 inputs (addnoise TOOTH ROTATION precedent).
struct QuatF { float x, y, z, w; };
inline QuatF randomUnitQuat(mathv::Rng& rng) {
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

// A representative, fully-populated random SwPoint (unit Rotation; other channels in modest finite
// ranges so no channel is a degenerate constant) — shared by every direct-dispatch tooth below.
inline void fillRandomPoint(mathv::Rng& rng, SwPoint& p) {
  p.Position = SW_PACKED3{rng.uniform(-4.0f, 4.0f), rng.uniform(-4.0f, 4.0f), rng.uniform(-4.0f, 4.0f)};
  p.FX1 = rng.uniform(-2.0f, 2.0f);
  QuatF q = randomUnitQuat(rng);
  p.Rotation = SW_FLOAT4{q.x, q.y, q.z, q.w};
  p.Color = SW_FLOAT4{rng.uniform(0.0f, 1.0f), rng.uniform(0.0f, 1.0f), rng.uniform(0.0f, 1.0f),
                      rng.uniform(0.0f, 1.0f)};
  p.Scale = SW_PACKED3{rng.uniform(0.1f, 4.0f), rng.uniform(0.1f, 4.0f), rng.uniform(0.1f, 4.0f)};
  p.FX2 = rng.uniform(-2.0f, 2.0f);
}

// Host (GPU ABI) / ref (CPU oracle) param builders from one call site each, so the two structs can
// never silently drift field-for-field (§0: the ABI names are the only legal shared artifact).
// CountA/CountB are NOT set here — BlendPointsDispatch::dispatch fills them from the array sizes.
inline BlendPointsParams hostParamsFrom(float blendFactor, float blendMode, float pairingMode,
                                         float width, float scatter) {
  BlendPointsParams p{};
  p.BlendFactor = blendFactor; p.BlendMode = blendMode; p.PairingMode = pairingMode;
  p.Width = width; p.Scatter = scatter;
  return p;
}
inline mathv_ref::BlendPointsParams refParamsFrom(float blendFactor, float blendMode,
                                                   float pairingMode, float width, float scatter) {
  mathv_ref::BlendPointsParams p{};
  p.blendFactor = blendFactor; p.blendMode = blendMode; p.pairingMode = pairingMode;
  p.width = width; p.scatter = scatter;
  return p;
}

// PRIMARY's ParamDomain table — BlendFactor ONLY (see selftests_mathv_blendpoints.cpp header's
// COMBINE-CLASS STRUCTURAL LIMIT note for why BlendMode/PairingMode/Width/Scatter are pinned, not
// fuzzed, in the generic per-element harness).
inline const std::vector<mathv::ParamDomain>& paramTable() {
  static const std::vector<mathv::ParamDomain> t = {
      {"BlendFactor", -1.0f, 2.0f, mathv::ParamDomain::Linear,
       "external/tixl BlendPoints.t3ui:41-42 Min=0 Max=1; widened to [-1,2] -- the SAME file's "
       "InputUi Description explicitly documents legitimate overshoot beyond [0,1] under Mix mode "
       "(plain lerp extrapolation, well-defined), and RangedSmooth folds BlendFactor via "
       "fmod(.,2) (BlendPoints.hlsl:80) so values outside [0,1] are meaningful there too (exercised "
       "by the dedicated BlendMode tooth in the probes TU, not PRIMARY -- PRIMARY pins Mix)."},
  };
  return t;
}

// Defined in selftests_mathv_blendpoints_probes.cpp — declared here so
// selftests_mathv_blendpoints.cpp's registration function can call them across the TU split.
bool checkBlendModeTooth(const BlendPointsDispatch& disp);
bool checkScatterTooth(const BlendPointsDispatch& disp);
bool checkNoBlendNanTooth(const BlendPointsDispatch& disp);
bool checkPairingCountsTooth(const BlendPointsDispatch& disp);
bool checkOffByOneTooth(const BlendPointsDispatch& disp);

}  // namespace mathv_bp_shared
}  // namespace sw
