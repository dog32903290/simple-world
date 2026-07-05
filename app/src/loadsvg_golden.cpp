// loadsvg_golden — --selftest-loadsvg / -bug. HERMETIC, impl-independent point golden for LoadSvg
// (svg_parse consumer: .svg → flattened polylines → SwPoint list via the pointlist String-path channel).
//
// The pointlist String channel is FLAT-cook only (fork-pointlist-flat-only-no-resident), so this golden
// drives the op's cook fn DIRECTLY via a hand-built PointListCookCtx (the SAME shape the flat cook driver
// builds: inputStrings=[Path], params={Scale, CenterToBounds, ...}). The load-bearing path
// (svgParsePolylinesFromFile → nanosvg parse → flatten → (x,1-y)*scale emit) runs verbatim.
//
// IMPL-INDEPENDENCE (GOLDEN_STANDARD 三特徵 #1): the fixtures use ONLY STRAIGHT segments (M/L, rect) —
// where nanosvg's curve tessellator NEVER runs, so the emitted vertices ARE the authored path vertices
// exactly. The expected positions are HAND-DERIVED from LoadSvg.cs:124 (Position = (Vector3(x, 1-y, 0) +
// centerOffset) * scale), NOT from sw's own output. Curve geometry is a named fork (fork-svg-flatten-
// algorithm) and is deliberately NOT asserted.
//
// SAMPLING OFF-IDENTITY (三特徵 #2): Scale=3 (≠1) and probes at (2,2) (y≠0, so the Y-flip 1-y=-1 is
// exercised, not the y=0 fixed point). If the op dropped the Y-flip (used y instead of 1-y) or dropped
// the scale, these asserts go RED — the body is genuinely tested.
//
// BUG leg (-bug): pointListInjectBug() makes the REAL cook CLEAR its output → 0 points → every count/
// value assertion fails → return 1. No expected-value inversion; the actual cook path is bitten. Did-not-
// trip → return 0 (P1: NO-BITE list catches a dead tooth).
#include <unistd.h>  // getpid

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <system_error>
#include <vector>

#include "runtime/pointlist_op_registry.h"  // PointListCookCtx / findPointListOp / pointListInjectBug
#include "runtime/selftest_registry.h"      // REGISTER_SELFTESTS
#include "runtime/tixl_point.h"             // SwPoint (64B)

