#pragma once
// selftests_mathv_reorientlinepoints_shared.h — shared dispatch adapter + corpus/classification
// helpers for the ReorientLinePoints mathv TU (Tier-H: quat qLookAt/qSlerp, §1.4 pre-emptive split
// — pulled out 2026-07-10 to make room for the qSlerp clamp-hi/-lo coverage tooth without busting the
// 400-line hard gate, same reasoning as selftests_mathv_blendpoints_shared.h /
// selftests_mathv_snappointstogrid_shared.h). Both selftests_mathv_reorientlinepoints.cpp (PRIMARY
// fuzz + identity/no-writeback/OOB quirk teeth + registration) and
// selftests_mathv_reorientlinepoints_probes.cpp (qSlerp clamp-hi/-lo branch tooth) include this
// header instead of forking two copies of the dispatch plumbing.
//
// Ordinary (non-anonymous) namespace on purpose (mathv_snap_shared / mathv_bp_shared precedent): this
// header must be textually identical across the two TUs that include it.
#include "mathv_harness.h"
#include "mathv_ref_reorientlinepoints.h"
#include "runtime/reorientlinepoints_params.h"
#include "runtime/tixl_point.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace sw {
namespace mathv_rlp_shared {

// direct-kernel dispatch adapter. `amount` is the sole live ABI param; GPU buffer is allocated to
// EXACTLY `in.size()` slots (no padding — the metal guard never reads past it, NAMED FORK "next-
// neighbour OOB-read guard" in reorientlinepoints.metal).
struct ReorientDispatch {
  MTL::Device* dev;
  MTL::CommandQueue* queue;
  MTL::ComputePipelineState* pso = nullptr;
  bool ok = false;
  ReorientDispatch(MTL::Device* d, MTL::CommandQueue* q, MTL::Library* lib) : dev(d), queue(q) {
    MTL::Function* fn =
        lib->newFunction(NS::String::string("reorientlinepoints", NS::UTF8StringEncoding));
    if (!fn) return;
    NS::Error* err = nullptr;
    pso = dev->newComputePipelineState(fn, &err);
    fn->release();
    ok = pso != nullptr;
  }
  ~ReorientDispatch() { if (pso) pso->release(); }
  ReorientDispatch(const ReorientDispatch&) = delete;
  bool dispatch(float amount, const std::vector<SwPoint>& in, std::vector<SwPoint>& out) const {
    if (!ok) return false;
    const uint32_t n = (uint32_t)in.size();
    out.clear();
    if (n == 0) return true;
    MTL::Buffer* srcBuf = dev->newBuffer(in.data(), (NS::UInteger)(n * sizeof(SwPoint)),
                                        MTL::ResourceStorageModeShared);
    MTL::Buffer* dstBuf = dev->newBuffer((NS::UInteger)(n * sizeof(SwPoint)),
                                        MTL::ResourceStorageModeShared);
    ReorientLineParams P{}; P.Count = n; P.Amount = amount;
    MTL::CommandBuffer* cmd = queue->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pso);
    enc->setBuffer(srcBuf, 0, REORIENTLINE_SourcePoints);
    enc->setBuffer(dstBuf, 0, REORIENTLINE_ResultPoints);
    enc->setBytes(&P, sizeof(P), REORIENTLINE_Params);
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

inline const std::vector<mathv::ParamDomain>& paramTable() {
  static const std::vector<mathv::ParamDomain> t = {
      {"Amount", 0.0f, 1.0f, mathv::ParamDomain::Linear,
       "external/tixl ReorientLinePoints.t3ui:16-25 Amount Min=0.0 Max=1.0 ClampMin/ClampMax=true"},
  };
  return t;
}

// Random UNIT quaternion (axis-angle) — same shape as addnoise.cpp's (each mathv TU duplicates it).
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

// Random-WALK polyline (not an independent point cloud): consecutive alive points carry a
// meaningful, varying tangent. Interior points (never 0/n-1) become a "line break" (NaN Scale.x)
// sentinel at `breakProb`; the rest get a random-in-domain Scale.x doubling as the alive flag.
inline std::vector<SwPoint> buildRandomPolyline(mathv::Rng& rng, size_t n, float breakProb) {
  std::vector<SwPoint> pts(n);
  float x = 0.0f, y = 0.0f, z = 0.0f;
  for (size_t i = 0; i < n; ++i) {
    if (i > 0) {
      float dx = rng.uniform(-1.0f, 1.0f), dy = rng.uniform(-1.0f, 1.0f), dz = rng.uniform(-1.0f, 1.0f);
      float len = std::sqrt(dx * dx + dy * dy + dz * dz);
      if (len < 1e-6f) { dx = 1.0f; dy = 0.0f; dz = 0.0f; len = 1.0f; }
      float step = rng.uniform(0.2f, 2.0f);
      x += dx / len * step; y += dy / len * step; z += dz / len * step;
    }
    SwPoint p{};
    p.Position = SW_PACKED3{x, y, z};
    QuatF q = randomUnitQuat(rng);
    p.Rotation = SW_FLOAT4{q.x, q.y, q.z, q.w};
    p.Color = SW_FLOAT4{rng.uniform(0.0f, 1.0f), rng.uniform(0.0f, 1.0f), rng.uniform(0.0f, 1.0f),
                        rng.uniform(0.0f, 1.0f)};
    p.Scale = SW_PACKED3{rng.uniform(0.1f, 3.0f), rng.uniform(-4.0f, 4.0f), rng.uniform(-4.0f, 4.0f)};
    p.FX1 = rng.uniform(-4.0f, 4.0f);
    p.FX2 = rng.uniform(-4.0f, 4.0f);
    pts[i] = p;
  }
  if (breakProb > 0.0f)
    for (size_t i = 1; i + 1 < n; ++i)
      if (rng.uniform(0.0f, 1.0f) < breakProb)
        pts[i].Scale.x = std::numeric_limits<float>::quiet_NaN();
  return pts;
}

// BRANCH CLASSIFICATION (instrumentation only, mirrors the already-oracled branch conditions, purely
// for evidence bucketing — never substitutes for the compared value). qSlerp has 6 buckets (clamp-hi,
// clamp-lo, acos-direct, acos-flipped, linear-direct, linear-flipped) — clamp-hi/-lo are ENGINEERED
// (not randomly reachable, see selftests_mathv_reorientlinepoints_probes.cpp's checkClampBranchTooth,
// R comment correction 2026-07-10: an earlier header note miscounted this as "5", folding clamp-hi
// and clamp-lo into one named bucket even though classifyQSlerp always reported them separately).
inline const char* classifyQLookAt(mathv_ref::quat::Vec3 dir, mathv_ref::quat::Quat curRot) {
  using namespace mathv_ref::quat;
  Vec3 fwd = hlslNormalize3(dir);
  Vec3 oldUp = qRotateVec3({0.0f, 1.0f, 0.0f}, curRot);
  float d = hlslDot3(oldUp, fwd);
  Vec3 projUp{oldUp.x - fwd.x * d, oldUp.y - fwd.y * d, oldUp.z - fwd.z * d};
  if (hlslLength3(projUp) < 1e-5f) {
    Vec3 axis = std::fabs(fwd.x) < 0.9f ? Vec3{1.0f, 0.0f, 0.0f} : Vec3{0.0f, 1.0f, 0.0f};
    projUp = hlslNormalize3(axis);
    float d2 = hlslDot3(projUp, fwd);
    projUp = hlslNormalize3({projUp.x - fwd.x * d2, projUp.y - fwd.y * d2, projUp.z - fwd.z * d2});
  } else {
    projUp = hlslNormalize3(projUp);
  }
  Vec3 up{-projUp.x, -projUp.y, -projUp.z};
  Vec3 right = hlslNormalize3(hlslCross3(fwd, up));
  Vec3 up2 = hlslNormalize3(hlslCross3(fwd, right));
  float num8 = right.x + up2.y + fwd.z;
  if (num8 > 0.0f) return "qlookat-trace-pos";
  if (right.x >= up2.y && right.x >= fwd.z) return "qlookat-m00-dominant";
  if (up2.y > fwd.z) return "qlookat-m11-dominant";
  return "qlookat-else-m22-dominant";
}
inline const char* classifyQSlerp(mathv_ref::quat::Quat a, mathv_ref::quat::Quat b) {
  float cosHalf = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
  if (cosHalf >= 1.0f) return "qslerp-clamp-hi";
  if (cosHalf <= -1.0f) return "qslerp-clamp-lo";
  bool flipped = cosHalf < 0.0f;
  float eff = flipped ? -cosHalf : cosHalf;
  if (eff < 0.99f) return flipped ? "qslerp-acos-flipped" : "qslerp-acos-direct";
  return flipped ? "qslerp-linear-flipped" : "qslerp-linear-direct";
}

// Compact SwPoint literal: Position=(x,y,z), Scale=(scaleX,1,1) (scaleX==NaN -> dead sentinel).
inline SwPoint mkPt(float x, float y, float z, float scaleX) {
  SwPoint p{};
  p.Position = SW_PACKED3{x, y, z};
  p.Scale = SW_PACKED3{scaleX, 1.0f, 1.0f};
  return p;
}

// Defined in selftests_mathv_reorientlinepoints_probes.cpp — declared here so
// selftests_mathv_reorientlinepoints.cpp's registration function can call it across the TU split.
// Folds its clamp-hi/-lo hits into the SAME slerpCov map the PRIMARY tooth's random fuzz populates,
// so the final coverage gate reports genuine 6/6 once both are merged (not two disjoint counters).
bool checkClampBranchTooth(const ReorientDispatch& disp, std::map<std::string, int>* slerpCov);

}  // namespace mathv_rlp_shared
}  // namespace sw
