// LoadSvgAsTexture2D image-SOURCE op — parse an .svg and RASTERIZE it into the op's own resolution-pinned
// RGBA8 output texture. TiXL authority: external/tixl/Operators/Lib/image/generate/load/LoadSvgAsTexture2D.cs
// (svgDocument.Draw(width,height) → System.Drawing.Bitmap → RGBA Texture2D, :106-139). sw rasterizes with
// nanosvgrast (svg_parse) to a host RGBA8 buffer and uploads it straight into the ensureTex output via
// c.output->replaceRegion — the SAME host-bytes→output-texture path point_ops_blend.cpp:320 uses (no copy
// shader, no platform include: nanosvgrast is pure runtime, replaceRegion is pure Metal, which runtime is
// allowed to call — ARCHITECTURE.md: platform-zone rule is about native macOS *frameworks* like ImageIO,
// not the Metal API itself, which every tex op already calls).
//
//   LoadSvgAsTexture2D.cs UpdateTexture() (the load-bearing path, distilled):
//     svgDoc = SvgLoader.TryLoad(Path);                                    // → svg_parse (nanosvg) parse
//     width/height from Resolution or ViewBox (:111-124);                  // → c.output dims (driver-pinned)
//     bitmap = workingDocument.Draw(width, height);                        // → nsvgRasterize(scale)
//     Texture.Value = ConvertBitmapToTexture2D(bitmap);   // RGBA8            → replaceRegion into c.output
//   Null/invalid svg → Texture.Value = null (:169-179) → sw clears the output to TRANSPARENT BLACK (the
//   same fallback cookLoadImage uses; never crash).
//
// NAMED FORKS (vs LoadSvgAsTexture2D.cs — that file IS the parity reference):
//   • fork-svg-rasterizer (svg_parse.h): nanosvgrast (scanline AA) ≠ GDI+ System.Drawing.Draw → edge
//     antialiased pixels differ. The golden pins INTERIOR-of-fill (solid color) and CLEAR-background
//     texels, NOT edge AA — where both rasterizers agree (impl-independent, GOLDEN_STANDARD).
//   • fork-svg-scale-fit: TiXL's Resolution/UseViewBox/Scale logic (:111-124) picks the raster size +
//     scale from either an explicit Resolution, the SVG ViewBox, or the requested resolution. sw
//     rasterizes at the DRIVER-pinned output size (c.output->width/height, the standard source-op
//     Resolution enum handled by the cook driver — same as LoadImage) and derives the scale = outputH /
//     svgHeight (fit-to-height, matching TiXL's ScaleToBounds-style height fit) unless the Scale knob
//     overrides (Scale>0 → explicit scale). SplitToLayers / SelectLayerRange (:43-104, a layer-range
//     compositor) are NOT ported (a later concern; named, not silent).
//   • fork-svg-no-svgfont / fork-svg-flatten-algorithm: inherited from svg_parse.h (do not affect a
//     filled-shape rasterize).
//   • fork[resident-uses-default-path]: like LoadImage — the resident (production) tex-cook driver sets
//     c.graph=nullptr, so a per-instance Path STRING does not reach the resident cook; the resident leg
//     loads the DEFAULT path. A per-instance runtime Path in resident needs a string channel on TexCookCtx
//     (blocked by the same line-count cap as LoadImage's fork). The flat golden proves the Path wiring.
//   • fork[no-hot-reload]: sw re-reads the .svg file every cook; TiXL hot-reloads via ResourceManager.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"
#include "runtime/graph.h"                      // Graph/Node/findSpec (flat Path-override read)
#include "runtime/image_filter_op_registry.h"   // ImageFilterOp + resolveImageFilterAssetPath
#include "runtime/point_graph.h"                // TexCookCtx, registerTexOp
#include "runtime/svg_parse.h"                  // svgParsePolylinesFromFile (unused here) / svgRasterizeRgba8FromText