namespace sw {
namespace {

bool nearf(float a, float b, float t = 1e-4f) { return std::fabs(a - b) < t; }
bool isSep(const SwPoint& p) { return std::isnan(p.Scale.x); }

// An OPEN 3-vertex straight path: M 0 0 L 2 0 L 2 2 (no Z → closed=0, 3 vertices, no closing dup).
const char* const kOpenPathSvg =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"4\" height=\"4\">"
    "<path d=\"M 0 0 L 2 0 L 2 2\" fill=\"none\" stroke=\"black\"/></svg>";

// A rect (closed): x=1 y=2 w=3 h=4 → nanosvg emits 5 verts with the first repeated at the end (its own
// figure-close, which matches LoadSvg's manual NeedsClosing append — fork-svg-nanosvg-closes-figure).
const char* const kRectSvg =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10\" height=\"10\">"
    "<rect x=\"1\" y=\"2\" width=\"3\" height=\"4\" fill=\"black\"/></svg>";

// The SAME open path but on a TIGHT 2×2 canvas (== the path's content extent x:0..2, y:0..2). Used for
// the CenterToBounds leg so canvas-bounds == content-bounds and the offset matches TiXL (fork-svg-bounds-
// are-canvas coincides here). bounds = (2,2) → centerOffset = (-1, +1).
const char* const kTightOpenPathSvg =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"2\" height=\"2\">"
    "<path d=\"M 0 0 L 2 0 L 2 2\" fill=\"none\" stroke=\"black\"/></svg>";

std::filesystem::path writeTempSvg(const char* content, const char* stem) {
  std::error_code ec;
  std::filesystem::path base = std::filesystem::temp_directory_path(ec);
  if (ec) return {};
  std::filesystem::path file = base / (std::string(stem) + "_" + std::to_string(::getpid()) + ".svg");
  std::ofstream o(file, std::ios::binary);
  if (!o.is_open()) return {};
  o << content;
  o.close();
  return file;
}

// Drive LoadSvg's cook fn directly (flat cook-driver shape). inputStrings=[path]; params overrides.
std::vector<SwPoint> cookLoadSvg(const std::string& path, std::map<std::string, float> params,
                                 bool injectBug) {
  const PointListCookFn* fn = findPointListOp("LoadSvg");
  if (!fn || !*fn) return {};
  std::vector<std::string> inputs{path};
  std::vector<SwPoint> out;
  PointListCookCtx ctx{};
  ctx.inputStrings = &inputs;
  ctx.params = &params;
  ctx.output = &out;
  pointListInjectBug() = injectBug;
  (*fn)(ctx);
  pointListInjectBug() = false;
  return out;
}

int runLoadSvgGoldenSelfTest(bool injectBug) {
  bool ok = true;
  std::filesystem::path openFile = writeTempSvg(kOpenPathSvg, "sw_svg_open");
  std::filesystem::path rectFile = writeTempSvg(kRectSvg, "sw_svg_rect");
  std::filesystem::path tightFile = writeTempSvg(kTightOpenPathSvg, "sw_svg_tight");

  // ===== LEG 1 — open path, Scale=3, no center. HAND-DERIVED positions (LoadSvg.cs:124). =====
  // Position = (x, 1 - y, 0) * 3:
  //   (0,0) → (0, 1, 0)*3 = (0, 3, 0)
  //   (2,0) → (2, 1, 0)*3 = (6, 3, 0)
  //   (2,2) → (2,-1, 0)*3 = (6,-3, 0)     ← y≠0 exercises the Y-flip (1-y = -1), off the fixed point.
  // 3 points + 1 trailing separator = 4 elements.
  {
    std::vector<SwPoint> got =
        cookLoadSvg(openFile.string(), {{"Scale", 3.0f}, {"CenterToBounds", 0.0f}}, injectBug);
    const float expPos[3][3] = {{0, 3, 0}, {6, 3, 0}, {6, -3, 0}};
    bool pass = got.size() == 4 && isSep(got[3]);  // 3 verts + separator
    if (pass) {
      for (int i = 0; i < 3; ++i) {
        const SwPoint& p = got[(size_t)i];
        pass = pass && nearf(p.Position.x, expPos[i][0]) && nearf(p.Position.y, expPos[i][1]) &&
               nearf(p.Position.z, expPos[i][2]) && nearf(p.FX1, 1.0f) &&
               nearf(p.Color.x, 1.0f) && nearf(p.Color.w, 1.0f);  // fork-svg-color-white
      }
    }
    ok = ok && pass;
    std::printf("[selftest-loadsvg] LEG1 open Scale=3 n=%zu want=4(3+sep) p2=(%.2f,%.2f) -> %s\n",
                got.size(), got.size() > 2 ? got[2].Position.x : -9.0f,
                got.size() > 2 ? got[2].Position.y : -9.0f, pass ? "PASS" : "FAIL");
  }

  // ===== LEG 2 — rect, Scale=1, no center. Closed figure: 5 verts (start dup at end) + separator. =====
  // rect x=1 y=2 w=3 h=4 → corners (1,2)(4,2)(4,6)(1,6) then back to (1,2). Position = (x, 1-y, 0)*1:
  //   (1,2)→(1,-1); (4,2)→(4,-1); (4,6)→(4,-5); (1,6)→(1,-5); (1,2)→(1,-1)   (Y-flip only, scale 1)
  {
    std::vector<SwPoint> got =
        cookLoadSvg(rectFile.string(), {{"Scale", 1.0f}, {"CenterToBounds", 0.0f}}, injectBug);
    const float expPos[5][2] = {{1, -1}, {4, -1}, {4, -5}, {1, -5}, {1, -1}};
    bool pass = got.size() == 6 && isSep(got[5]);  // 5 verts (closed) + separator
    if (pass) {
      for (int i = 0; i < 5; ++i) {
        const SwPoint& p = got[(size_t)i];
        pass = pass && nearf(p.Position.x, expPos[i][0]) && nearf(p.Position.y, expPos[i][1]);
      }
      // Closing dup: first == last vertex position (fork-svg-nanosvg-closes-figure).
      pass = pass && nearf(got[0].Position.x, got[4].Position.x) &&
             nearf(got[0].Position.y, got[4].Position.y);
    }
    ok = ok && pass;
    std::printf("[selftest-loadsvg] LEG2 rect closed n=%zu want=6(5+sep) close-dup=%d -> %s\n",
                got.size(),
                got.size() >= 5 ? (int)(nearf(got[0].Position.x, got[4].Position.x)) : -1,
                pass ? "PASS" : "FAIL");
  }

  // ===== LEG 3 — CenterToBounds on the TIGHT-canvas path (bounds=2×2, Scale=1). ==
  // centerOffset = (-w/2, +h/2) = (-1, +1). Position = (x - 1, (1 - y) + 1, 0):
  //   (0,0)→(-1, 2); (2,0)→(1, 2); (2,2)→(1, 0). Canvas==content here so this MATCHES TiXL's
  //   content-bounds offset (fork-svg-bounds-are-canvas coincides — see the op header).
  {
    std::vector<SwPoint> got =
        cookLoadSvg(tightFile.string(), {{"Scale", 1.0f}, {"CenterToBounds", 1.0f}}, /*injectBug=*/false);
    const float expPos[3][2] = {{-1, 2}, {1, 2}, {1, 0}};
    bool pass = got.size() == 4;
    if (pass)
      for (int i = 0; i < 3; ++i)
        pass = pass && nearf(got[(size_t)i].Position.x, expPos[i][0]) &&
               nearf(got[(size_t)i].Position.y, expPos[i][1]);
    ok = ok && pass;
    std::printf("[selftest-loadsvg] LEG3 center offset=(-2,+2) n=%zu p0=(%.2f,%.2f) -> %s\n",
                got.size(), got.size() ? got[0].Position.x : -9.0f,
                got.size() ? got[0].Position.y : -9.0f, pass ? "PASS" : "FAIL");
  }

  // ===== LEG 4 — empty path → empty list (hermetic, not bug-dependent). =====
  {
    std::vector<SwPoint> got = cookLoadSvg("", {{"Scale", 1.0f}}, /*injectBug=*/false);
    bool pass = got.empty();
    ok = ok && pass;
    std::printf("[selftest-loadsvg] LEG4 empty path n=%zu want=0 -> %s\n", got.size(),
                pass ? "PASS" : "FAIL");
  }

  std::error_code ec;
  if (!openFile.empty()) std::filesystem::remove(openFile, ec);
  if (!rectFile.empty()) std::filesystem::remove(rectFile, ec);
  if (!tightFile.empty()) std::filesystem::remove(tightFile, ec);

  if (injectBug) {
    if (ok) {
      std::printf("[selftest-loadsvg] injectBug did not trip (cook unchanged)\n");
      return 0;  // dead tooth → exit 0 (P1: NO-BITE list catches it)
    }
    std::printf("[selftest-loadsvg] injectBug correctly RED (REAL cook cleared → counts/values diverged)\n");
    return 1;
  }
  std::printf("[selftest-loadsvg] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace

REGISTER_SELFTESTS(/*orderBase=*/620, {"loadsvg", runLoadSvgGoldenSelfTest});

}  // namespace sw
