// app/src/selftests_window_follow.cpp — area manifest leaf for the --selftest router:
// S1-fill window-follow (PointGraph::setWindowSize → cook-entry apply → window `target` rebuild).
//
// Shell-tier (app/src/ root): self-registers its row into selftestRegistry() during pre-main
// dynamic init. LEAF-LOCAL — includes its own headers; selftests_decls.h is NOT touched.
// Reached via --selftest-window-follow (+ the -bug refuter).
//
// Ground truth: TiXL's Fill resolution is the LIVE Output-window size, re-read every frame —
//   ResolutionHandling.cs:120   `var windowSize = ImGui.GetWindowSize();`
//   ResolutionHandling.cs:124-127  Fill (Width/Height <= 0) returns that window size verbatim
//   OutputWindow.cs:411-414     seeds EvaluationContext.RequestedResolution from it EVERY frame
// sw's seam: the Output window pushes its content region each frame (setWindowSize records it);
// the cook entry applies it (Impl::seedFrameResolution) and rebuilds the window-sized `target`
// ONLY when the size actually changed. This tooth proves all four legs headless:
//   (1) the push is DEFERRED — no rebuild happens before a cook (mid-imgui-frame safety),
//   (2) after a cook the resolution accessors AND the real Metal texture adopt the new size,
//   (3) an unchanged push re-cooked does NOT reallocate (pointer-stable target — zero churn),
//   (4) the frame override (a selected preset) still wins over the window size (precedence).
// injectBug: skip the cook between the push and the assertions — the "texture resized" check
// MUST go red, proving the green pass is caused by the cook-entry apply, not by the push alone.
#include "runtime/selftest_registry.h"

#include <cstdio>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"
#include "runtime/graph.h"
#include "runtime/point_graph.h"

namespace sw {

int runWindowFollowSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-window-follow] FAIL: no Metal device\n");
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);  // no shaders needed (empty-graph cook → clearTarget)

  int rc = 0;
  auto check = [&](bool cond, const char* msg) {
    if (!cond) {
      std::printf("[selftest-window-follow] FAIL: %s\n", msg);
      rc = 1;
    }
  };
  Graph g;                   // EMPTY graph: cook() resolves no target node → clearTarget path,
  EvaluationContext ctx{};   // which still runs the cook ENTRY (where the pending resize applies).

  // Leg 0 — construction baseline: accessors and the real texture agree on the ctor size.
  check(pg.windowResolution().w == 64 && pg.windowResolution().h == 64, "ctor windowResolution != 64x64");
  check(pg.frameResolution().w == 64 && pg.frameResolution().h == 64, "ctor frameResolution != 64x64");
  check(pg.target() && pg.target()->width() == 64 && pg.target()->height() == 64, "ctor target != 64x64");

  // Leg 1 — the push is DEFERRED: nothing changes until a cook applies it (mid-frame safety).
  pg.setWindowSize(80, 60);
  check(pg.windowResolution().w == 64 && pg.windowResolution().h == 64,
        "windowResolution changed BEFORE cook (apply must be deferred to cook entry)");
  check(pg.target()->width() == 64, "target rebuilt BEFORE cook (mid-imgui-frame release hazard)");

  // Leg 2 — a cook applies the pending size: accessors AND the Metal texture adopt 80x60.
  // BUG: skip the cook — the pending size is never applied, the checks below bite.
  if (!injectBug) pg.cook(g, ctx, nullptr, /*targetNodeId=*/0);
  check(pg.windowResolution().w == 80 && pg.windowResolution().h == 60,
        "windowResolution did not follow the pushed window size after cook");
  check(pg.frameResolution().w == 80 && pg.frameResolution().h == 60,
        "frameResolution (Fill, no override) did not follow the window size");
  check(pg.target() && pg.target()->width() == 80 && pg.target()->height() == 60,
        "window target texture was not rebuilt to the pushed size");

  // Leg 3 — rebuild ONLY on change: an identical push re-cooked keeps the SAME texture object.
  MTL::Texture* before = pg.target();
  pg.setWindowSize(80, 60);
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/0);
  check(pg.target() == before, "unchanged size push reallocated the target (per-frame churn)");

  // Leg 3b — degenerate pushes are dropped (collapsed window keeps the last size).
  pg.setWindowSize(0, 99);
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/0);
  check(pg.windowResolution().w == 80 && pg.windowResolution().h == 60, "degenerate push not dropped");

  // Leg 4 — precedence intact: a frame override (selected preset) wins over the window size;
  // clearing it falls back to the LIVE window size (TiXL export > selector > Fill).
  pg.setFrameResolutionOverride(RenderResolution{1920, 1080});
  check(pg.frameResolution().w == 1920 && pg.frameResolution().h == 1080, "override lost to window size");
  check(pg.windowResolution().w == 80 && pg.windowResolution().h == 60, "override leaked into windowResolution");
  pg.clearFrameResolutionOverride();
  check(pg.frameResolution().w == 80 && pg.frameResolution().h == 60, "clear did not fall back to window");

  q->release();
  dev->release();
  pool->release();
  if (rc == 0) std::printf("[selftest-window-follow] PASS\n");
  return rc;
}

// High orderBase → APPENDS at the end of --selftest-list (registry sorts by order; max in use 800).
REGISTER_SELFTESTS(/*orderBase=*/810, {"window-follow", runWindowFollowSelfTest});

}  // namespace sw