namespace sw {

// Default asset key (a committed .svg under assets/) — the resident static path + the flat default when a
// node carries no Path override. Empty by default: SVG has no committed default asset yet, so the resident
// leg (and a Path-less flat cook) fall to the transparent-black fallback (never crash). A committed default
// .svg can be added later and named here without touching the cook body.
static const char* kLoadSvgDefaultPath = "";

// Forward decl so the file-scope registrar (below the anon namespace) can name the selftest.
int runLoadSvgAsTexture2DSelfTest(bool injectBug);

// injectBug hook (golden only): when set, the REAL raster is discarded and the output is cleared to
// transparent black — bites the golden's interior-fill probe (a solid-color interior collapses to
// (0,0,0,0)). Off in production. The golden sets/clears it around its cook.
bool& loadSvgTexInjectBug();

namespace {

bool g_loadSvgTexInjectBug = false;

// Read a whole file (raw/binary). Empty path → "" (→ fallback). obj_parse.cpp posture.
bool readFileBytes(const std::string& path, std::string& out) {
  if (path.empty()) return false;
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  std::fseek(f, 0, SEEK_END);
  long n = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (n <= 0) { std::fclose(f); return false; }
  out.resize((size_t)n);
  size_t rd = std::fread(&out[0], 1, (size_t)n, f);
  std::fclose(f);
  out.resize(rd);
  return rd > 0;
}

// Resolve THIS cook's SVG source text. Flat leg: read the node's per-instance Path override
// (Node::strParams["Path"]) — resolved as an absolute path, or a "Lib:..." asset key via
// resolveImageFilterAssetPath. Resident leg (c.graph == nullptr): the default path.
bool resolveSvgText(TexCookCtx& c, std::string& textOut) {
  std::string path = kLoadSvgDefaultPath;
  if (c.graph) {
    const Node* n = c.graph->node(c.nodeId);
    if (n) {
      auto it = n->strParams.find("Path");
      if (it != n->strParams.end()) path = it->second;  // explicit value (may be empty = blank)
    }
  }
  if (path.empty()) return false;
  // "Lib:..." → SW_ASSETS_DIR-relative; otherwise treat as an absolute/cwd path verbatim.
  std::string resolved = path;
  if (path.rfind("Lib:", 0) == 0) {
    resolved = resolveImageFilterAssetPath(path);
    if (resolved.empty()) return false;
  }
  return readFileBytes(resolved, textOut);
}

// Clear the output to transparent black (fallback — mirror cookLoadImage's no-source clear).
void clearOutputTransparent(TexCookCtx& c) {
  const uint32_t w = (uint32_t)c.output->width(), h = (uint32_t)c.output->height();
  std::vector<uint8_t> px((size_t)w * h * 4, 0);  // 0,0,0,0
  c.output->replaceRegion(MTL::Region::Make2D(0, 0, w, h), 0, px.data(), w * 4);
}

void cookLoadSvgAsTexture2D(TexCookCtx& c) {
  if (!c.output) return;
  const uint32_t w = (uint32_t)c.output->width(), h = (uint32_t)c.output->height();
  if (w == 0 || h == 0) return;

  std::string svgText;
  if (!resolveSvgText(c, svgText)) {
    clearOutputTransparent(c);  // no source (empty/blank Path, resident, unreadable) → fallback
    return;
  }

  // Parse once for the canvas size (to derive the fit-scale). fork-svg-scale-fit: scale = outputH /
  // svgHeight unless the Scale knob overrides. A zero/absent canvas → scale 1 (rasterize 1:1).
  float scaleKnob = cookParam(c, "Scale", 0.0f);
  float scale = 1.0f;
  {
    SvgPolylines probe;
    if (svgParsePolylinesFromText(svgText, probe) && probe.height > 0.0f)
      scale = (float)h / probe.height;
  }
  if (scaleKnob > 0.0f) scale = scaleKnob;

  std::vector<unsigned char> rgba;
  if (!svgRasterizeRgba8FromText(svgText, (int)w, (int)h, scale, rgba) ||
      rgba.size() != (size_t)w * h * 4) {
    clearOutputTransparent(c);
    return;
  }

  // Test-only: corrupt the REAL raster → clear to transparent (bites the interior-fill probe). Off in
  // production. Same shape as every source op's inject hook (the golden asserts a solid interior color).
  if (g_loadSvgTexInjectBug) {
    clearOutputTransparent(c);
    return;
  }

  c.output->replaceRegion(MTL::Region::Make2D(0, 0, w, h), 0, rgba.data(), w * 4);
}

}  // namespace

// Non-anon accessor (the golden includes this TU's decl to toggle the flag). Mirrors the file-scope
// inject pattern used by other image-source goldens (rgbtv's g_rgbtvDropPattern).
bool& loadSvgTexInjectBug() { return g_loadSvgTexInjectBug; }

// Self-registration. A Texture2D SOURCE (no image input) + Path(String) + Resolution enum + Scale knob.
static const ImageFilterOp _reg_loadsvgastexture2d{
    {"LoadSvgAsTexture2D", "LoadSvgAsTexture2D",
     {// No Image input — a SOURCE.
      {"Texture", "Texture", "Texture2D", false},
      // Path (String): the .svg path (absolute/cwd, or "Lib:..." asset key). strDef = the default
      // (empty → fallback until a committed default svg is named). Last positional field.
      {"Path", "Path", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, ""},
      // Scale (>0 → explicit user-unit→pixel scale; 0 → fit-to-height, fork-svg-scale-fit). Default 0.
      {"Scale", "Scale", "Float", true, 0.0f, 0.0f, 100.0f},
      // Resolution (standard image-source enum; default WindowFollow) + Custom dims — same shape as LoadImage.
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "LoadSvgAsTexture2D", cookLoadSvgAsTexture2D, "loadsvgastexture2d", runLoadSvgAsTexture2DSelfTest};

}  // namespace sw
