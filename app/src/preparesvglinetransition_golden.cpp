// preparesvglinetransition_golden — --selftest-preparesvglinetransition / -bug. HERMETIC, closed-form
// point golden for PrepareSvgLineTransition (a PURE point-list transform: measures separator-delimited
// polyline segments → writes a stroke-animation progress into each point's F1).
//
// No SVG parse (the op is pure math on a PointList). The golden hand-builds a KNOWN two-segment input
// list and drives the op's cook fn directly via a PointListCookCtx (inputLists=[the built list]) — the
// SAME shape the flat cook driver builds. The load-bearing measure+write passes run verbatim.
//
// CLOSED-FORM ORACLE (GOLDEN_STANDARD 三特徵 #1) — hand-derived from PrepareSvgLineTransition.cs:99-158,
// SpreadMode=UseStrokeLength(1), Spread=0 (so range == packedRange, the deterministic branch — no .NET
// Random drawn, fork-preparesvg-random-net never exercised). INPUT = two horizontal segments separated
// by a NaN-Scale separator:
//   seg0: (0,0,0)→(1,0,0)          [segLen 1]   ┐
//   SEPARATOR                                    ├─ 2 segments, totalLength=4, maxLength=3
//   seg1: (0,1,0)→(3,1,0)          [segLen 3]   ┘
//   SEPARATOR
// distStep = maxLength/(segCount-1) = 3/1 = 3.
//   seg0 (idx 0): packedStart=(0)/3=0, packedDuration=segLen/maxLen=1/3 → F1 = lerp(0,1/3,t):
//                 point0 (t=0) → 0 ; point1 (t=1) → 1/3 (≈0.33333)
//   seg1 (idx 1): pGrid=3, anchor=3 → packedStart=0, packedDuration=3/3=1 → F1 = lerp(0,1,t):
//                 point0 (t=0) → 0 ; point1 (t=1) → 1
// EXPECTED F1 stream: [0, 0.33333, (sep passthrough), 0, 1, (sep passthrough)].
//
// OFF-IDENTITY SAMPLING (三特徵 #2): the two segments have DIFFERENT lengths (1 vs 3) so the maxLength /
// distStep math is genuinely exercised (equal lengths would hide the packedStart term). If the op used
// point INDEX instead of measured LENGTH, or dropped the maxLength normalization, seg1's endpoint would
// not land on exactly 1.0 and point1-of-seg0 not on 1/3 → RED.
//
// BUG leg (-bug): pointListInjectBug() makes the REAL cook CLEAR its output → 0 points → the count/F1
// assertions fail → return 1. Did-not-trip → return 0 (P1).
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include "runtime/pointlist_op_registry.h"  // PointListCookCtx / findPointListOp / pointListInjectBug / swPointDefault
#include "runtime/selftest_registry.h"      // REGISTER_SELFTESTS
#include "runtime/tixl_point.h"             // SwPoint (64B)

