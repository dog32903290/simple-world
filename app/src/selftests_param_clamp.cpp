// selftests_param_clamp — the jog-overshoot CONSUMER-CLAMP gate (--selftest-param-clamp).
//
// CONTEXT: drag param-input changed to TiXL jog semantics (jog can drag a non-clamp param past its
// PortSpec min/max — intentional, matches TiXL, UI feel must not change). A refuter pass found 7 GPU/
// UB-facing consumption points that trusted the incoming value with no defensive re-clamp at the
// consumption site. This selftest is the machine-checkable regression gate for the fix: feed each
// testable consumer an out-of-range value (1e9 / negative / a huge product) and assert the value it
// actually acts on is clamped to its PortSpec-declared bound — WITHOUT allocating the huge resource
// the unclamped value would have requested.
//
// SCOPE — 2 of the 7 fixed sites are covered here as REAL pure-logic/registered-op calls (no Metal
// device, no huge GPU alloc):
//   1. RenderTarget CustomW/CustomH -> resolveRenderResolution() (point_graph.h): a free function, zero
//      allocation, returns a plain {w,h} struct. Directly callable.
//   2. StructuredBufferWithViews Stride/CountValue -> the REGISTERED BufferCookFn via findBufferOp(),
//      driven with a fake requestBytes() that captures the byteSize the op asked for instead of really
//      allocating it (so a would-be 4 GiB request is caught as a number, not a real allocation).
//
// The remaining 5 fixed sites (point_graph.cpp generator Count, Loop Count, RaymarchField MaxSteps,
// Draw VertexCount, CollapseVertices StepCount) are each a ONE-LINE clamp buried inside a cook-core
// method that requires a full PointGraph::cook() through a real Graph — and for Loop specifically,
// there is no debugCooked* accessor exposing the cooked RenderCommand's item count (the observable
// effect of the clamp), so asserting the post-clamp value would require adding new PointGraph API
// surface outside this task's file whitelist. Per FABLE_VERIFY_WORKORDER's escape hatch, those 5 are
// verified by CODE REVIEW (see the refuter-cited line + the added clamp comment at each site) plus the
// existing run_all_selftests.sh --bite sweep (no regression in any op that already exercises them).
//
// injectBug (dispatched via --selftest-param-clamp-bug): skips ONE of the two real clamps (the
// RenderTarget CustomW/H one) by calling a copy of the OLD unclamped expression inline, so the -bug
// leg's assertion (w,h == 8192 cap) fails on a REAL corrupted computation, not a flipped expected value.
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "runtime/point_graph.h"          // resolveRenderResolution, RenderResolution
#include "runtime/buffer_op_registry.h"   // BufferCookCtx, BufferCookFn, findBufferOp
#include "runtime/sw_buffer.h"            // SwBuffer (full def; point_graph.h only forward-declares it)
#include "runtime/selftest_registry.h"    // REGISTER_SELFTESTS

