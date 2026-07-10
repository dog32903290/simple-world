// selftests_mathv_reorientlinepoints.cpp — --selftest-mathv-reorientlinepoints (D role, fuzz driver).
// MATH_VERIFY_WORKFLOW.md 量產鏈 Tier-H: fuzz GPU "reorientlinepoints" vs the R-authored CPU oracle
// (mathv_ref_reorientlinepoints.h + mathv_ref_shared_quat.h, TRANSCRIBED from external/tixl HLSL) via
// direct-kernel dispatch (§1.3). NOT the generic mathv_harness.h MathvCase runner: output at index i
// depends on its NEIGHBOURS' Position, not just its own element, so the generic per-element
// `ref(P,e,out)` signature can't carry it — every tooth below is a direct Comparator batch
// (addnoise.cpp's TOOTH ROTATION shape, as the whole test, not a PRIMARY-layer supplement).
//
// Shared dispatch adapter + corpus/classification helpers live in
// selftests_mathv_reorientlinepoints_shared.h (§1.4 split — the qSlerp clamp-hi/-lo coverage tooth
// added 2026-07-10, selftests_mathv_reorientlinepoints_probes.cpp, didn't fit this file's own
// 400-line budget).
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
// qSlerp has 6 (clamp-hi, clamp-lo, acos-direct, acos-flipped, linear-direct, linear-flipped).
// classifyQLookAt/classifyQSlerp (shared.h) are INSTRUMENTATION ONLY (replicate the already-oracled
// branch CONDITIONS for bucketing, never substitute for the compared value); every Rotation-lane
// compare is tagged with both. clamp-hi/-lo have no simple closed-form trigger through random fuzz
// alone (measure-zero to land on an exact float boundary by chance) — checkClampBranchTooth (probes
// TU) engineers them deterministically instead and folds its hits into the SAME slerpCov map, so the
// gate below requires the full 6/6 once both sources are merged (MASTER_PLAN.md 接力待辦 ㈢ D "clamp
// 補牙 6-6", 2026-07-10; qLookAt's >=3/4 floor is unchanged by this fix).
//
// QUIRK PROBES: 1. NO-WRITEBACK (ref :119-130) — TiXL's early `return;` leaves ResultPoints[idx]
// UNWRITTEN; reorientlinepoints.metal FORKS to an explicit copy-through. Probed on all 3 early-return
// conditions: GPU copy-through must equal ref-called-with-its-documented-preseed contract
// (`out[i]=in[i]` before the call) — pinned parity. A naive zero-init call (the oracle's documented
// usage TRAP) is shown diverging too — printed, not gated. 2. OOB-READ at the last point (ref
// :132-148, metal NAMED FORK "next-neighbour OOB-read guard") — sw's GPU buffer has no slot `count`,
// so GPU always behaves as if the padding were NaN. ref(padding=NaN)==GPU is the fork's own invariant
// (pinned); ref(padding=a real differing point) reproduces TiXL's literal OOB-read bug and diverges
// from GPU (documented NAMED FORK, sanity-asserted to actually differ, proving the probe fired).
// 3. qSlerp CLAMP-HI/-LO branch coverage (probes TU) — see BRANCH COVERAGE above.
//
// ZONE: shell tier (app/src/ root, mathv support). Crosses runtime only for SwPoint + params ABI.
#include "mathv_harness.h"
#include "mathv_ref_reorientlinepoints.h"
#include "runtime/reorientlinepoints_params.h"
#include "runtime/selftest_registry.h"
#include "runtime/tixl_point.h"
#include "selftests_mathv_reorientlinepoints_shared.h"
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
using mathv::Rng;
using mathv_rlp_shared::buildRandomPolyline;
using mathv_rlp_shared::classifyQLookAt;
using mathv_rlp_shared::classifyQSlerp;
using mathv_rlp_shared::mkPt;
using mathv_rlp_shared::paramTable;
using mathv_rlp_shared::ReorientDispatch;

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
      // batch tag = a STATIC string literal. Comparator::Evidence copies it into a std::string at
      // record time (mathv_compare.h, stack-use-after-scope fix 2026-07-10 — a per-iteration
      // snprintf'd tag used to dangle by print() time), but literals remain the simplest safe choice.
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
  bool passClamp = mathv_rlp_shared::checkClampBranchTooth(disp, &slerpCov);
  printf("[mathv-reorientlinepoints] qLookAt branch coverage:\n");
  for (const auto& kv : lookatCov) printf("  %s: %d\n", kv.first.c_str(), kv.second);
  printf("[mathv-reorientlinepoints] qSlerp branch coverage:\n");
  for (const auto& kv : slerpCov) printf("  %s: %d\n", kv.first.c_str(), kv.second);
  // clamp-hi/-lo used to be "no simple closed-form trigger" (random fuzz alone never reaches an exact
  // float boundary) — checkClampBranchTooth engineers both deterministically and merges its hits into
  // slerpCov above, so all 6 qSlerp buckets are now genuinely reachable and required (MASTER_PLAN.md
  // 接力待辦 ㈢ D "clamp 補牙 6-6", 2026-07-10; qLookAt's >=3/4 floor is unchanged by this fix).
  bool covOk = (int)lookatCov.size() >= 3 && (int)slerpCov.size() == 6;
  printf("[mathv-reorientlinepoints] coverage gate: qLookAt %zu/4, qSlerp %zu/6 -> %s\n",
         lookatCov.size(), slerpCov.size(), covOk ? "ok" : "RED");
  ParityReport rep("selftest-mathv-reorientlinepoints");
  rep.expectTrue("rotation+passthrough(main random-line fuzz, quat-branch tagged)", passMain,
                 passMain ? 1.0 : 0.0);
  rep.expectTrue("identity(Amount=0, vs-input pinned)", passIdentity, passIdentity ? 1.0 : 0.0);
  rep.expectTrue("quirk(no-writeback early-return x3, pinned parity — RED=regression)",
                 passNoWriteback, passNoWriteback ? 1.0 : 0.0);
  rep.expectTrue("quirk(OOB last-point, guard invariant pinned + NAMED FORK documented)", passOob,
                 passOob ? 1.0 : 0.0);
  rep.expectTrue("quirk(qSlerp clamp-hi/-lo, engineered exact-boundary hit)", passClamp,
                 passClamp ? 1.0 : 0.0);
  rep.expectTrue("quatBranchCoverage(qLookAt>=3/4, qSlerp==6/6 buckets hit)", covOk,
                 covOk ? 1.0 : 0.0);
  return rep.finish();
}
// order 1005: appends after mathv-clearsomepoints/blendpoints/snaptopoints/snappointstogrid (1003-1004).
REGISTER_SELFTESTS(/*orderBase=*/1005,
                   {"mathv-reorientlinepoints", runMathvReorientLinePointsSelfTest});
}  // namespace sw
