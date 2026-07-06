// texttopoints_golden — --selftest-texttopoints / -bug. TiXL-ABSENT node (柏為 2026-07-06 拍板破例,
// memory baiwei-approved-tixl-absent-glyph-points): TextToPoints turns text → a SwPoint cloud sampled
// from glyph OUTLINES via the runtime/glyph_points leaf seam (CoreText impl in platform/glyph_outline).
//
// HONESTY (GOLDEN_STANDARD 三特徵, adapted for a TiXL-absent op with NO parity oracle):
//   • The op has TWO halves. The LAYOUT/EMIT half (runtime, this op) is closed-form: pen advance,
//     newline, scale, (penX+x)*scale positioning, per-contour separators, resample. The OUTLINE half
//     (CoreText) has no oracle (TiXL never parses TTF outlines; nanosvg can't either — svg_parse.h
//     fork-svg-no-svgfont). So:
//   • LEGS 1–5 install a HAND-BUILT FIXTURE provider (exact, impl-independent contours: a fake 'I' =
//     one 4-corner rectangle loop; a fake '口' = two nested rectangle loops). Expected positions are
//     HAND-DERIVED from the op's documented math ((penX+localX)*Scale), NOT from sw's own output. This
//     pins the LAYOUT body: if the op dropped the advance, the scale, the newline drop, or a separator,
//     these asserts go RED. (三特徵 #1 impl-independent + #2 off-identity: Scale=2≠1, penX moves off 0,
//     multi-line moves penY off 0.)
//   • LEG 6 is a real-CoreText SMOKE leg on '口' (a real system CJK font): it asserts only GEOMETRY
//     INVARIANTS that hold for ANY correct outline extractor — ≥1 closed contour, first≈last (closure),
//     bbox within the glyph's em/advance, nonzero advance. NOT pixel-parity. Honestly a smoke leg (no
//     numeric oracle exists), per GOLDEN_STANDARD's stateful/emergent honesty rule.
//   • BUG leg (-bug): pointListInjectBug() makes the REAL cook CLEAR its output → 0 points → the fixture
//     legs' count/value asserts fail → return 1. No expected-value inversion; the actual cook path is
//     bitten. Did-not-trip → return 0 (P1: NO-BITE list catches a dead tooth).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "platform/glyph_outline.h"         // real CoreText extractGlyphOutline (LEG 6 smoke)
#include "runtime/glyph_points.h"           // GlyphOutline / setGlyphOutlineProvider (the seam)
#include "runtime/pointlist_op_registry.h"  // PointListCookCtx / findPointListOp / pointListInjectBug
#include "runtime/selftest_registry.h"      // REGISTER_SELFTESTS
#include "runtime/tixl_point.h"             // SwPoint (64B)

namespace sw {
namespace {

bool nearf(float a, float b, float t = 1e-3f) { return std::fabs(a - b) < t; }
bool isSep(const SwPoint& p) { return std::isnan(p.Scale.x); }

// ── Hand-built fixture provider (impl-independent geometry) ────────────────────────────────────────
// Codepoint → known contours + advance, in glyph-local text-space (y UP), fontSize-scaled. Chosen so
// the golden derives every expected SwPoint by hand and never consults sw's output.
//   'I' (U+0049): ONE rectangle loop, corners (0,0)(10,0)(10,60)(0,60), closed (first repeated) → 5
//                 verts. advance = 12.
//   '口' (U+53E3): TWO rectangle loops (outer 0..60, inner 15..45), each 5 verts closed. advance = 64.
//   ' ' (U+0020): ZERO contours, advance = 20 (a space still moves the pen).
// The provider IGNORES fontName/fontSize/flatness (the fixture is already the emitted geometry) — that
// is fine: LEGS 1–5 test the OP's layout, not CoreText. fontSize-independence is intentional so the
// hand-derived expected values are stable.
bool fixtureProvider(uint32_t cp, const char* /*font*/, float /*size*/, float /*flat*/,
                     GlyphOutline& out) {
  out.contours.clear();
  out.advance = 0.0f;
  out.valid = true;
  auto rectLoop = [](float x0, float y0, float x1, float y1) {
    GlyphContour c;
    c.closed = true;
    c.points = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}, {x0, y0}};  // first repeated = closed
    return c;
  };
  if (cp == (uint32_t)'I') {
    out.contours.push_back(rectLoop(0, 0, 10, 60));
    out.advance = 12.0f;
    return true;
  }
  if (cp == 0x53E3 /* 口 */) {
    out.contours.push_back(rectLoop(0, 0, 60, 60));    // outer
    out.contours.push_back(rectLoop(15, 15, 45, 45));  // inner
    out.advance = 64.0f;
    return true;
  }
  if (cp == (uint32_t)' ') {
    out.advance = 20.0f;  // no contours, still advances
    return true;
  }
  // unknown → valid font, no glyph, small advance
  out.advance = 8.0f;
  return true;
}

