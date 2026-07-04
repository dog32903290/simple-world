// pickcolorfromimage_golden — --selftest-pickcolorfromimage. PRODUCTION-PATH pin for
// PickColorFromImage (numbers/color/PickColorFromImage.cs): Texture2D → host vec4 eyedropper.
//
// R-2 IRON RULE: the op cooks on the point-into-frame slot (cookPickColorFromImageNodes, called by
// cook_host_values::cookPointValueFromGraph AFTER pg.cookResident with texFor =
// PointGraph::residentTexFor). This golden drives EXACTLY that seam: builds a real
// CheckerBoard→PickColorFromImage resident graph, cooks the texture through pg.cookResident, then
// runs the REAL pass with the REAL residentTexFor accessor and reads extOut[0..3].
//
// Expected values hand-derived from PickColorFromImage.cs + Color.cs (cited in the leaf header):
//   LEG A (R8G8B8A8_UNorm production chain): 64×64 CheckerBoard (2×2 cells), ColorA=(1,0,0,1)
//   ColorB=(0,0,1,1) — 255/0 bytes, so the 1/255 normalize (Color.cs:39-47) is EXACT. Quadrant map
//   pinned by t3import_remapcolor_golden: texel(16,16)=ColorB, (48,16)=ColorA, (16,48)=ColorA,
//   (48,48)=ColorB. column/row = clamp((int)(pos*64), 0, 63) (cs:36-37):
//     pos(0.25,0.25)→(16,16)→(0,0,1,1) ; pos(0.75,0.25)→(48,16)→(1,0,0,1) ;
//     pos(0.25,0.75)→(16,48)→(1,0,0,1) ; pos(0.75,0.75)→(48,48)→(0,0,1,1)
//   LEG B (the cs:40-46 STALENESS quirk, load-bearing): recolor ColorA→(0,1,0,1) and re-cook the
//   texture on the SAME node path. AlwaysUpdate=false → the pick still reads the CACHED staging copy
//   → STALE (1,0,0,1). AlwaysUpdate=true → re-copy → fresh (0,1,0,1).
//   LEG C (format switch on hand-filled 2×2 textures fed through the SAME production pass via the
//   texFor accessor — real MTL textures, byte-authored, no shader AA):
//     RGBA32Float texels t00..t11 = (1,2,3,4)/(5,6,7,8)/(9,10,11,12)/(13,14,15,16):
//       pos(0.75,0.25)→texel(1,0)=(5,6,7,8) ; pos(1.7,1.7)→clamp(3→1,3→1)=(13,14,15,16) ;
//       pos(-1.5,0.25)→clamp(-3→0,0)=(1,2,3,4)          [the cs:36-37 clamp legs]
//     RGBA16Float texel(1,0) = halves 0x3800/0xC000/0x4400/0x3C00 → (0.5,-2,4,1) (raw half→float)
//     RGBA16Unorm texel(1,0) = ushorts 0x1234/0x5678/0x9ABC/0xFFFF → HIGH bytes RAW (cs:111-123
//       quirk, NOT normalized) → (18,86,154,255)
//     R32Float (unhandled format) → Color.White = (1,1,1,1) (cs:142-145)
//   LEG D (unwired InputImage): cs:26-29 early return, no write → extOut stays (0,0,0,0).
//
// injectBug: pickColorImageInjectBug() DROPS the Color(Byte4) 1/255 normalize on the REAL decode
// (leaf cs:39-47 term) → every LEG A/B probe reads 0..255-scaled values → RED (LEG C float formats
// are normalize-free and stay green — the bite is isolated to the corrupted term). Wants are
// INDEPENDENT of the flag. did-not-trip → 0 added failures → exit 0 → --bite NO-BITE catches it.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <simd/simd.h>

#include "runtime/compound_graph.h"       // Symbol/SymbolChild/SymbolLibrary
#include "runtime/eval_context.h"         // EvaluationContext
#include "runtime/graph.h"                // findSpec
#include "runtime/graph_bridge.h"         // atomicSymbolFromSpec
#include "runtime/point_graph.h"          // PointGraph (cook the checker + residentTexFor)
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / ResidentEvalCtx / ResidentNode

