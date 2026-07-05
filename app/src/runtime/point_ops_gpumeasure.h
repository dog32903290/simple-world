// runtime/point_ops_gpumeasure — GpuMeasure (TiXL render/analyze): GPU-time measurement of a wrapped
// Command subtree, Command-rail passthrough + value-rail LastMeasureInMs/µs latch. Tiny own-header (the
// GetScreenPos/GetPosition precedent — point_ops.h is at its ratchet cap). runtime leaf: no upward deps.
//
// SMOKE-LEVEL (declared honestly, GOLDEN_STANDARD "no closed-form → smoke"): the raw GPU time is
// hardware/load dependent and has NO closed-form expected. The golden proves the MEASUREMENT MECHANISM is
// wired (value non-zero after a real render, monotone smoothing converges toward the raw sample), NOT a
// numeric parity value. See point_ops_gpumeasure_golden.cpp.
#pragma once

namespace sw {

// Register the "GpuMeasure" cmd op; called by registerDrawPointOps.
void registerGpuMeasureOp();

// Executor hook (ONE-line call from cookRenderTarget after waitUntilCompleted): record the whole-buffer GPU
// time (SECONDS = MTLCommandBuffer.GPUEndTime − GPUStartTime) IFF a GpuMeasure is active in this cook.
// Converts to µs + applies TiXL's 0.03 exponential lerp to the ms latch (GpuMeasure.cs:65,74). No-op when
// no GpuMeasure is in the graph (gpuMeasureActiveThisFrame() false) → zero-cost for every other graph.
void gpuMeasureRecordBufferTime(double bufferSeconds);

// True iff a GpuMeasure op cooked this frame (its cook sets the per-frame flag). The executor consults this
// so the timing readback only runs when the graph actually contains a GpuMeasure — every existing render is
// byte-identical (the hook is skipped). Reset at the start of each RenderTarget executor pass.
bool gpuMeasureActiveThisFrame();
void gpuMeasureResetActive();  // executor calls this before cooking a subtree (clears the per-frame flag)

// Test-only latch access (the smoke golden reads/seeds these). lastMs = the smoothed ms value; lastUs = the
// last raw µs sample. Exposed so the golden can assert non-zero + monotone convergence without a real GPU.
double& gpuMeasureLastMs();
long& gpuMeasureLastUs();

// Test-only tooth: when true the executor hook is SKIPPED (as if the GpuMeasure mechanism were unwired) — the
// latch never updates → the golden's "value becomes non-zero after a render" assertion goes RED. OFF in
// production. CPU op flag (constitution rule).
bool& gpuMeasureDisableRecordForTest();

// --selftest entry (point_ops_gpumeasure_golden.cpp).
int runGpuMeasureSelfTest(bool injectBug);

}  // namespace sw