namespace sw {
namespace {

bool nearf(float a, float b, float t = 1e-4f) { return std::fabs(a - b) < t; }

SwPoint at(float x, float y, float z) {
  SwPoint p = swPointDefault();
  p.Position = {x, y, z};
  return p;
}
SwPoint sep() {
  SwPoint p = swPointDefault();
  p.Scale = {std::nanf(""), std::nanf(""), std::nanf("")};
  return p;
}

// Drive PrepareSvgLineTransition's cook fn directly. inputLists=[list]; params overrides.
std::vector<SwPoint> cookPrepare(const std::vector<SwPoint>& in, std::map<std::string, float> params,
                                 bool injectBug) {
  const PointListCookFn* fn = findPointListOp("PrepareSvgLineTransition");
  if (!fn || !*fn) return {};
  std::vector<std::vector<SwPoint>> inputLists{in};
  std::vector<SwPoint> out;
  PointListCookCtx ctx{};
  ctx.inputLists = &inputLists;
  ctx.params = &params;
  ctx.output = &out;
  pointListInjectBug() = injectBug;
  (*fn)(ctx);
  pointListInjectBug() = false;
  return out;
}

int runPrepareSvgLineTransitionGoldenSelfTest(bool injectBug) {
  bool ok = true;

  // Two-segment input (see header derivation).
  std::vector<SwPoint> in;
  in.push_back(at(0, 0, 0));
  in.push_back(at(1, 0, 0));
  in.push_back(sep());
  in.push_back(at(0, 1, 0));
  in.push_back(at(3, 1, 0));
  in.push_back(sep());

  // ===== LEG 1 — UseStrokeLength, Spread=0: closed-form F1 stream. =====
  {
    std::vector<SwPoint> got =
        cookPrepare(in, {{"SpreadMode", 1.0f}, {"Spread", 0.0f}}, injectBug);
    // Expected F1 at the 4 real points (indices 0,1,3,4); separators (2,5) pass through untouched.
    bool pass = got.size() == 6;
    if (pass) {
      pass = pass && nearf(got[0].FX1, 0.0f) && nearf(got[1].FX1, 1.0f / 3.0f) &&
             std::isnan(got[2].Scale.x) &&                // separator passthrough
             nearf(got[3].FX1, 0.0f) && nearf(got[4].FX1, 1.0f) && std::isnan(got[5].Scale.x);
    }
    ok = ok && pass;
    std::printf("[selftest-preparesvglinetransition] LEG1 n=%zu F1=[%.4f,%.4f,sep,%.4f,%.4f,sep] "
                "want[0,0.3333,-,0,1,-] -> %s\n",
                got.size(), got.size() > 0 ? got[0].FX1 : -9.0f, got.size() > 1 ? got[1].FX1 : -9.0f,
                got.size() > 3 ? got[3].FX1 : -9.0f, got.size() > 4 ? got[4].FX1 : -9.0f,
                pass ? "PASS" : "FAIL");
  }

  // ===== LEG 2 — < 2 segments → passthrough (F1 untouched). One segment only. =====
  {
    std::vector<SwPoint> oneSeg;
    oneSeg.push_back(at(0, 0, 0));
    oneSeg.push_back(at(1, 0, 0));
    oneSeg.push_back(sep());
    // Seed a distinctive F1 so passthrough is observable (default swPointDefault F1=1).
    oneSeg[0].FX1 = 7.0f;
    std::vector<SwPoint> got =
        cookPrepare(oneSeg, {{"SpreadMode", 1.0f}, {"Spread", 0.0f}}, /*injectBug=*/false);
    bool pass = got.size() == 3 && nearf(got[0].FX1, 7.0f);  // untouched (bailed: <2 segments)
    ok = ok && pass;
    std::printf("[selftest-preparesvglinetransition] LEG2 <2-seg passthrough n=%zu F1[0]=%.2f want=7 "
                "-> %s\n",
                got.size(), got.size() ? got[0].FX1 : -9.0f, pass ? "PASS" : "FAIL");
  }

  // ===== LEG 3 — empty input → empty (hermetic). =====
  {
    std::vector<SwPoint> got = cookPrepare({}, {{"SpreadMode", 1.0f}}, /*injectBug=*/false);
    bool pass = got.empty();
    ok = ok && pass;
    std::printf("[selftest-preparesvglinetransition] LEG3 empty n=%zu want=0 -> %s\n", got.size(),
                pass ? "PASS" : "FAIL");
  }

  if (injectBug) {
    if (ok) {
      std::printf("[selftest-preparesvglinetransition] injectBug did not trip (cook unchanged)\n");
      return 0;  // dead tooth → exit 0 (P1)
    }
    std::printf("[selftest-preparesvglinetransition] injectBug correctly RED (REAL cook cleared → "
                "F1/counts diverged)\n");
    return 1;
  }
  std::printf("[selftest-preparesvglinetransition] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace

REGISTER_SELFTESTS(/*orderBase=*/621, {"preparesvglinetransition", runPrepareSvgLineTransitionGoldenSelfTest});

}  // namespace sw
