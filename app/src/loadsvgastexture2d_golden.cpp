// loadsvgastexture2d_golden — --selftest-loadsvgastexture2d / -bug. Closed-form pixel golden for
// LoadSvgAsTexture2D (svg_parse rasterize → RGBA8 output texture; the image SOURCE seam).
//
// What it proves (FLAT leg): a LoadSvgAsTexture2D node with a per-instance Path override parses+rasterizes
// that .svg (nsvgRasterize) and uploads the RGBA8 result into its resolution-pinned output. The golden
// drives the flat tex-cook driver (PointGraph::cook → target()) — the SAME shape production's flat path
// builds — and reads back the output texture.
//
// IMPL-INDEPENDENCE (GOLDEN_STANDARD 三特徵 #1, fork-svg-rasterizer): the fixture is a SOLID-FILL rect
// covering the LEFT HALF of a 16×16 canvas (fill=rgb(200,50,25)), the right half empty. Interior-of-fill
// texels (far from the edge) are the SOLID fill color in ANY correct rasterizer; clear-background texels
// are transparent (0,0,0,0). The golden probes ONLY interior-fill + deep-background texels — NOT the
// antialiased edge column (which nanosvgrast and GDI+ shade differently). Expected values are the SVG's
// authored fill / clear, NOT sw's own output.
//
// OFF-IDENTITY SAMPLING (三特徵 #2): the fill color has R≠G≠B (200,50,25) and the fill covers only HALF
// the canvas — so a channel swap (R↔B), a whole-canvas fill (ignoring the rect geometry), or a
// transparent-everywhere no-op each go RED. A uniform grey fill or a full-canvas fill would hide these.
//
// injectBug (loadSvgTexInjectBug): the REAL raster is discarded → output cleared to transparent black →
// the interior-fill probe (solid 200,50,25) collapses to (0,0,0,0) → RED. A real cook-path corruption,
// not an expected-value flip. Did-not-trip → return 0 (P1).
#include "runtime/point_graph.h"

#include <unistd.h>  // getpid

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <system_error>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"
#include "runtime/selftest_registry.h"  // REGISTER_SELFTESTS
#include "runtime/tex_op_cache.h"

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// Declared in point_ops_loadsvgastexture2d.cpp (file-scope inject flag accessor).
bool& loadSvgTexInjectBug();

namespace {

// A 16×16 canvas with a solid rect over the LEFT HALF (x 0..8), fill rgb(200,50,25). Right half empty.
const char* const kHalfRectSvg =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\">"
    "<rect x=\"0\" y=\"0\" width=\"8\" height=\"16\" fill=\"rgb(200,50,25)\"/></svg>";

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

// Cook one LoadSvgAsTexture2D node (FLAT path) into a 16×16 output; read it back. pathOverride = Path.
bool cookFlatLoadSvgTex(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib,
                        const std::string& pathOverride, std::vector<uint8_t>& out) {
  registerBuiltinPointOps();  // ensures LoadSvgAsTexture2D's texReg/spec are registered

  const uint32_t N = 16;
  PointGraph pg(dev, lib, q, N, N);  // window N×N → WindowFollow output = N×N
  Graph g;
  Node n; n.id = 1; n.type = "LoadSvgAsTexture2D";
  n.params["Resolution"] = 0.0f;      // WindowFollow → N×N
  n.params["Scale"] = 1.0f;           // explicit 1:1 (canvas 16 → 16px, no fit-scale)
  n.strParams["Path"] = pathOverride; // per-instance Path (flat leg reads this)
  g.nodes.push_back(n);

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, /*target=*/1);  // the terminal tex node

  MTL::Texture* tex = pg.target();
  if (!tex || tex->width() != N || tex->height() != N) return false;
  out.assign((size_t)N * N * 4, 0);
  tex->getBytes(out.data(), N * 4, MTL::Region::Make2D(0, 0, N, N), 0);
  return true;
}

}  // namespace

int runLoadSvgAsTexture2DSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-loadsvgastexture2d] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  std::filesystem::path svgFile = writeTempSvg(kHalfRectSvg, "sw_svg_tex");
  bool wrote = !svgFile.empty();

  loadSvgTexInjectBug() = injectBug;
  std::vector<uint8_t> tex;
  bool cooked = wrote && cookFlatLoadSvgTex(dev, q, lib, svgFile.string(), tex);
  loadSvgTexInjectBug() = false;

  const uint32_t N = 16;
  auto px = [&](uint32_t x, uint32_t y, int ch) { return (int)tex[((size_t)y * N + x) * 4 + ch]; };
  const int kTol = 4;  // nanosvgrast interior fill is exact modulo its 8-bit rounding; allow a few LSB

  // Interior-of-fill probe: (3, 8) — deep inside the left rect (x∈[0,8)), away from the x=8 edge.
  // Expected the solid fill (200,50,25,255). Background probe: (13, 8) — deep in the empty right half →
  // transparent (0,0,0,0). Both are edge-free → impl-independent (fork-svg-rasterizer).
  bool fillOk = false, bgOk = false;
  int fr = -1, fg = -1, fb = -1, fa = -1, br = -1, bg = -1, bb = -1, ba = -1;
  if (cooked) {
    fr = px(3, 8, 0); fg = px(3, 8, 1); fb = px(3, 8, 2); fa = px(3, 8, 3);
    br = px(13, 8, 0); bg = px(13, 8, 1); bb = px(13, 8, 2); ba = px(13, 8, 3);
    fillOk = std::abs(fr - 200) <= kTol && std::abs(fg - 50) <= kTol && std::abs(fb - 25) <= kTol &&
             std::abs(fa - 255) <= kTol;
    bgOk = br <= kTol && bg <= kTol && bb <= kTol && ba <= kTol;  // transparent background
  }

  bool pass = cooked && fillOk && bgOk;
  std::printf("[selftest-loadsvgastexture2d] cooked=%d fill(3,8)=(%d,%d,%d,%d)want(200,50,25,255) "
              "bg(13,8)=(%d,%d,%d,%d)want(0,0,0,0) fillOk=%d bgOk=%d injectBug=%d -> %s\n",
              cooked ? 1 : 0, fr, fg, fb, fa, br, bg, bb, ba, fillOk ? 1 : 0, bgOk ? 1 : 0,
              injectBug ? 1 : 0, pass ? "PASS" : "FAIL");

  std::error_code ec;
  if (!svgFile.empty()) std::filesystem::remove(svgFile, ec);
  lib->release(); q->release(); dev->release(); pool->release();

  if (injectBug) {
    if (pass) {
      std::printf("[selftest-loadsvgastexture2d] injectBug did not trip (raster unchanged)\n");
      return 0;  // dead tooth → exit 0 (P1)
    }
    std::printf("[selftest-loadsvgastexture2d] injectBug correctly RED (REAL raster cleared → fill "
                "probe collapsed to transparent)\n");
    return 1;
  }
  return pass ? 0 : 1;
}

}  // namespace sw
