// selftests_mathv_reorientlinepoints.cpp — --selftest-mathv-reorientlinepoints (D role, fuzz driver).
// MATH_VERIFY_WORKFLOW.md 量產鏈 Tier-H: fuzz GPU "reorientlinepoints" vs the R-authored CPU oracle
// (mathv_ref_reorientlinepoints.h + mathv_ref_shared_quat.h, TRANSCRIBED from external/tixl HLSL) via
// direct-kernel dispatch (§1.3). NOT the generic mathv_harness.h MathvCase runner: output at index i
// depends on its NEIGHBOURS' Position, not just its own element, so the generic per-element
// `ref(P,e,out)` signature can't carry it — every tooth below is a direct Comparator batch
// (addnoise.cpp's TOOTH ROTATION shape, as the whole test, not a PRIMARY-layer supplement).
//
// ParamDomain: ReorientLinePoints.t3ui:16-25 Amount Min=0.0 Max=1.0 ClampMin/ClampMax=true — the only
// live ABI field (Center/UpVector/WIsWeight/Flip are declared but never read; sw drops them, no slot).
//
// LINE-STRUCTURED INPUT: buildRandomPolyline() random-walks (bounded step length) so alive points
// carry a meaningful, varying tangent — a random cloud gives poor qLookAt/qSlerp branch diversity. A
// NaN-Scale.x "line break" sentinel (TiXL's own dead-point convention) is sprinkled at a tunable rate.
//
// EQUAL-STRENGTH CHANNELS: Rotation(4, transcendental — acos/sin) is the only computed channel;
// Position/Color/Scale/FX1/FX2 (12 lanes, exact) are copy-through on EVERY path (early-return OR the
// live `*out=p` pre-copy) — checked with equal rigor, same corpus.
//
// BRANCH COVERAGE (quat 分支覆蓋 batch-tag): qLookAt has 4 branches (trace>0/m00/m11/else-dominant),
// qSlerp has 5 (clamp-hi/-lo, acos±flip, linear±flip). classifyQLookAt/classifyQSlerp are
// INSTRUMENTATION ONLY (replicate the already-oracled branch CONDITIONS for bucketing, never
// substitute for the compared value); every Rotation-lane compare is tagged with both, gated >=3/4
// and >=3/5. clamp-hi/-lo have no simple closed-form trigger and are not engineered — noted, not required.
//
// QUIRK PROBES: 1. NO-WRITEBACK (ref :119-130) — TiXL's early `return;` leaves ResultPoints[idx]
// UNWRITTEN; reorientlinepoints.metal FORKS to an explicit copy-through. Probed on all 3 early-return
// conditions: GPU copy-through must equal ref-called-with-its-documented-preseed contract
// (`out[i]=in[i]` before the call) — pinned parity. A naive zero-init call (the oracle's documented
// usage TRAP) is shown diverging too — printed, not gated. 2. OOB-READ at the last point (ref
// :132-148, metal NAMED FORK "guard index+1<numStructs") — sw's GPU buffer has no slot `count`, so
// GPU always behaves as if the padding were NaN. ref(padding=NaN)==GPU is the fork's own invariant
// (pinned); ref(padding=a real differing point) reproduces TiXL's literal OOB-read bug and diverges
// from GPU (documented NAMED FORK, sanity-asserted to actually differ, proving the probe fired).
//
// ZONE: shell tier (app/src/ root, mathv support). Crosses runtime only for SwPoint + params ABI.
#include "mathv_harness.h"
#include "mathv_ref_reorientlinepoints.h"
#include "runtime/reorientlinepoints_params.h"
#include "runtime/selftest_registry.h"
#include "runtime/tixl_point.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <vector>
#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif
namespace sw {
namespace {
using mathv::Comparator;
using mathv::EpsSpec;
using mathv::ParamDomain;
using mathv::Rng;

// direct-kernel dispatch adapter. `amount` is the sole live ABI param; GPU buffer is allocated to
// EXACTLY `in.size()` slots (no padding — the metal guard never reads past it, see quirk 2).
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
const std::vector<ParamDomain>& paramTable() {
  static const std::vector<ParamDomain> t = {
      {"Amount", 0.0f, 1.0f, ParamDomain::Linear,
       "external/tixl ReorientLinePoints.t3ui:16-25 Amount Min=0.0 Max=1.0 ClampMin/ClampMax=true"},
  };
  return t;
}

// Random UNIT quaternion (axis-angle) — same shape as addnoise.cpp's (each mathv TU duplicates it).
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
// Random-WALK polyline (not an independent point cloud): consecutive alive points carry a
// meaningful, varying tangent. Interior points (never 0/n-1) become a "line break" (NaN Scale.x)
// sentinel at `breakProb`; the rest get a random-in-domain Scale.x doubling as the alive flag.
std::vector<SwPoint> buildRandomPolyline(Rng& rng, size_t n, float breakProb) {
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
// for evidence bucketing — never substitutes for the compared value).
const char* classifyQLookAt(mathv_ref::quat::Vec3 dir, mathv_ref::quat::Quat curRot) {
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
const char* classifyQSlerp(mathv_ref::quat::Quat a, mathv_ref::quat::Quat b) {
  float cosHalf = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
  if (cosHalf >= 1.0f) return "qslerp-clamp-hi";
  if (cosHalf <= -1.0f) return "qslerp-clamp-lo";
  bool flipped = cosHalf < 0.0f;
  float eff = flipped ? -cosHalf : cosHalf;
  if (eff < 0.99f) return flipped ? "qslerp-acos-flipped" : "qslerp-acos-direct";
  return flipped ? "qslerp-linear-flipped" : "qslerp-linear-direct";
}

// MAIN TOOTH: random-line rotation parity (transcendental) + passthrough (exact) on the SAME corpus.
// injectBug: GPU-side amount += 1e-2 (§1.5 — Amount is params[0], the sole live ABI slot).
bool checkMainTooth(const ReorientDispatch& disp, bool injectBug,
                    std::map<std::string, int>* lookatCov, std::map<std::string, int>* slerpCov) {
  Comparator cmpRot("mathv-reorientlinepoints-rotation", EpsSpec::transcendental(), 5);
  Comparator cmpPass("mathv-reorientlinepoints-passthrough", EpsSpec::exact(), 5);
  Rng rng(mathv::mathvSeed(injectBug ? "reorientlinepoints-main-bug" : "reorientlinepoints-main"));
  const auto& dom = paramTable();
  bool dispatchOk = true;
  for (int pl = 0; pl < 24; ++pl) {
    size_t n = 6 + (size_t)(rng.uniform(0.0f, 58.0f));  // 6..64 points
    float breakProb = (pl % 3 == 0) ? 0.0f : 0.15f;
    std::vector<SwPoint> line = buildRandomPolyline(rng, n, breakProb);
    float amount = mathv::sampleUniform(rng, dom[0]);
    std::vector<SwPoint> gpuOut;
    if (!disp.dispatch(injectBug ? amount + 1e-2f : amount, line, gpuOut) || gpuOut.size() != n) {
      dispatchOk = false; continue;
    }
    // ref: n+1 elements, padding[n].Scale.x=NaN (matches GPU's structurally-guarded semantics —
    // the true OOB-bug padding is exercised separately in checkOobLastPointQuirk).
    std::vector<SwPoint> inPadded(n + 1);
    for (size_t i = 0; i < n; ++i) inPadded[i] = line[i];
    inPadded[n] = SwPoint{};
    inPadded[n].Scale.x = std::numeric_limits<float>::quiet_NaN();
    std::vector<SwPoint> refOut(line.begin(), line.end());  // preseed: NO-WRITEBACK contract
    mathv_ref::mathvRefReorientLinePoints(inPadded.data(), refOut.data(), n, amount);
    // SwPoint is a 16-float-contiguous POD (offsets already static_assert'd in tixl_point.h):
    // floats[0..2]=Position [3]=FX1 [4..7]=Rotation [8..11]=Color [12..14]=Scale [15]=FX2 — cast once
    // per element instead of hand-listing 12+ struct-field literals per channel.
    static const int kPassLanes[12] = {0, 1, 2, 3, 8, 9, 10, 11, 12, 13, 14, 15};
    for (size_t i = 0; i < n; ++i) {
      const float* gf = reinterpret_cast<const float*>(&gpuOut[i]);
      const float* rf = reinterpret_cast<const float*>(&refOut[i]);
      const float* inf = reinterpret_cast<const float*>(&line[i]);
      for (int k : kPassLanes) cmpPass.add(gf[k], rf[k], inf, 16, k, -1.0f, "passthrough");
      // Recompute the neighbour scan (instrumentation only) so the batch tag names the REAL branch.
      uint32_t prevIndex = (uint32_t)i, nextIndex = (uint32_t)i;
      if (i > 0 && !std::isnan(line[i - 1].Scale.x)) prevIndex = (uint32_t)(i - 1);
      if (i + 1 <= n && !std::isnan(inPadded[i + 1].Scale.x)) nextIndex = (uint32_t)(i + 1);
      // batch tag = a STATIC string literal (Evidence stores a raw `const char*`, not a copy — a
      // per-iteration snprintf buffer would dangle by print() time; only literals are safe here).
      const char* tag = "no-tangent";
      if (!std::isnan(line[i].Scale.x) && prevIndex != nextIndex) {
        mathv_ref::quat::Vec3 v{inPadded[nextIndex].Position.x - inPadded[prevIndex].Position.x,
                                inPadded[nextIndex].Position.y - inPadded[prevIndex].Position.y,
                                inPadded[nextIndex].Position.z - inPadded[prevIndex].Position.z};
        float l = mathv_ref::quat::hlslLength3(v);
        if (l < 0.0001f) {
          tag = "degenerate-dir";
        } else {
          mathv_ref::quat::Vec3 dir{v.x / l, v.y / l, v.z / l};
          mathv_ref::quat::Quat curRot{line[i].Rotation.x, line[i].Rotation.y, line[i].Rotation.z,
                                       line[i].Rotation.w};
          const char* lk = classifyQLookAt(dir, curRot);
          const char* sl = classifyQSlerp(curRot, mathv_ref::qAlignForward2(curRot, dir));
          if (lookatCov) ++(*lookatCov)[lk];
          if (slerpCov) ++(*slerpCov)[sl];
          tag = sl;
        }
      }
      for (int k = 0; k < 4; ++k) cmpRot.add(gf[4 + k], rf[4 + k], inf + 4, 4, k, -1.0f, tag);
    }
  }
  cmpPass.print();
  cmpRot.print();
  return dispatchOk && cmpPass.verdict() && cmpRot.verdict();
}

// IDENTITY SENTINEL: Amount=0 -> qSlerp(cur,target,0)==cur (ref self-check case A/C: blendA==1.0/
// blendB==0.0 exactly at t=0). GPU rotation vs the ORIGINAL input rotation, loosened-exact (qSlerp
// still renormalizes at t=0, so a not-quite-unit input quaternion drifts by ulp).
bool checkIdentityAmountZeroTooth(const ReorientDispatch& disp) {
  Comparator cmp("mathv-reorientlinepoints-identity-amount0", EpsSpec::exact(), 5);
  Rng rng(mathv::mathvSeed("reorientlinepoints-identity"));
  bool dispatchOk = true;
  for (int pl = 0; pl < 12; ++pl) {
    size_t n = 6 + (size_t)(rng.uniform(0.0f, 26.0f));
    std::vector<SwPoint> line = buildRandomPolyline(rng, n, 0.1f);
    std::vector<SwPoint> gpuOut;
    if (!disp.dispatch(0.0f, line, gpuOut) || gpuOut.size() != n) { dispatchOk = false; continue; }
    for (size_t i = 0; i < n; ++i) {
      const float* gf = reinterpret_cast<const float*>(&gpuOut[i]) + 4;  // Rotation lanes
      const float* inf = reinterpret_cast<const float*>(&line[i]) + 4;
      for (int k = 0; k < 4; ++k) cmp.add(gf[k], inf[k], inf, 4, k, -1.0f, "identity-amount0");
    }
  }
  cmp.print();
  return dispatchOk && cmp.verdict();
}
// Compact SwPoint literal: Position=(x,y,z), Scale=(scaleX,1,1) (scaleX==NaN -> dead sentinel).
SwPoint mkPt(float x, float y, float z, float scaleX) {
  SwPoint p{};
  p.Position = SW_PACKED3{x, y, z};
  p.Scale = SW_PACKED3{scaleX, 1.0f, 1.0f};
  return p;
}

// QUIRK PROBE 1: NO-WRITEBACK on the three early-return conditions (dead/isolated/degenerate).
bool checkNoWritebackQuirk(const ReorientDispatch& disp) {
  const float kNaN = std::numeric_limits<float>::quiet_NaN();
  bool ok = true;
  auto checkOne = [&](const char* label, std::vector<SwPoint> line, uint32_t idx) {
    std::vector<SwPoint> gpuOut;
    bool dispatchOk = disp.dispatch(1.0f, line, gpuOut) && gpuOut.size() == line.size();
    std::vector<SwPoint> inPadded(line.size() + 1);
    for (size_t i = 0; i < line.size(); ++i) inPadded[i] = line[i];
    inPadded[line.size()] = SwPoint{};
    inPadded[line.size()].Scale.x = std::numeric_limits<float>::quiet_NaN();
    SwPoint refPreseeded = line[idx];  // CORRECT usage: preseed out[idx]=in[idx] (documented contract)
    mathv_ref::reorientLinePointsOne(inPadded.data(), &refPreseeded, idx, (uint32_t)line.size(), 1.0f);
    SwPoint refTrap{};  // TRAP usage: zero-init, no preseed (the oracle's own documented gotcha)
    mathv_ref::reorientLinePointsOne(inPadded.data(), &refTrap, idx, (uint32_t)line.size(), 1.0f);
    bool gpuMatchesIn = dispatchOk && std::memcmp(&gpuOut[idx], &line[idx], sizeof(SwPoint)) == 0;
    bool refPreseededOk = std::memcmp(&refPreseeded, &line[idx], sizeof(SwPoint)) == 0;
    bool trapDiverges = std::memcmp(&refTrap, &line[idx], sizeof(SwPoint)) != 0;
    printf("[mathv-reorientlinepoints-no-writeback] %s: gpu-copy-through=%s ref-preseeded=%s "
           "trap-diverges(expected,not-gated)=%s\n",
           label, gpuMatchesIn ? "yes" : "no", refPreseededOk ? "yes" : "no",
           trapDiverges ? "yes" : "no");
    return dispatchOk && gpuMatchesIn && refPreseededOk;
  };
  // (a) dead point: index 1 of a 3-line, Scale.x=NaN, live neighbours either side. Rotation set to a
  // non-trivial value so the copy-through assertion isn't vacuously true on an already-zero field.
  std::vector<SwPoint> deadLine = {mkPt(0, 0, 0, 1), mkPt(0, 0, 0, kNaN), mkPt(2, 0, 0, 1)};
  deadLine[1].Rotation = SW_FLOAT4{0.1f, 0.2f, 0.3f, 0.9f};
  ok &= checkOne("dead-point", deadLine, 1);
  // (b) isolated point: index 1 flanked by dead sentinels both sides -> prevIndex==nextIndex==1.
  ok &= checkOne("isolated-point", {mkPt(9, 9, 9, kNaN), mkPt(1, 1, 1, 1), mkPt(-9, -9, -9, kNaN)}, 1);
  // (c) degenerate coincident neighbours: prev/next positions identical -> l < 0.0001.
  ok &= checkOne("degenerate-neighbours", {mkPt(3, 3, 3, 1), mkPt(5, 5, 5, 1), mkPt(3, 3, 3, 1)}, 1);
  return ok;
}

// QUIRK PROBE 2: OOB-READ at the last point.
bool checkOobLastPointQuirk(const ReorientDispatch& disp) {
  std::vector<SwPoint> line = {mkPt(0, 0, 0, 1), mkPt(2, 0, 0, 1), mkPt(4, 0, 0, 1)};
  for (auto& p : line) p.Rotation = SW_FLOAT4{0.0f, 0.0f, 0.0f, 1.0f};
  const float amount = 1.0f;
  std::vector<SwPoint> gpuOut;
  bool dispatchOk = disp.dispatch(amount, line, gpuOut) && gpuOut.size() == 3;
  // ref at index 2 (the last point) with a caller-chosen padding slot[3] — mirrors the oracle's
  // count+1 contract (mathv_ref_reorientlinepoints.h :216-221).
  auto refAt2 = [&](SwPoint padTail) {
    std::vector<SwPoint> pad = {line[0], line[1], line[2], padTail};
    SwPoint out = line[2];
    mathv_ref::reorientLinePointsOne(pad.data(), &out, 2, 3, amount);
    return out;
  };
  const float kNaN = std::numeric_limits<float>::quiet_NaN();
  SwPoint refNaNPad = refAt2(mkPt(0, 0, 0, kNaN));                // gate GREEN vs GPU
  SwPoint refRealPad = refAt2(mkPt(100.0f, 50.0f, -25.0f, 1.0f));  // NAMED FORK vs GPU (far-off point)
  auto q4 = [](const SwPoint& p) { return reinterpret_cast<const float*>(&p) + 4; };
  bool gateOk = dispatchOk;
  bool namedForkDiverges = false;
  for (int k = 0; k < 4; ++k) {
    gateOk &= std::fabs(q4(gpuOut[2])[k] - q4(refNaNPad)[k]) < 1e-5f;
    if (std::fabs(q4(refRealPad)[k] - q4(refNaNPad)[k]) > 1e-4f) namedForkDiverges = true;
  }
  printf("[mathv-reorientlinepoints-oob-last-point] gpu==ref(pad=NaN)[gate]=%s "
         "ref(pad=real)!=ref(pad=NaN)[NAMED FORK, expect yes]=%s\n",
         gateOk ? "yes" : "RED", namedForkDiverges ? "yes" : "no");
  return gateOk && namedForkDiverges;
}

}  // namespace

int runMathvReorientLinePointsSelfTest(bool injectBug) {
  ParityHarness h;
  if (!h.ok()) {
    printf("[selftest-mathv-reorientlinepoints] FAIL: no metallib\n");
    return 1;
  }
  ReorientDispatch disp(h.dev, h.queue, h.lib);
  if (!disp.ok) {
    printf("[selftest-mathv-reorientlinepoints] FAIL: no reorientlinepoints kernel\n");
    return 1;
  }
  if (injectBug) {
    bool pass = checkMainTooth(disp, /*injectBug=*/true, nullptr, nullptr);
    return mathv::mathvVerdictToExit(pass, true, "reorientlinepoints");
  }
  std::map<std::string, int> lookatCov, slerpCov;
  bool passMain = checkMainTooth(disp, false, &lookatCov, &slerpCov);
  bool passIdentity = checkIdentityAmountZeroTooth(disp);
  bool passNoWriteback = checkNoWritebackQuirk(disp);
  bool passOob = checkOobLastPointQuirk(disp);
  printf("[mathv-reorientlinepoints] qLookAt branch coverage:\n");
  for (const auto& kv : lookatCov) printf("  %s: %d\n", kv.first.c_str(), kv.second);
  printf("[mathv-reorientlinepoints] qSlerp branch coverage:\n");
  for (const auto& kv : slerpCov) printf("  %s: %d\n", kv.first.c_str(), kv.second);
  bool covOk = (int)lookatCov.size() >= 3 && (int)slerpCov.size() >= 3;
  printf("[mathv-reorientlinepoints] coverage gate: qLookAt %zu/4, qSlerp %zu/5 -> %s\n",
         lookatCov.size(), slerpCov.size(), covOk ? "ok" : "RED");
  ParityReport rep("selftest-mathv-reorientlinepoints");
  rep.expectTrue("rotation+passthrough(main random-line fuzz, quat-branch tagged)", passMain,
                 passMain ? 1.0 : 0.0);
  rep.expectTrue("identity(Amount=0, vs-input pinned)", passIdentity, passIdentity ? 1.0 : 0.0);
  rep.expectTrue("quirk(no-writeback early-return x3, pinned parity — RED=regression)",
                 passNoWriteback, passNoWriteback ? 1.0 : 0.0);
  rep.expectTrue("quirk(OOB last-point, guard invariant pinned + NAMED FORK documented)", passOob,
                 passOob ? 1.0 : 0.0);
  rep.expectTrue("quatBranchCoverage(qLookAt>=3/4, qSlerp>=3/5 buckets hit)", covOk,
                 covOk ? 1.0 : 0.0);
  return rep.finish();
}
// order 1005: appends after mathv-clearsomepoints/blendpoints/snaptopoints/snappointstogrid (1003-1004).
REGISTER_SELFTESTS(/*orderBase=*/1005,
                   {"mathv-reorientlinepoints", runMathvReorientLinePointsSelfTest});
}  // namespace sw
