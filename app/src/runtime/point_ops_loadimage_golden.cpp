// LoadImage GOLDEN — closed-form RED->GREEN proof of the SOURCE-OP seam (the proving op LoadImage).
//
// What it proves:
//   (1) FLAT leg: a LoadImage node with a per-instance Path override decodes that asset (via the
//       asset-texture seam: TexCookCtx -> cachedAssetTexture -> registered decoder) and COPIES the
//       decoded RGBA8 texels into its resolution-pinned output. We assert the 4 corner texels of the
//       output equal the KNOWN straight-stored fixture texels (closed-form, each of R/G/B/A
//       load-bearing — the fixture has R!=G!=B per texel and distinct texels per corner).
//   (2) RESIDENT leg (R-2 iron rule — production walks cookResident, not flat cook): a LoadImage as a
//       Texture2D SOURCE terminal, cooked through cookResident, must produce a NON-ZERO texture (the
//       resident driver drove the static-default asset seam: resolved key -> decoder -> assetTexture
//       -> leaf -> target()). A flat-only golden can let resident garbage through (Cut 52 lesson).
//
// DECODER: a SYNTHETIC decoder shim fabricates the SAME 4 texels the committed fixture
// (assets/images/basic/test-2x2-rgba.png) stores — so the golden asserts EXACT known values without
// pulling ImageIO into the runtime test (the platform decoder is independently proven byte-exact by
// platform's --selftest-imagedecode). The fixture's real bytes were verified at author time (PIL
// round-trip): top-left=(255,16,32,255) top-right=(64,200,8,255) bot-left=(10,50,240,255)
// bot-right=(120,90,30,255), all OPAQUE (A=255 -> the premultiply fork is a no-op, decode byte-exact).
//
// ORIENTATION: the copy preserves source orientation 1:1 (out(x,y)==src(x,y); the loadimage_vs Y-flip
// cancels against the texture sample address). The golden asserts same-position corner texels exactly.
//
// injectBug: override Path to a BLANK string -> the asset key is empty -> cachedAssetTexture returns
// null -> the cook hits the FALLBACK (clear to TRANSPARENT black, alpha 0). The known fixture texels
// are OPAQUE (alpha 255) and non-zero RGB, so BOTH legs diverge from GREEN -> RED. (Resident -bug
// drops the static asset registration so its driver binds null too.) A real wiring perturbation.
#include "runtime/point_graph.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"
#include "runtime/graph.h"
#include "runtime/image_filter_op_registry.h"  // setAssetTextureDecoder / clearImageFilterAssetCache / asset sink
#include "runtime/resident_eval_graph.h"
#include "runtime/tex_op_cache.h"
#include "runtime/tixl_point.h"

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// The 4 KNOWN straight-stored fixture texels (assets/images/basic/test-2x2-rgba.png, PIL-verified).
// Row-major, top row first: index 0=top-left, 1=top-right, 2=bot-left, 3=bot-right.
struct Texel { uint8_t r, g, b, a; };
constexpr Texel kFixture[4] = {
    {255, 16, 32, 255},  // (0,0) top-left
    {64, 200, 8, 255},   // (1,0) top-right
    {10, 50, 240, 255},  // (0,1) bot-left
    {120, 90, 30, 255},  // (1,1) bot-right
};

int g_decoderCalls = 0;

// Synthetic decoder: fabricate the 2x2 RGBA8 texture with kFixture texels (same orientation the
// platform decoder produces: texel (0,0) = image top-left). Exercises the asset seam WITHOUT ImageIO.
MTL::Texture* syntheticDecoder(MTL::Device* dev, const char* /*absPath*/, bool /*mipped*/) {
  ++g_decoderCalls;
  const uint32_t N = 2;
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, N, N, false);
  td->setUsage(MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* t = dev->newTexture(td);
  uint8_t px[16];
  for (int i = 0; i < 4; ++i) {
    px[i * 4 + 0] = kFixture[i].r; px[i * 4 + 1] = kFixture[i].g;
    px[i * 4 + 2] = kFixture[i].b; px[i * 4 + 3] = kFixture[i].a;
  }
  t->replaceRegion(MTL::Region::Make2D(0, 0, N, N), 0, px, N * 4);
  return t;  // ownership -> cachedAssetTexture (released by clearImageFilterAssetCache)
}