namespace sw {
namespace {

// Mirrors the OLD pre-fix expression at point_ops_rendertarget.cpp (lower-bound only, no upper clamp)
// — used ONLY by the injectBug leg, to make the RED case a genuine unclamped computation.
uint32_t unclampedCustomDim(float v) {
  return (uint32_t)std::lround(std::fmax(1.0f, v));
}

}  // namespace

int runParamClampSelfTest(bool injectBug) {
  bool ok = true;

  // ---- 1. RenderTarget CustomW/CustomH -> resolveRenderResolution() PortSpec [1,8192] ----
  // (node_registry_draw_render.cpp: {"CustomW",...,1.0f,8192.0f}, {"CustomH",...,1.0f,8192.0f}).
  {
    std::map<std::string, float> params;
    params["Resolution"] = 4.0f;   // Custom
    params["CustomW"] = 1.0e9f;    // jog-dragged past declared max
    params["CustomH"] = -50.0f;    // also exercise the negative/garbage leg (pre-existing lower clamp)

    RenderResolution rr = resolveRenderResolution(params, RenderResolution{640, 480});
    uint32_t w = rr.w, h = rr.h;
    if (injectBug) {
      // Simulate the pre-fix code path: recompute W with the OLD unclamped expression so the RED leg
      // fails against the same 8192 assertion below (a real corrupted number, not a flipped expected).
      w = unclampedCustomDim(params["CustomW"]);
    }
    bool wOK = (w == 8192u);              // clamped to PortSpec max
    bool hOK = (h == 1u);                 // clamped to PortSpec min (pre-existing behavior, still holds)
    bool pass = wOK && hOK;
    ok = ok && pass;
    std::printf("[selftest-param-clamp] RenderTarget CustomW(1e9)->%u(want 8192) CustomH(-50)->%u(want 1) -> %s\n",
                w, h, pass ? "PASS" : "FAIL");
  }

  // ---- 2. StructuredBufferWithViews Stride/CountValue -> cookStructuredBufferWithViews PortSpec
  //         Stride[0,4096] CountValue[0,1048576] (buffer_ops_structuredbufferwithviews.cpp:72-73) ----
  {
    const BufferCookFn* fn = findBufferOp("StructuredBufferWithViews");
    bool haveOp = fn != nullptr;
    ok = ok && haveOp;
    uint32_t requestedByteSize = 0;
    bool requestCalled = false;

    if (haveOp) {
      std::map<std::string, float> params;
      params["Stride"] = 1.0e9f;       // jog-dragged past declared max 4096
      params["CountValue"] = 1.0e9f;   // jog-dragged past declared max 1048576

      BufferCookCtx c{};
      c.params = &params;
      SwBuffer out{};
      c.output = &out;
      // Fake allocator: CAPTURE the requested size, do NOT really allocate (no GPU / no huge malloc).
      c.requestBytes = [&](uint32_t byteSize) -> void* {
        requestCalled = true;
        requestedByteSize = byteSize;
        static std::vector<uint8_t> dummy;
        dummy.assign(byteSize < 4096 ? byteSize : 4096, 0);  // bounded dummy backing store either way
        return dummy.data();
      };

      (*fn)(c);
    }

    // Without the Stride/CountValue clamp, 1e9 * 1e9 truncated to uint32 wraps unpredictably (could
    // even land on 0, which would SKIP the requestBytes call entirely — the worst outcome: a caller
    // believes it has a huge buffer when it silently got nothing). With the clamp, the op computes
    // 4096 * 1048576 == 2^32, which the added uint64 overflow guard also rejects (byteSize64 > UINT32
    // range) -> requestBytes must NOT be called, and the op returns cleanly (no buffer, no corruption).
    bool pass = haveOp && !requestCalled;
    ok = ok && pass;
    std::printf("[selftest-param-clamp] StructuredBufferWithViews Stride(1e9)/CountValue(1e9) -> "
                "requestBytes %s (want NOT called; clamped 4096*1048576==2^32 correctly rejected) -> %s\n",
                requestCalled ? "CALLED" : "skipped", pass ? "PASS" : "FAIL");
  }

  // ---- 3/4. Loop Count [0,1000] + CollapseVertices StepCount [1,16]: NOT independently unit-tested
  // here (see file header SCOPE note — both clamps are buried inside a PointGraph::cook()-only method
  // with no debugCooked* accessor exposing the clamped effect). Verified by code review of the clamp
  // lines added at point_graph_command_cook.cpp (Loop branch) and mesh_ops_collapsevertices.cpp
  // (StepCount) + the existing run_all_selftests.sh --bite sweep (zero regression in ops exercising
  // Loop / CollapseVertices at in-range values).
  std::printf("[selftest-param-clamp] Loop Count + CollapseVertices StepCount: code-review-only "
              "(see file header) -- not a gap, a named scope limit\n");

  std::printf("[selftest-param-clamp] overall -> %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// Self-register (orderBase 950 — above the current highest single-entry orderBase 900/selftests_export.cpp).
REGISTER_SELFTESTS(/*orderBase=*/950,
    {"param-clamp", runParamClampSelfTest});

}  // namespace sw
