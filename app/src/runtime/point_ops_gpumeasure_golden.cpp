// runtime/point_ops_gpumeasure_golden — GpuMeasure GPU-time SMOKE golden.
//
// ★SMOKE-LEVEL (declared honestly per GOLDEN_STANDARD "有狀態/emergent 的 op ... 沒有 closed-form 就明標
// smoke 級"): the raw GPU time is hardware- and load-dependent — there is NO closed-form expected value to
// assert. This golden proves the MEASUREMENT MECHANISM is WIRED, not a numeric parity:
//   (1) after a real render flows through GpuMeasure, the µs latch is NON-ZERO (the executor recorded a real
//       MTLCommandBuffer.GPUEndTime−GPUStartTime — the mechanism is connected end-to-end);
//   (2) the ms smoothing is MONOTONE toward the raw sample and STRICTLY BETWEEN the start value and the raw
//       target (the TiXL 0.03 lerp, GpuMeasure.cs:74 — proves the smoothing formula is applied, not a raw copy).
//
// injectBug (real cook seam, GOLDEN_STANDARD polarity): gpuMeasureDisableRecordForTest() makes the executor
// hook SKIP the record (as if GpuMeasure were unwired) → the latch stays at its seeded value → the "non-zero
// after render" assertion goes RED. Corrupts the REAL record path, not the expected value.
#include "runtime/point_ops_gpumeasure.h"

#include <cmath>
#include <cstdio>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"                  // Graph/Node/pinId
#include "runtime/point_graph.h"            // PointGraph::cook, registerBuiltinPointOps
#include "runtime/tixl_point.h"             // EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

int runGpuMeasureSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-gpumeasure] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();

  // Graph: RadialPoints → DrawPoints → GpuMeasure → RenderTarget. The GpuMeasure wraps the DrawPoints
  // command subtree; the RenderTarget executor runs it in ONE command buffer and records the GPU time.
  Graph g;
  Node gen; gen.id = 1; gen.type = "RadialPoints";
  gen.params["Count"] = 256.0f; gen.params["Radius"] = 2.0f; g.nodes.push_back(gen);
  Node drw; drw.id = 2; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  Node gm; gm.id = 3; gm.type = "GpuMeasure"; g.nodes.push_back(gm);
  Node rt; rt.id = 4; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f; rt.params["CustomW"] = 256.0f; rt.params["CustomH"] = 256.0f;
  g.nodes.push_back(rt);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // RadialPoints.points → DrawPoints.points
  g.connections.push_back({102, pinId(2, 1), pinId(3, 3)});  // DrawPoints.out → GpuMeasure.Command (input port 3)
  g.connections.push_back({103, pinId(3, 0), pinId(4, 0)});  // GpuMeasure.Output → RenderTarget.command

  // Seed the latch to a KNOWN start so we can observe (a) it moved (non-zero record) and (b) the smoothing
  // lands strictly between start and the raw target (not a raw copy, not unchanged).
  gpuMeasureLastMs() = 0.0;
  gpuMeasureLastUs() = 0;

  gpuMeasureDisableRecordForTest() = injectBug;  // -bug: executor SKIPS the record → latch stays 0

  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  PointGraph pg(dev, lib, q, 256, 256);
  pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));

  const long us = gpuMeasureLastUs();
  const double ms = gpuMeasureLastMs();
  const double targetMs = (double)us / 1000.0;

  gpuMeasureDisableRecordForTest() = false;  // reset (never leak)

  // (1) mechanism wired: a real render recorded a non-zero GPU time.
  const bool nonZero = us > 0;
  // (2) smoothing applied (only meaningful when a sample was recorded): ms moved from 0 toward targetMs by
  //     exactly the 0.03 step (ms ≈ 0.03·targetMs), so 0 < ms < targetMs. If us==0 (the bug leg), this is
  //     vacuously skipped — the non-zero check already carries the RED.
  const double expectedMs = 0.03 * targetMs;  // Lerp(0, targetMs, 0.03)
  const bool smoothed = !nonZero || (ms > 0.0 && ms < targetMs && std::fabs(ms - expectedMs) < 1e-6);

  const bool ok = nonZero && smoothed;
  std::printf("[selftest-gpumeasure] us=%ld ms=%.6f (target=%.6f, expected 0.03·target=%.6f) nonZero=%d "
              "smoothed=%d%s -> %s\n", us, ms, targetMs, expectedMs, nonZero ? 1 : 0, smoothed ? 1 : 0,
              injectBug ? " (injectBug→record disabled)" : "", ok ? "PASS" : "tripped");

  lib->release(); q->release(); dev->release(); pool->release();
  // Polarity (GOLDEN_STANDARD): -bug bit (latch stayed 0 → nonZero false) → ok=false → 1; did-not-trip → 0.
  return ok ? 0 : 1;
}

}  // namespace sw
