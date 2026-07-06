// TextToPoints pointlist op — a TiXL-ABSENT sw-original node (柏為 2026-07-06 拍板破例, memory
// baiwei-approved-tixl-absent-glyph-points). Turns a text string (INCLUDING CJK) into a SwPoint point
// cloud sampled from each character's GLYPH OUTLINE, via the runtime/glyph_points leaf seam (CoreText
// impl in platform/glyph_outline.mm, installed by the app). This is the「中文字 as points」MV 承重
// (memory simple-world-real-target-mv-tooling); there is NO TiXL parity oracle (grep of TiXL's tree
// finds no TTF outline parser — LineTextPoints needs an SVG line-font, TextSprites/TextOutlines are
// bitmap/SDF). The golden pins MECHANISM + GEOMETRY INVARIANTS from a hand-built fixture provider, not
// pixel-parity (see texttopoints_golden.cpp).
//
// WHAT THIS OP OWNS (the layout/currency half — the CoreText half lives behind the seam):
//   • Decode the Text String (UTF-8) → codepoints; call the provider per codepoint → GlyphOutline
//     (contours in glyph-local text-space, y UP, fontSize-scaled) + advance.
//   • Lay glyphs out on a pen: cursor.x += (advance + Spacing) after each; '\n' → cursor.x=0,
//     cursor.y -= LineHeight (points down a line). A space (0 contours) still advances the cursor.
//   • Emit each contour vertex as a SwPoint at (cursor + glyphLocal) * Scale (uniform), TiXL Point
//     defaults (F1=1, identity rot, white, unit scale, F2=1). No Y-flip here — CoreText is already
//     y-up like TiXL Point space (unlike LoadSvg's SVG y-down source).
//   • Per-contour Point.Separator() (Scale=NaN) between contours, exactly like LoadSvg between figures,
//     so a downstream consumer sees the list as separator-delimited closed loops (one per stroke/loop).
//
// SAMPLING (SamplesPerContour>0 → resample each contour to that many equal-index points; else keep the
// provider's flattened vertices as-is). Monotone in the knob (more samples = more points). FillMode is
// reserved (0=outline only, the shipped behavior); a future fill sampler is a named non-shipped fork.
//
// FORKS (named): fork-texttopoints-tixl-absent (no oracle — golden = geometry invariants). fork-
// texttopoints-outline-only (fill sampling not shipped). fork-pointlist-flat-only-no-resident /
// fork-pointlist-string-path-channel (identical to LoadSvg: String inputs via inputStrings, flat cook).
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "runtime/glyph_points.h"           // GlyphOutline / glyphOutlineProvider (the leaf seam)
#include "runtime/graph.h"                   // NodeSpec, PortSpec, Widget
#include "runtime/pointlist_op_registry.h"   // PointListOp / PointListCookCtx / pointListInjectBug / swPointDefault
#include "runtime/tixl_point.h"              // SwPoint (the host point currency)