namespace sw {

// The production pass + its teeth (value_op_pickcolorfromimage.cpp).
void cookPickColorFromImageNodes(ResidentEvalGraph& g, const ResidentEvalCtx& ctx,
                                 const std::function<MTL::Texture*(const std::string&)>& texFor);
bool& pickColorImageInjectBug();

namespace {

int g_fail = 0;
void expectColor(const char* what, const float* got, simd::float4 want) {
  const bool ok = std::fabs(got[0] - want.x) < 1e-4f && std::fabs(got[1] - want.y) < 1e-4f &&
                  std::fabs(got[2] - want.z) < 1e-4f && std::fabs(got[3] - want.w) < 1e-4f;
  if (!ok) {
    ++g_fail;
    printf("  [pickcolorfromimage] FAIL %s got=(%.4f,%.4f,%.4f,%.4f) want=(%.4f,%.4f,%.4f,%.4f)\n",
           what, got[0], got[1], got[2], got[3], want.x, want.y, want.z, want.w);
  } else {
    printf("  [pickcolorfromimage] ok   %s = (%.3f,%.3f,%.3f,%.3f)\n", what, got[0], got[1], got[2],
           got[3]);
  }
}

// Root R: child1 = CheckerBoard(colorA/colorB, 64×64), child2 = PickColorFromImage(pos, alwaysUpdate),
// wire 1.out → 2.InputImage (wired=false drops the wire for the LEG D unwired probe).
SymbolLibrary makeLib(simd::float4 colorA, simd::float4 colorB, float posX, float posY,
                      float alwaysUpdate, bool wired) {
  SymbolLibrary lib;
  for (const char* t : {"CheckerBoard", "PickColorFromImage"})
    if (const NodeSpec* s = findSpec(t)) lib.symbols[t] = atomicSymbolFromSpec(*s);
  Symbol root; root.id = "R"; root.name = "R"; root.atomic = false;
  SymbolChild cb; cb.id = 1; cb.symbolId = "CheckerBoard";
  const float ar = colorA.x, ag = colorA.y, ab = colorA.z, aa = colorA.w;
  const float br = colorB.x, bg = colorB.y, bb = colorB.z, ba = colorB.w;
  cb.overrides = {{"ColorA.r", ar}, {"ColorA.g", ag}, {"ColorA.b", ab}, {"ColorA.a", aa},
                  {"ColorB.r", br}, {"ColorB.g", bg}, {"ColorB.b", bb}, {"ColorB.a", ba},
                  {"Stretch.x", 1.0f}, {"Stretch.y", 1.0f}, {"Scale", 1.0f},
                  {"UseAspectRatio", 1.0f}, {"Resolution", 4.0f}, {"CustomW", 64.0f},
                  {"CustomH", 64.0f}};
  SymbolChild pk; pk.id = 2; pk.symbolId = "PickColorFromImage";
  pk.overrides = {{"Position.x", posX}, {"Position.y", posY}, {"AlwaysUpdate", alwaysUpdate}};
  root.children = {cb, pk};
  root.nextChildId = 3;
  if (wired) root.connections.push_back({1, "out", 2, "InputImage"});
  lib.symbols["R"] = root; lib.rootId = "R";
  return lib;
}

// Cook the checker texture (pg.cookResident, target "1"), then run the REAL production pick pass
// with the REAL residentTexFor accessor; return extOut of node "2".
const float* pickThroughProduction(PointGraph& pg, SymbolLibrary& lib, ResidentEvalGraph& g) {
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cookResident(g, ctx, nullptr, "1");  // this frame's texture cook (the point-into-frame order)
  ResidentEvalCtx rc;
  rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.frameIndex = 0; rc.lib = &lib;
  cookPickColorFromImageNodes(g, rc,
                              [&pg](const std::string& p) { return pg.residentTexFor(p); });
  const ResidentNode* n = g.node("2");
  static float zero[4] = {-999, -999, -999, -999};
  return n ? n->extOut : zero;
}

// Hand-authored 2×2 texture (replaceRegion — byte-exact, no shader in the loop).
MTL::Texture* makeTex(MTL::Device* dev, MTL::PixelFormat fmt, const void* bytes, size_t rowBytes) {
  MTL::TextureDescriptor* td = MTL::TextureDescriptor::texture2DDescriptor(fmt, 2, 2, false);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* t = dev->newTexture(td);
  t->replaceRegion(MTL::Region::Make2D(0, 0, 2, 2), 0, bytes, rowBytes);
  return t;
}

// Run the production pass with a HAND-authored texture behind the same texFor accessor seam.
void pickFromTex(SymbolLibrary& lib, ResidentEvalGraph& g, MTL::Texture* tex, float out[4]) {
  ResidentEvalCtx rc;
  rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.frameIndex = 0; rc.lib = &lib;
  cookPickColorFromImageNodes(g, rc, [tex](const std::string&) { return tex; });
  const ResidentNode* n = g.node("2");
  for (int i = 0; i < 4; ++i) out[i] = n ? n->extOut[i] : -999.0f;
}

const simd::float4 kRed = simd::make_float4(1, 0, 0, 1);
const simd::float4 kGreen = simd::make_float4(0, 1, 0, 1);
const simd::float4 kBlue = simd::make_float4(0, 0, 1, 1);

// LEG A+B (the RGBA8 production chain + the staleness quirk). Accumulates into g_fail.
void runCheckerLegs(PointGraph& pg) {
  struct { float px, py; simd::float4 want; const char* note; } cases[] = {
      {0.25f, 0.25f, kBlue, "A (0.25,0.25) -> texel(16,16) ColorB"},
      {0.75f, 0.25f, kRed, "A (0.75,0.25) -> texel(48,16) ColorA"},
      {0.25f, 0.75f, kRed, "A (0.25,0.75) -> texel(16,48) ColorA"},
      {0.75f, 0.75f, kBlue, "A (0.75,0.75) -> texel(48,48) ColorB"},
  };
  for (auto& c : cases) {
    SymbolLibrary lib = makeLib(kRed, kBlue, c.px, c.py, 0.0f, true);
    ResidentEvalGraph g = buildEvalGraph(lib, "R");
    expectColor(c.note, pickThroughProduction(pg, lib, g), c.want);
  }
  // LEG B: recolor ColorA→green, re-cook the texture on the SAME path. AlwaysUpdate=0 → the pick
  // reads the CACHED copy (desc unchanged, cs:40-46) → STALE red. AlwaysUpdate=1 → re-copy → green.
  {
    SymbolLibrary lib = makeLib(kGreen, kBlue, 0.75f, 0.25f, 0.0f, true);
    ResidentEvalGraph g = buildEvalGraph(lib, "R");
    expectColor("B stale (AlwaysUpdate=0, recolored -> cached red)",
                pickThroughProduction(pg, lib, g), kRed);
  }
  {
    SymbolLibrary lib = makeLib(kGreen, kBlue, 0.75f, 0.25f, 1.0f, true);
    ResidentEvalGraph g = buildEvalGraph(lib, "R");
    expectColor("B fresh (AlwaysUpdate=1 -> re-copied green)", pickThroughProduction(pg, lib, g),
                kGreen);
  }
}

// LEG C (format switch) + LEG D (unwired). Hand-authored textures behind the texFor seam;
// AlwaysUpdate=1 so each case re-copies (no cross-case cache coupling on the shared "2" path).
void runFormatLegs(MTL::Device* dev) {
  // RGBA32Float: 4 distinct texels — pick + both clamp legs.
  {
    const float t[2][2][4] = {{{1, 2, 3, 4}, {5, 6, 7, 8}}, {{9, 10, 11, 12}, {13, 14, 15, 16}}};
    MTL::Texture* tex = makeTex(dev, MTL::PixelFormatRGBA32Float, t, 2 * 16);
    struct { float px, py; simd::float4 want; const char* note; } cases[] = {
        {0.75f, 0.25f, simd::make_float4(5, 6, 7, 8), "C 32F texel(1,0)"},
        {1.7f, 1.7f, simd::make_float4(13, 14, 15, 16), "C 32F clamp-high -> texel(1,1)"},
        {-1.5f, 0.25f, simd::make_float4(1, 2, 3, 4), "C 32F clamp-low -> texel(0,0)"},
    };
    for (auto& c : cases) {
      SymbolLibrary lib = makeLib(kRed, kBlue, c.px, c.py, 1.0f, true);
      ResidentEvalGraph g = buildEvalGraph(lib, "R");
      float out[4];
      pickFromTex(lib, g, tex, out);
      expectColor(c.note, out, c.want);
    }
    tex->release();
  }
  // RGBA16Float: texel(1,0) = (0.5,-2,4,1) as halves (0x3800,0xC000,0x4400,0x3C00) — raw half→float.
  {
    const uint16_t t[2][2][4] = {{{0, 0, 0, 0}, {0x3800, 0xC000, 0x4400, 0x3C00}},
                                 {{0, 0, 0, 0}, {0, 0, 0, 0}}};
    MTL::Texture* tex = makeTex(dev, MTL::PixelFormatRGBA16Float, t, 2 * 8);
    SymbolLibrary lib = makeLib(kRed, kBlue, 0.75f, 0.25f, 1.0f, true);
    ResidentEvalGraph g = buildEvalGraph(lib, "R");
    float out[4];
    pickFromTex(lib, g, tex, out);
    expectColor("C 16F texel(1,0) half->float", out, simd::make_float4(0.5f, -2.0f, 4.0f, 1.0f));
    tex->release();
  }
  // RGBA16Unorm: texel(1,0) ushorts (0x1234,0x5678,0x9ABC,0xFFFF) → HIGH bytes RAW (cs:111-123
  // quirk): (0x12,0x56,0x9A,0xFF) = (18,86,154,255) — NOT normalized.
  {
    const uint16_t t[2][2][4] = {{{0, 0, 0, 0}, {0x1234, 0x5678, 0x9ABC, 0xFFFF}},
                                 {{0, 0, 0, 0}, {0, 0, 0, 0}}};
    MTL::Texture* tex = makeTex(dev, MTL::PixelFormatRGBA16Unorm, t, 2 * 8);
    SymbolLibrary lib = makeLib(kRed, kBlue, 0.75f, 0.25f, 1.0f, true);
    ResidentEvalGraph g = buildEvalGraph(lib, "R");
    float out[4];
    pickFromTex(lib, g, tex, out);
    expectColor("C 16Unorm high-byte quirk (raw 0..255)", out,
                simd::make_float4(18, 86, 154, 255));
    tex->release();
  }
  // Unhandled format (R32Float) → Color.White (cs:142-145).
  {
    const float t[2][2] = {{0.3f, 0.6f}, {0.1f, 0.9f}};
    MTL::Texture* tex = makeTex(dev, MTL::PixelFormatR32Float, t, 2 * 4);
    SymbolLibrary lib = makeLib(kRed, kBlue, 0.25f, 0.25f, 1.0f, true);
    ResidentEvalGraph g = buildEvalGraph(lib, "R");
    float out[4];
    pickFromTex(lib, g, tex, out);
    expectColor("C unknown format -> White", out, simd::make_float4(1, 1, 1, 1));
    tex->release();
  }
  // LEG D: unwired InputImage → cs:26-29 early return, extOut stays zero.
  {
    SymbolLibrary lib = makeLib(kRed, kBlue, 0.25f, 0.25f, 0.0f, false);
    ResidentEvalGraph g = buildEvalGraph(lib, "R");
    float out[4];
    pickFromTex(lib, g, nullptr, out);
    expectColor("D unwired -> untouched (0,0,0,0)", out, simd::make_float4(0, 0, 0, 0));
  }
}

}  // namespace

int runPickColorFromImageSelfTest(bool injectBug) {
  g_fail = 0;
  printf("[selftest] pickcolorfromimage (Texture2D -> host vec4 eyedropper, production pass)\n");
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) {
    printf("[selftest] pickcolorfromimage FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  {
    PointGraph pg(dev, mlib, q, 64, 64);

    // CLEAN pass first (production behavior must hold regardless of injectBug).
    pickColorImageInjectBug() = false;
    runCheckerLegs(pg);
    runFormatLegs(dev);
    const int cleanFail = g_fail;
    printf("[selftest] pickcolorfromimage clean pass: %d failure(s)\n", cleanFail);

    if (injectBug) {
      // REAL-term corruption: DROP the Color(Byte4) 1/255 normalize — the RGBA8 legs must FAIL.
      pickColorImageInjectBug() = true;
      const int before = g_fail;
      runCheckerLegs(pg);
      printf("[selftest] pickcolorfromimage bug(drop-1/255-normalize) added %d failure(s)\n",
             g_fail - before);
      pickColorImageInjectBug() = false;
    }
  }
  mlib->release(); q->release(); dev->release(); pool->release();
  printf("[selftest] pickcolorfromimage %s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
  return g_fail ? 1 : 0;
}

}  // namespace sw
