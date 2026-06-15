// Headless RED->GREEN proof that the Cut 50 compute-shader cook seam works in the RESIDENT
// (production) cook path — not only in the flat --selftest-crop. The live app drives cookResident
// (frame_cook.cpp), so a compute leaf (Crop, the only ImageFilterComputeOp) must, in resident:
//   (1) size its output via imageFilterSizeFns() from the cooked INPUT dims (Crop output =
//       input - margins), NOT the Resolution pin — proven by the output texture's dimensions, and
//   (2) get MTL::TextureUsageShaderWrite on that output (imageFilterComputeTypes()/needsWrite) so
//       the kernel's RWTexture2D write lands — proven by full coverage + the shifted marker.
// Before this seam was back-ported into point_graph_resident.cpp's cookTexNode, the resident path
// called ensureTex(path, res.w, res.h) plainly: the output stayed window-sized (dimsOk FAILS) and
// lacked ShaderWrite (the kernel write was rejected -> garbage/stale -> coverage + marker FAIL).
//
// Topology: RenderTarget (a REAL registered tex op, its cook fn overridden here to paint the crop
// test pattern into its own output — the parity-selftest pattern, so findSpec resolves it) -> Crop
// terminal. We cook through cookResident with Crop as targetPath, then read back pg.target()
// (= displayTex, Crop's own resolution-sized output) and assert the SAME contract as the flat golden.
//
// injectBug: paint the source marker OUTSIDE the kept rect so the shifted-marker probe fails — a
// tooth proving the readback assertions actually check the cooked pixels (not a blind pass).
#include "runtime/point_graph.h"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"       // SymbolLibrary / Symbol / SymbolChild / SymbolConnection
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / kSymbolBoundary
#include "runtime/tixl_point.h"           // EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// Input geometry: 64x64 (the PointGraph window size below) -> the source TexSource output. The
// crop margins are chosen non-8-divisible in the output (40 = 5*8, 44 = 5*8+4) so a ceil-div vs
// floor-div host dispatch difference would show as a coverage hole — same trap as the flat golden.
constexpr uint32_t kInW = 64, kInH = 64;
constexpr int kLeft = 12, kRight = 8, kTop = 20, kBottom = 4;  // -> out 44 x 40
constexpr uint32_t kOutW = kInW - kLeft - kRight;             // 44
constexpr uint32_t kOutH = kInH - kTop - kBottom;             // 40
// Source marker location. The kernel maps output(i) = source(i.x-Left, i.y-Top), so source (kMx,kMy)
// surfaces at output (kMx+Left, kMy+Top); pick kMx,kMy so that lands strictly inside the 44x40 output
// (kMx+Left < 44 -> kMx<32; kMy+Top < 40 -> kMy<20).
constexpr uint32_t kMx = 20, kMy = 10;  // -> output (32, 30), inside the cropped rect

bool g_bugMarkerOutside = false;  // injectBug: paint marker where the shift probe won't find it

// Source painter: paint a black field + one green marker into the output the resident driver
// allocated for this node (at the Resolution-pin size). Overrides RenderTarget's cook fn (which has
// a real registered NodeSpec, so findSpec resolves it in the resident walk). No GPU work here.
void texSource(TexCookCtx& c) {
  if (!c.output) return;
  const uint32_t w = (uint32_t)c.output->width();
  const uint32_t h = (uint32_t)c.output->height();
  std::vector<uint8_t> px((size_t)w * h * 4, 0);
  for (size_t i = 0; i < (size_t)w * h; ++i) px[i * 4 + 3] = 255;  // opaque black
  uint32_t mx = g_bugMarkerOutside ? 0u : kMx;  // bug: marker at (0,0) -> cropped away / shifted off
  uint32_t my = g_bugMarkerOutside ? 0u : kMy;
  if (mx < w && my < h) {
    size_t mi = ((size_t)my * w + mx) * 4;
    px[mi + 0] = 0; px[mi + 1] = 255; px[mi + 2] = 0; px[mi + 3] = 255;  // green
  }
  c.output->replaceRegion(MTL::Region::Make2D(0, 0, w, h), 0, px.data(), w * 4);
}

Symbol atomicOp(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}

}  // namespace

int runResidentCropSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  g_bugMarkerOutside = injectBug;

  // Override RenderTarget's cook fn with the pattern painter (parity-selftest pattern: a REAL
  // registered op so findSpec resolves it). Crop is already self-registered (ImageFilterComputeOp).
  registerTexOp("RenderTarget", texSource);

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) {
    std::printf("[selftest-cropresident] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // Resident lib: RenderTarget#1 (Texture2D out) -> Crop#2.Image; Crop#2.out -> Root boundary out.
  SymbolLibrary lib;
  lib.symbols["RenderTarget"] = atomicOp(
      "RenderTarget",
      {{"command", "command", "Command", 0.0f}, {"Resolution", "Resolution", "Float", 0.0f},
       {"CustomW", "CustomW", "Float", 512.0f}, {"CustomH", "CustomH", "Float", 512.0f}},
      {{"out", "out", "Texture2D", 0.0f}});
  lib.symbols["Crop"] = atomicOp(
      "Crop",
      {{"Image", "Image", "Texture2D", 0.0f},
       {"LeftRight.x", "LeftRight", "Float", 0.0f}, {"LeftRight.y", "LeftRight.y", "Float", 0.0f},
       {"TopBottom.x", "TopBottom", "Float", 0.0f}, {"TopBottom.y", "TopBottom.y", "Float", 0.0f},
       {"PaddingColor.r", "PaddingColor", "Float", 1.0f},
       {"PaddingColor.g", "PaddingColor.g", "Float", 1.0f},
       {"PaddingColor.b", "PaddingColor.b", "Float", 1.0f},
       {"PaddingColor.a", "PaddingColor.a", "Float", 0.0f}},
      {{"out", "out", "Texture2D", 0.0f}});
  Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
  root.outputDefs = {{"out", "out", "Texture2D", 0.0f}};
  SymbolChild c1; c1.id = 1; c1.symbolId = "RenderTarget";
  SymbolChild c2; c2.id = 2; c2.symbolId = "Crop";
  c2.overrides["LeftRight.x"] = (float)kLeft;  c2.overrides["LeftRight.y"] = (float)kRight;
  c2.overrides["TopBottom.x"] = (float)kTop;   c2.overrides["TopBottom.y"] = (float)kBottom;
  c2.overrides["PaddingColor.r"] = 1.0f; c2.overrides["PaddingColor.g"] = 0.0f;
  c2.overrides["PaddingColor.b"] = 1.0f; c2.overrides["PaddingColor.a"] = 1.0f;  // magenta pad
  root.children = {c1, c2};
  root.connections = {
      {1, "out", 2, "Image"},
      {2, "out", kSymbolBoundary, "out"},
  };
  lib.symbols["Root"] = root; lib.rootId = "Root";
  ResidentEvalGraph rg = buildEvalGraph(lib, "Root");

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  // Window 64x64: RenderTarget#1 (Resolution defaults to WindowFollow) -> 64x64 source texture.
  // Crop's SizeFn then shrinks its OWN output to 44x40 from that cooked input — the seam under test.
  PointGraph pg(dev, mlib, q, kInW, kInH);
  pg.cookResident(rg, ctx, /*reg=*/nullptr, /*targetPath=*/"2");

  MTL::Texture* out = pg.target();  // = displayTex = Crop's resolution-sized output

  // (1) DIMS: resident sized Crop's output via the SizeFn from the cooked input (44x40), NOT the
  // 64x64 Resolution pin. On the pre-seam resident cook this would be 64x64 -> FAILS here.
  uint32_t ow = out ? (uint32_t)out->width() : 0;
  uint32_t oh = out ? (uint32_t)out->height() : 0;
  bool dimsOk = (ow == kOutW && oh == kOutH);

  // Read back only if the dims are what we expect (a wrong-sized texture means the seam didn't run).
  std::vector<uint8_t> buf;
  if (out) {
    buf.assign((size_t)ow * oh * 4, 0);
    out->getBytes(buf.data(), ow * 4, MTL::Region::Make2D(0, 0, ow, oh), 0);
  }

  // (2) SHIFT: the kernel computes output(i) = source(i.x-Left, i.y-Top), so the source marker at
  // (Mx,My) surfaces at output (Mx+Left, My+Top). Requires ShaderWrite to have landed (else garbage).
  int sx = (int)kMx + kLeft;  // 32
  int sy = (int)kMy + kTop;   // 30
  int gR = -1, gG = -1, gB = -1;
  bool markerShifted = false;
  if (dimsOk && sx >= 0 && sy >= 0 && (uint32_t)sx < ow && (uint32_t)sy < oh) {
    size_t si = ((size_t)sy * ow + sx) * 4;
    gR = buf[si + 0]; gG = buf[si + 1]; gB = buf[si + 2];
    markerShifted = (gG > 200) && (gR < 60) && (gB < 60);
  }

  // (3) PADDING: output (2, 5) maps to source x = 2-Left = -10 < 0 -> padded magenta.
  int pR = -1, pG = -1, pB = -1;
  bool paddedMagenta = false;
  if (dimsOk) {
    const uint32_t pX = 2, pY = 5;
    size_t pi = ((size_t)pY * ow + pX) * 4;
    pR = buf[pi + 0]; pG = buf[pi + 1]; pB = buf[pi + 2];
    paddedMagenta = (pR > 200) && (pG < 60) && (pB > 200);
  }

  // (4) FULL COVERAGE: a compute write into a ShaderWrite output covers every pixel. Without the
  // ShaderWrite flag the kernel write is dropped -> no opaque cropped pixels. We assert every pixel
  // is opaque (alpha==255 from either the source band or the magenta pad) -> non-zero coverage.
  int unwritten = 0;
  if (dimsOk) {
    for (size_t i = 0; i < (size_t)ow * oh; ++i)
      if (buf[i * 4 + 3] != 255) ++unwritten;
  }
  bool fullyCovered = dimsOk && (unwritten == 0);

  bool pass = dimsOk && markerShifted && paddedMagenta && fullyCovered;
  std::printf("[selftest-cropresident] out=%ux%u(want %ux%u,dimsOk=%d) marker@(%d,%d)=(R%d,G%d,B%d)"
              " shifted=%d pad=(R%d,G%d,B%d) magenta=%d unwritten=%d covered=%d -> %s\n",
              ow, oh, kOutW, kOutH, dimsOk ? 1 : 0, sx, sy, gR, gG, gB, markerShifted ? 1 : 0,
              pR, pG, pB, paddedMagenta ? 1 : 0, unwritten, fullyCovered ? 1 : 0,
              pass ? "PASS" : "FAIL");

  g_bugMarkerOutside = false;
  mlib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