// Drive TextToPoints' cook fn directly (flat cook-driver shape). inputStrings=[Text, Font]; params.
std::vector<SwPoint> cookTTP(const std::string& text, std::map<std::string, float> params,
                             bool injectBug) {
  const PointListCookFn* fn = findPointListOp("TextToPoints");
  if (!fn || !*fn) return {};
  std::vector<std::string> inputs{text, /*Font=*/"fixture"};
  std::vector<SwPoint> out;
  PointListCookCtx ctx{};
  ctx.inputStrings = &inputs;
  ctx.output = &out;
  ctx.params = &params;
  pointListInjectBug() = injectBug;
  (*fn)(ctx);
  pointListInjectBug() = false;
  return out;
}

// Count separators (Scale.x == NaN).
int countSeps(const std::vector<SwPoint>& v) {
  int n = 0;
  for (const auto& p : v) if (isSep(p)) ++n;
  return n;
}

int runTextToPointsGoldenSelfTest(bool injectBug) {
  bool ok = true;

  // Install the fixture provider for LEGS 1–5 (LEG 6 calls the real extractor directly).
  setGlyphOutlineProvider(&fixtureProvider);

  // ===== LEG 1 — single glyph 'I' (1 loop), Scale=2. =====
  // Provider 'I' = 5 verts (rect, first repeated). Emit at (0+x)*2, (0+y)*2 (penX=0, no flip). Then a
  // trailing separator. Expect: 6 points (5 + sep). Verify positions, closure (first==last), sep.
  {
    std::vector<SwPoint> g = cookTTP("I", {{"Scale", 2.0f}}, injectBug);
    bool pass = g.size() == 6 && !isSep(g[0]) && isSep(g[5]) && countSeps(g) == 1;
    const float exp[5][2] = {{0, 0}, {20, 0}, {20, 120}, {0, 120}, {0, 0}};
    if (pass)
      for (int i = 0; i < 5; ++i)
        pass = pass && nearf(g[(size_t)i].Position.x, exp[i][0]) &&
               nearf(g[(size_t)i].Position.y, exp[i][1]);
    // closure: first vertex == last (repeated) → g[0]==g[4]
    if (pass) pass = nearf(g[0].Position.x, g[4].Position.x) && nearf(g[0].Position.y, g[4].Position.y);
    ok = ok && pass;
    std::printf("[selftest-texttopoints] LEG1 'I' 1-loop Scale=2 n=%zu seps=%d closed=%d -> %s\n",
                g.size(), g.size() ? countSeps(g) : -1,
                g.size() >= 5 ? (int)nearf(g[0].Position.x, g[4].Position.x) : -1,
                pass ? "PASS" : "FAIL");
  }

  // ===== LEG 2 — CJK '口' → TWO contours (outer + inner) → TWO separators. =====
  // Correct outline-loop count is the load-bearing invariant for CJK (口 has an inner hole). If the op
  // dropped a contour or a separator, this goes RED. 5+5 verts + 2 seps = 12 points.
  {
    std::vector<SwPoint> g = cookTTP("\xE5\x8F\xA3", {{"Scale", 1.0f}}, injectBug);  // UTF-8 口
    bool pass = g.size() == 12 && countSeps(g) == 2;
    // outer loop starts at (0,0); inner loop starts at (15,15) — find the 2nd contour's first vertex.
    if (pass) {
      // points: [0..4]=outer, [5]=sep, [6..10]=inner, [11]=sep
      pass = pass && nearf(g[0].Position.x, 0) && nearf(g[0].Position.y, 0) && isSep(g[5]) &&
             nearf(g[6].Position.x, 15) && nearf(g[6].Position.y, 15) && isSep(g[11]);
    }
    ok = ok && pass;
    std::printf("[selftest-texttopoints] LEG2 '口' 2-loop n=%zu seps=%d inner0=(%.1f,%.1f) -> %s\n",
                g.size(), g.size() ? countSeps(g) : -1, g.size() > 6 ? g[6].Position.x : -9.0f,
                g.size() > 6 ? g[6].Position.y : -9.0f, pass ? "PASS" : "FAIL");
  }

  // ===== LEG 3 — advance/layout: "II" → 2nd glyph offset by advance(12)+Spacing(3)=15. =====
  // Scale=1. First 'I' at penX=0; second at penX=15. So the second loop's first vertex x = (15+0)*1=15.
  // If the op ignored the advance or the spacing, this goes RED (off-identity: penX≠0).
  {
    std::vector<SwPoint> g = cookTTP("II", {{"Scale", 1.0f}, {"Spacing", 3.0f}}, injectBug);
    // [0..4] glyph0 loop, [5] sep, [6..10] glyph1 loop, [11] sep
    bool pass = g.size() == 12 && countSeps(g) == 2;
    if (pass) pass = nearf(g[0].Position.x, 0) && nearf(g[6].Position.x, 15) && nearf(g[7].Position.x, 25);
    ok = ok && pass;
    std::printf("[selftest-texttopoints] LEG3 'II' adv+spacing g1.x0=%.1f (want 15) -> %s\n",
                g.size() > 6 ? g[6].Position.x : -9.0f, pass ? "PASS" : "FAIL");
  }

  // ===== LEG 4 — newline: "I\nI" → 2nd line penX=0, penY -= LineHeight(50). Scale=1. =====
  // Second glyph's loop y-values are shifted DOWN by 50: first vertex (0, 0-50)=(0,-50).
  {
    std::vector<SwPoint> g = cookTTP("I\nI", {{"Scale", 1.0f}, {"LineHeight", 50.0f}}, injectBug);
    bool pass = g.size() == 12 && countSeps(g) == 2;
    if (pass) pass = nearf(g[0].Position.y, 0) && nearf(g[6].Position.x, 0) && nearf(g[6].Position.y, -50);
    ok = ok && pass;
    std::printf("[selftest-texttopoints] LEG4 'I\\nI' line2 y0=%.1f (want -50) -> %s\n",
                g.size() > 6 ? g[6].Position.y : -9.0f, pass ? "PASS" : "FAIL");
  }

  // ===== LEG 5 — SamplesPerContour monotonicity: 'I' resampled to 20 pts/contour. =====
  // One contour → 20 sampled verts + 1 sep = 21 points (vs LEG1's 6 with samples=0). Monotone in knob.
  {
    std::vector<SwPoint> g8 = cookTTP("I", {{"Scale", 1.0f}, {"SamplesPerContour", 8.0f}}, false);
    std::vector<SwPoint> g20 = cookTTP("I", {{"Scale", 1.0f}, {"SamplesPerContour", 20.0f}}, false);
    // 8 samples + 1 sep = 9 ; 20 + 1 sep = 21 ; strictly more with higher knob.
    bool pass = g8.size() == 9 && g20.size() == 21 && g20.size() > g8.size();
    // resampled endpoints still start at the contour start (0,0) and stay on the fixture rectangle.
    if (pass) pass = nearf(g20[0].Position.x, 0) && nearf(g20[0].Position.y, 0);
    ok = ok && pass;
    std::printf("[selftest-texttopoints] LEG5 resample n8=%zu n20=%zu monotone=%d -> %s\n",
                g8.size(), g20.size(), (int)(g20.size() > g8.size()), pass ? "PASS" : "FAIL");
  }

  // ===== LEG 6 — REAL CoreText SMOKE on '口' (中文實測). Geometry invariants only (no oracle). =====
  // Direct platform call (the golden may include platform — it lives in app/src root, not a zone dir).
  // Try a few common CJK system fonts; assert invariants that hold for ANY correct extractor.
  {
    const char* fonts[] = {"PingFang SC", "STHeiti", "Songti SC", "Heiti SC", "STSong", ""};
    bool got = false;
    platform::PlatformGlyphOutline po;
    for (const char* fn : fonts) {
      platform::PlatformGlyphOutline p;
      if (platform::extractGlyphOutline(0x53E3 /*口*/, fn, 100.0f, 1.0f, p) && !p.contours.empty()) {
        po = std::move(p);
        got = true;
        break;
      }
    }
    bool pass = true;  // skip (no CJK font in env) is non-fatal; a FOUND glyph MUST pass its invariants.
    if (got) {
      // (a) advance is a sane fraction of em (100pt) — CJK ≈ full em; allow a wide sane band.
      pass = pass && po.advance > 20.0f && po.advance < 200.0f;
      // (b) ≥1 contour, each closed (first≈last) and non-trivial; compute bbox.
      float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
      bool anyClosed = false;
      for (const auto& c : po.contours) {
        if (c.xy.size() < 8) continue;  // <4 points = degenerate
        float fx = c.xy[0], fy = c.xy[1];
        float lx = c.xy[c.xy.size() - 2], ly = c.xy[c.xy.size() - 1];
        if (nearf(fx, lx, 1.0f) && nearf(fy, ly, 1.0f)) anyClosed = true;
        for (size_t i = 0; i + 1 < c.xy.size(); i += 2) {
          minx = std::fmin(minx, c.xy[i]);
          maxx = std::fmax(maxx, c.xy[i]);
          miny = std::fmin(miny, c.xy[i + 1]);
          maxy = std::fmax(maxy, c.xy[i + 1]);
        }
      }
      float w = maxx - minx, h = maxy - miny;
      // (c) bbox within a generous em box (glyph fits, not exploded), and reasonably sized.
      bool bboxOk = w > 10.0f && w < 200.0f && h > 10.0f && h < 200.0f;
      pass = pass && anyClosed && bboxOk;
      std::printf("[selftest-texttopoints] LEG6 real '口' contours=%zu adv=%.1f bbox=%.1fx%.1f "
                  "closed=%d -> %s\n",
                  po.contours.size(), po.advance, w, h, (int)anyClosed, pass ? "PASS" : "FAIL");
    } else {
      // No CJK font available in this environment → do NOT fail the whole golden (env-dependent), but
      // report it. The fixture legs already exercise the op body; LEG 6 is the real-font smoke bonus.
      std::printf("[selftest-texttopoints] LEG6 real '口' SKIPPED (no CJK system font found) — "
                  "fixture legs still gate the op body\n");
    }
    ok = ok && pass;  // FOUND → invariants gate; SKIPPED → pass=true (env-dependent, non-fatal).
  }

  // Reset the provider so we don't leak the fixture into any later selftest in the same process.
  setGlyphOutlineProvider(nullptr);

  if (injectBug) {
    if (ok) {
      std::printf("[selftest-texttopoints] injectBug did not trip (cook unchanged)\n");
      return 0;  // dead tooth → exit 0 (P1: NO-BITE list catches it)
    }
    std::printf("[selftest-texttopoints] injectBug correctly RED (REAL cook cleared → diverged)\n");
    return 1;
  }
  std::printf("[selftest-texttopoints] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace

REGISTER_SELFTESTS(/*orderBase=*/688, {"texttopoints", runTextToPointsGoldenSelfTest});

}  // namespace sw
