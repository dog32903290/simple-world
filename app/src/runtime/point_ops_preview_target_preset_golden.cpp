// point_ops_preview_target_preset_golden — the RED-face tooth the ORIGINAL resolution-selector .scn +
// selftest-requestedresolution-frame MISSED: the default preview surface (a DrawPoints/Command terminal
// with NO RenderTarget node) must adopt a fixed preset. Root cause of the shipped bug (point_graph_debug
// .cpp seedFrameResolution): the preview `target` texture was sized from the WINDOW and ignored the frame
// override, so picking 1080p/4k left previewTexture()->width()/height() (main.cpp previewTextureSize →
// the WxH overlay + Output preview) window-sized until a RenderTarget node happened to route displayTex.
//
// WHY THE OLD TESTS DIDN'T CATCH IT: selftest-requestedresolution-frame + output_resolution_selector.scn
// both build a RenderTarget terminal ON PURPOSE (the .scn header even says "a Command terminal ignores
// the override — its texture follows the WINDOW"). That routes displayTex (its OWN ensureTex texture,
// which DID adopt requestedResolution) → the override became visible THROUGH the RenderTarget, masking
// that the default `target` never did. This tooth removes the RenderTarget and asserts on target() itself.
//
// EXPECTED VALUE (independent-of-impl): TiXL Resolution.ComputeResolution (ResolutionHandling.cs:117-118)
// returns the FIXED Size verbatim for a non-aspect preset; OutputWindow.cs:411-414 seeds RequestedResolution
// from it every frame and the output texture adopts it. So a fixed 1920×1080 preset → the preview surface
// is EXACTLY 1920×1080, regardless of the 800×600 window. 1080p is the shipped preset (kResPresets, output_
// window_resolution.cpp:27); the number is TiXL's default table (ResolutionHandling.cs:75 `new("1080p",
// 1920, 1080)`), not sw's own output — impl-independent.
//
// injectBug (real cook-path corruption, GOLDEN_STANDARD 特徵3): debugSetTargetFollowsWindowBug(true) flips
// the Impl flag seedFrameResolution reads → `target` is sized from the window, ignoring the override → the
// preview stays 800×600 after the 1080p pick → target()->width()!=1920 → the tooth BITES (RED). Not a
// want-flip: the assertion is unchanged; the COOK regresses.
#include "runtime/point_ops.h"

#include "runtime/point_graph.h"        // PointGraph, RenderResolution, registerBuiltinPointOps
#include "runtime/selftest_registry.h"  // REGISTER_SELFTESTS

#include <cstdint>
#include <cstdio>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"       // Graph / Node / pinId
#include "runtime/tixl_point.h"  // EvaluationContext

namespace sw {

// A DrawPoints Command terminal (NO RenderTarget node) → defaultDrawTarget picks the Command → the cook
// realizes into p_->target → target() returns p_->target (displayTex is null). This is the DEFAULT preview
// path the bug lived on. Leg A (Fill) pins target()==window; Leg B (1080p override) must retarget target()
// to 1920×1080 — the face the old RenderTarget-terminal tests could not reach.
int runPreviewTargetPresetSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 128, WINW = 800, WINH = 600, PW = 1920, PH = 1080;  // 1080p preset (TiXL table)

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-preview-target-preset] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();  // RadialPoints + DrawPoints

  PointGraph pg(dev, lib, q, WINW, WINH);  // window 800×600
  if (injectBug) pg.debugSetTargetFollowsWindowBug(true);  // real cook regresses (target ← window)
  Graph g;
  // RadialPoints (id 1) → DrawPoints (id 2). NO RenderTarget node: defaultDrawTarget → DrawPoints Command
  // terminal, which draws into p_->target (the default preview surface). target()==p_->target (no displayTex).
  Node gen; gen.id = 1; gen.type = "RadialPoints";
  gen.params["Count"] = (float)N; gen.params["Radius"] = 2.0f; g.nodes.push_back(gen);
  Node drw; drw.id = 2; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // RadialPoints.points → DrawPoints.points

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  // ── Leg A: Fill (no override) → the preview surface is the 800×600 window (byte-identical-to-window). ──
  pg.clearFrameResolutionOverride();
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/2);
  MTL::Texture* texA = pg.target();
  uint32_t aw = texA ? (uint32_t)texA->width() : 0;
  uint32_t ah = texA ? (uint32_t)texA->height() : 0;
  bool fillOk = texA && aw == WINW && ah == WINH;

  // ── Leg B: 1080p preset (fixed 1920×1080) → the preview surface RETARGETS to 1920×1080, NOT the window.
  // The window is STILL 800×600 (no resize) — proving the retarget is driven by the OVERRIDE, not a resize.
  pg.setFrameResolutionOverride(RenderResolution{PW, PH});
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/2);
  MTL::Texture* texB = pg.target();
  uint32_t bw = texB ? (uint32_t)texB->width() : 0;
  uint32_t bh = texB ? (uint32_t)texB->height() : 0;
  bool sizedB = texB && bw == PW && bh == PH;

  bool pass = fillOk && sizedB;
  std::printf("[selftest-preview-target-preset] fill=%ux%u(want %ux%u) preset1080p=%ux%u(want %ux%u) -> %s\n",
              aw, ah, WINW, WINH, bw, bh, PW, PH, pass ? "PASS" : "FAIL");

  pg.clearFrameResolutionOverride();
  lib->release(); q->release(); dev->release(); pool->release();

  if (injectBug) {
    if (pass) {  // the bug did not trip (target still 1920×1080 despite target-follows-window forced) →
      // return 0 so --bite's NO-BITE list catches the dead tooth (GOLDEN_STANDARD 特徵3), never a false green.
      std::printf("[selftest-preview-target-preset] did not trip: injectBug still sized the preview target "
                  "1920×1080 — the target-follows-window seam is hollow, the tooth cannot bite\n");
      return 0;
    }
    std::printf("[selftest-preview-target-preset] injectBug correctly RED (preview target stayed at the "
                "%ux%u window after the 1080p pick, not %ux%u)\n", WINW, WINH, PW, PH);
    return 1;
  }
  return pass ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/312, {"preview-target-preset", runPreviewTargetPresetSelfTest});

}  // namespace sw