// Cook a single LoadImage node through the FLAT path into a 2x2 RGBA8 output; read it back.
// pathOverride: the node's Path strParam (blank "" -> empty key -> fallback). out filled 2x2x4.
bool cookFlatLoadImage(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib,
                       const std::string& pathOverride, std::vector<uint8_t>& out) {
  registerBuiltinPointOps();  // ensures LoadImage's texReg/spec are registered

  PointGraph pg(dev, lib, q, 2, 2);  // window 2x2 -> WindowFollow output = 2x2 (matches fixture)
  Graph g;
  Node li; li.id = 1; li.type = "LoadImage";
  li.params["Resolution"] = 0.0f;        // WindowFollow -> 2x2 output
  li.strParams["Path"] = pathOverride;   // per-instance Path (flat leg reads this)
  g.nodes.push_back(li);

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, /*target=*/1);  // LoadImage is the terminal tex node

  MTL::Texture* tex = pg.target();
  if (!tex || tex->width() != 2 || tex->height() != 2) return false;
  out.assign(2 * 2 * 4, 0);
  tex->getBytes(out.data(), 2 * 4, MTL::Region::Make2D(0, 0, 2, 2), 0);
  return true;
}

Symbol atomicOp(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}

// Cook a LoadImage SOURCE terminal through cookResident; read back its output. The resident driver
// binds the STATIC default asset key (imageFilterAssetTextures()["LoadImage"]) -> decoder -> output.
bool cookResidentLoadImage(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* mlib,
                           std::vector<uint8_t>& out, uint32_t& ow, uint32_t& oh) {
  SymbolLibrary lib;
  lib.symbols["LoadImage"] = atomicOp(
      "LoadImage",
      {{"Path", "Path", "String", 0.0f}, {"Resolution", "Resolution", "Float", 0.0f},
       {"CustomW", "CustomW", "Float", 512.0f}, {"CustomH", "CustomH", "Float", 512.0f}},
      {{"out", "out", "Texture2D", 0.0f}});
  Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
  root.outputDefs = {{"out", "out", "Texture2D", 0.0f}};
  SymbolChild c1; c1.id = 1; c1.symbolId = "LoadImage";
  c1.overrides["Resolution"] = 0.0f;  // WindowFollow -> window (8x8) output
  root.children = {c1};
  root.connections = {{1, "out", kSymbolBoundary, "out"}};
  lib.symbols["Root"] = root; lib.rootId = "Root";
  ResidentEvalGraph rg = buildEvalGraph(lib, "Root");

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  const uint32_t W = 8, H = 8;
  PointGraph pg(dev, mlib, q, W, H);
  pg.cookResident(rg, ctx, nullptr, /*targetPath=*/"1");

  MTL::Texture* tex = pg.target();
  ow = tex ? (uint32_t)tex->width() : 0;
  oh = tex ? (uint32_t)tex->height() : 0;
  if (!tex || ow == 0 || oh == 0) return false;
  out.assign((size_t)ow * oh * 4, 0);
  tex->getBytes(out.data(), ow * 4, MTL::Region::Make2D(0, 0, ow, oh), 0);
  return true;
}

}  // namespace

int runLoadImageSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  g_decoderCalls = 0;
  setAssetTextureDecoder(&syntheticDecoder);
  clearImageFilterAssetCache();

  // injectBug also strips the RESIDENT static asset registration so its driver binds null (fallback).
  // Saved + restored so the production registration survives the test.
  std::string savedKey;
  bool hadKey = false;
  {
    auto& m = imageFilterAssetTextures();
    auto it = m.find("LoadImage");
    if (it != m.end()) { hadKey = true; savedKey = it->second; }
    if (injectBug) m.erase("LoadImage");
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-loadimage] FAIL: no metallib\n");
    setAssetTextureDecoder(nullptr);
    q->release(); dev->release(); pool->release();
    return 1;
  }

  const int kTol = 1;  // nearest copy of opaque straight-stored texels -> exact (allow 1 LSB)

  // ===== FLAT leg: LoadImage with a Path override -> decode fixture -> copy into 2x2 output. =====
  // injectBug -> Path = "" (blank) -> empty key -> null source -> fallback (transparent black).
  std::vector<uint8_t> flat;
  bool flatOk = cookFlatLoadImage(dev, q, lib,
                                  injectBug ? std::string("")
                                            : std::string("Lib:images/basic/test-2x2-rgba.png"),
                                  flat);

  // Orientation: the copy preserves source orientation 1:1 (the loadimage_vs Y-flip cancels against
  // the texture sample address — verified empirically: out(x,y) == src(x,y)). So each output corner
  // equals the SAME-position fixture texel: out(x,y) = kFixture[row-major(x,y)].
  //   out(0,0)=top-left kFixture[0] ; out(1,0)=top-right kFixture[1]
  //   out(0,1)=bot-left kFixture[2] ; out(1,1)=bot-right kFixture[3]
  struct Corner { uint32_t x, y; const Texel* want; };
  const Corner kCorners[4] = {
      {0, 0, &kFixture[0]}, {1, 0, &kFixture[1]}, {0, 1, &kFixture[2]}, {1, 1, &kFixture[3]},
  };
  auto px = [&](const std::vector<uint8_t>& v, uint32_t x, uint32_t y, int ch) {
    return (int)v[((size_t)y * 2 + x) * 4 + ch];
  };
  bool flatMatch = flatOk;
  int flatMaxDelta = 0;
  if (flatOk)
    for (const Corner& c : kCorners) {
      int want[4] = {c.want->r, c.want->g, c.want->b, c.want->a};
      for (int ch = 0; ch < 4; ++ch) {
        int d = std::abs(px(flat, c.x, c.y, ch) - want[ch]);
        flatMaxDelta = std::max(flatMaxDelta, d);
        if (d > kTol) flatMatch = false;
      }
    }

  // ===== RESIDENT leg: LoadImage SOURCE terminal -> static default asset -> non-zero output. =====
  std::vector<uint8_t> res;
  uint32_t rw = 0, rh = 0;
  bool resOk = cookResidentLoadImage(dev, q, lib, res, rw, rh);
  // Non-zero RGB anywhere (the decoded fixture is fully opaque non-black). injectBug: static key
  // erased -> resident binds null -> fallback transparent -> all RGB 0 + alpha 0 -> resNonZero false.
  int resNonZero = 0;
  if (resOk)
    for (size_t i = 0; i < (size_t)rw * rh; ++i)
      if (res[i * 4] > 0 || res[i * 4 + 1] > 0 || res[i * 4 + 2] > 0) ++resNonZero;
  bool residentLit = resOk && resNonZero > 0;

  // The asset seam must have fired (decoder invoked via the cook driver / flat cachedAssetTexture).
  // Under injectBug, the flat key is "" (no decode) AND the resident key is erased (no decode) -> 0.
  bool assetSeamFired = (g_decoderCalls >= 1);

  // INVARIANT: the fixture genuinely carries 4 DISTINCT, channel-load-bearing texels (so a channel
  // swap or a passthrough-of-uniform would be caught). Verified statically on kFixture.
  bool distinct = true;
  for (int i = 0; i < 4; ++i)
    for (int j = i + 1; j < 4; ++j)
      if (kFixture[i].r == kFixture[j].r && kFixture[i].g == kFixture[j].g &&
          kFixture[i].b == kFixture[j].b)
        distinct = false;
  bool channelsDiffer = true;  // R!=G!=B within each texel
  for (int i = 0; i < 4; ++i)
    if (kFixture[i].r == kFixture[i].g || kFixture[i].g == kFixture[i].b) channelsDiffer = false;

  bool pass = flatOk && flatMatch && residentLit && assetSeamFired && distinct && channelsDiffer;

  std::printf(
      "[selftest-loadimage] flatOk=%d flatMaxDelta=%d flatMatch=%d | residentOk=%d out=%ux%u "
      "nonZero=%d lit=%d | assetSeamFired=%d(calls=%d) distinct=%d channelsDiffer=%d injectBug=%d "
      "-> %s\n",
      flatOk ? 1 : 0, flatMaxDelta, flatMatch ? 1 : 0, resOk ? 1 : 0, rw, rh, resNonZero,
      residentLit ? 1 : 0, assetSeamFired ? 1 : 0, g_decoderCalls, distinct ? 1 : 0,
      channelsDiffer ? 1 : 0, injectBug ? 1 : 0, pass ? "PASS" : "FAIL");
  if (flatOk) {
    std::printf("  -- flat output (1:1 copy of fixture) --\n");
    for (const Corner& c : kCorners)
      std::printf("  out(%u,%u) want=(%d,%d,%d,%d) got=(%d,%d,%d,%d)\n", c.x, c.y, c.want->r,
                  c.want->g, c.want->b, c.want->a, px(flat, c.x, c.y, 0), px(flat, c.x, c.y, 1),
                  px(flat, c.x, c.y, 2), px(flat, c.x, c.y, 3));
  }

  // Restore the production asset registration (if we erased it for the bug leg).
  if (injectBug && hadKey) imageFilterAssetTextures()["LoadImage"] = savedKey;
  clearImageFilterAssetCache();
  setAssetTextureDecoder(nullptr);
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