namespace sw {

namespace {

// Minimal UTF-8 → UTF-32 decode (enough for BMP + astral CJK; malformed bytes are skipped as U+FFFD).
void decodeUtf8(const std::string& s, std::vector<uint32_t>& out) {
  size_t i = 0, n = s.size();
  while (i < n) {
    unsigned char c = (unsigned char)s[i];
    uint32_t cp = 0xFFFD;
    int extra = 0;
    if (c < 0x80) { cp = c; extra = 0; }
    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
    else { ++i; out.push_back(0xFFFD); continue; }
    if (i + (size_t)extra >= n) { out.push_back(0xFFFD); break; }
    bool ok = true;
    for (int k = 1; k <= extra; ++k) {
      unsigned char cc = (unsigned char)s[i + k];
      if ((cc & 0xC0) != 0x80) { ok = false; break; }
      cp = (cp << 6) | (cc & 0x3F);
    }
    if (!ok) { ++i; out.push_back(0xFFFD); continue; }
    i += (size_t)extra + 1;
    out.push_back(cp);
  }
}

// Resample a polyline to `target` equally-index-spaced points (by arc length). target<=0 → keep input.
void resampleContour(const std::vector<GlyphPoint2>& in, int target, std::vector<GlyphPoint2>& out) {
  out.clear();
  if (in.empty()) return;
  if (target <= 0) { out = in; return; }
  if (in.size() == 1) { out.assign((size_t)target, in[0]); return; }
  // cumulative arc length
  std::vector<double> cum(in.size(), 0.0);
  for (size_t i = 1; i < in.size(); ++i)
    cum[i] = cum[i - 1] + std::hypot(in[i].x - in[i - 1].x, in[i].y - in[i - 1].y);
  double total = cum.back();
  out.reserve((size_t)target);
  if (total <= 0.0) { out.assign((size_t)target, in[0]); return; }
  for (int k = 0; k < target; ++k) {
    double d = total * (double)k / (double)(target - 1 > 0 ? target - 1 : 1);
    // find segment
    size_t j = 1;
    while (j < in.size() && cum[j] < d) ++j;
    if (j >= in.size()) j = in.size() - 1;
    double seg = cum[j] - cum[j - 1];
    double t = seg > 0.0 ? (d - cum[j - 1]) / seg : 0.0;
    GlyphPoint2 p{(float)(in[j - 1].x + t * (in[j].x - in[j - 1].x)),
                  (float)(in[j - 1].y + t * (in[j].y - in[j - 1].y))};
    out.push_back(p);
  }
}

void cookTextToPoints(PointListCookCtx& c) {
  if (!c.output) return;
  c.output->clear();

  // String inputs (spec port order): [0]=Text, [1]=Font. Absent → empty (no text / default font).
  std::string text, font;
  if (c.inputStrings) {
    if (c.inputStrings->size() > 0) text = (*c.inputStrings)[0];
    if (c.inputStrings->size() > 1) font = (*c.inputStrings)[1];
  }

  const float size = pointListParam(c.params, "Size", 64.0f);
  const float spacing = pointListParam(c.params, "Spacing", 0.0f);
  const float lineHeight = pointListParam(c.params, "LineHeight", size * 1.2f);
  const float scale = pointListParam(c.params, "Scale", 0.01f);  // points → composition units (small)
  const float flatness = pointListParam(c.params, "Flatness", 1.0f);
  const int samplesPerContour = (int)std::lround(pointListParam(c.params, "SamplesPerContour", 0.0f));

  GlyphOutlineFn provider = glyphOutlineProvider();
  if (!provider) {
    // No provider installed (runtime-only build / no fixture) → empty list. (bug hook still honored.)
    if (pointListInjectBug()) c.output->clear();
    return;
  }

  std::vector<uint32_t> cps;
  decodeUtf8(text, cps);
  if (cps.empty()) return;

  double penX = 0.0, penY = 0.0;
  const char* fontC = font.empty() ? nullptr : font.c_str();

  auto emitContour = [&](const std::vector<GlyphPoint2>& pts) {
    if (pts.size() < 2) return;
    std::vector<GlyphPoint2> sampled;
    resampleContour(pts, samplesPerContour, sampled);
    for (const GlyphPoint2& gp : sampled) {
      SwPoint p = swPointDefault();
      p.Position = {(float)((penX + gp.x) * scale), (float)((penY + gp.y) * scale), 0.0f};
      c.output->push_back(p);
    }
    // Trailing separator between contours (LoadSvg convention: Scale=NaN).
    SwPoint sep = swPointDefault();
    sep.Scale = {std::nanf(""), std::nanf(""), std::nanf("")};
    c.output->push_back(sep);
  };

  for (uint32_t cp : cps) {
    if (cp == (uint32_t)'\n') {
      penX = 0.0;
      penY -= (double)lineHeight;
      continue;
    }
    GlyphOutline go;
    if (!provider(cp, fontC, size, flatness, go)) {
      // font creation failed entirely → stop (no glyphs obtainable).
      break;
    }
    for (const GlyphContour& ct : go.contours) emitContour(ct.points);
    penX += (double)go.advance + (double)spacing;
  }

  // Test-only: corrupt the REAL output → CLEAR the whole list. Off in production. Same hook as every leaf.
  if (pointListInjectBug()) c.output->clear();
}

}  // namespace

// Self-registration. ONE PointList output "ResultList" + Text/Font(String, wire-OR-const) + knobs.
// PortSpec positional: {id, name, dataType, isInput, def, minV, maxV, widget, labels, pinless, vecArity,
//                       multiInput, strDef}.
static const PointListOp _reg_texttopoints{
    {"TextToPoints", "TextToPoints",
     {{"ResultList", "ResultList", "PointList", false},
      // Text (String input, wire-OR-const). strDef default gives a visible glyph out of the box.
      {"Text", "Text", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, "永"},
      // Font (String input): system font family/PostScript name OR a font-file path. Empty → default.
      {"Font", "Font", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, "PingFang SC"},
      // Size (glyph point size fed to CoreText). Float knob.
      {"Size", "Size", "Float", true, 64.0f, 1.0f, 512.0f},
      // Spacing (extra pen advance between glyphs, in points). Default 0.
      {"Spacing", "Spacing", "Float", true, 0.0f, -100.0f, 200.0f},
      // LineHeight (pen y-drop per '\n', in points). Default 0 → op fills size*1.2 when left at 0? No:
      // exposed explicit; default here is a typical 1.2·64. User overrides freely.
      {"LineHeight", "LineHeight", "Float", true, 76.8f, 0.0f, 1000.0f},
      // Scale (uniform points → composition-unit factor). Default 0.01 (a 64pt glyph → ~0.64 units).
      {"Scale", "Scale", "Float", true, 0.01f, 0.0001f, 10.0f},
      // Flatness (curve subdivision tolerance in points; smaller = denser). Default 1.
      {"Flatness", "Flatness", "Float", true, 1.0f, 0.05f, 20.0f},
      // SamplesPerContour (0 = keep provider's flattened vertices; >0 = resample each contour to N
      // equal-arc-length points). Monotone in the knob (fork-texttopoints-tixl-absent — no oracle).
      {"SamplesPerContour", "SamplesPerContour", "Float", true, 0.0f, 0.0f, 2000.0f}},
     /*evaluate=*/nullptr},
    cookTextToPoints};

}  // namespace sw
