// runtime/point_ops_gpumeasure — GpuMeasure command op + value-rail LastMeasureInMs/µs latch.
// TiXL authority: external/tixl/Operators/Lib/render/analyze/GpuMeasure.cs (+ .t3 defaults).
//
// TiXL GpuMeasure wraps a Command subtree in a D3D11 timestamp-query pair and reports its GPU time
// (GpuMeasure.cs:31-80):
//   - three D3D11 queries (TimestampDisjoint + two Timestamps) bracket Command.GetValue(context)   (cs:19-43)
//   - PING-PONGED across frames (_readyToMeasure toggles): frame N issues, frame N+1 fetches         (cs:37-55)
//   - durationInS = (frameEnd − frameBegin) / disjoint.Frequency                                     (cs:60)
//   - usDuration  = (int)(durationInS · 1e6)                                                          (cs:65)
//   - LastMeasureInMicroSeconds = usDuration                                                          (cs:70)
//   - LastMeasureInMs = Lerp(LastMeasureInMs, LastMeasureInMicroSeconds / 1000.0, 0.03)  (smoothed)   (cs:74)
//   It is a PER-SUBTREE (whole wrapped chain) measurement, NOT per-draw; the value is latched one frame late.
//
// ★METAL MAPPING (the simplest parity-reachable mechanism — workorder GpuMeasure choice): sw's render runs the
// wrapped subtree's draw items in ONE MTLCommandBuffer (cookRenderTarget commits+waits it). MTLCommandBuffer
// exposes GPUStartTime/GPUEndTime (whole-buffer GPU wall time, SECONDS) — the DIRECT analog of TiXL's
// timestamp-pair around the subtree. So the executor, after waitUntilCompleted, records
// (GPUEndTime − GPUStartTime) here; this leaf converts to µs and applies TiXL's SAME 0.03 lerp (cs:65,74).
// No ping-pong: MTLCommandBuffer.GPU*Time is valid AFTER waitUntilCompleted (already synchronous in sw's
// executor) — the one-frame latency TiXL's query fetch imposes is absent, which is faithful in VALUE (the
// measured duration is the same subtree's GPU time) and simpler in mechanism (fork-gpumeasure-sync-no-pingpong).
//
// ★CROSS-RAIL (Command cook → value rail): the GetScreenPos/GetPosition latch pattern. The cmd cook forwards
// the subtree items (passthrough, like SetRequestedResolution) AND sets the per-frame active flag; the
// executor records the buffer GPU time into the process latch; the value rail reads LastMeasureInMs (out[1])
// and LastMeasureInMicroSeconds (out[2]) through the stateful-value sink.
// FORKS: fork-gpumeasure-single-latch (one process latch, cc.nodeId=0 — multiple GpuMeasure share the last
//   render's time, the GetScreenPos single-latch precedent); fork-gpumeasure-whole-buffer (sw times the whole
//   RenderTarget command buffer, not a sub-subtree query — v1 single-buffer executor; a nested GpuMeasure
//   inside one RenderTarget measures the same buffer). SMOKE-level golden (no closed-form GPU time).
#include "runtime/point_ops_gpumeasure.h"

#include <map>
#include <string>

#include "runtime/point_graph.h"            // CmdCookCtx, registerCmdOp
#include "runtime/render_command.h"         // RenderCommand
#include "runtime/stateful_value_op_registry.h"  // StatefulOpReg (value-rail step self-registration)
#include "runtime/stateful_value_ops.h"          // StatefulValueState/TransportSnapshot/ContextVarMap decls

namespace sw {

double& gpuMeasureLastMs() { static double v = 0.0; return v; }
long& gpuMeasureLastUs() { static long v = 0; return v; }

bool& gpuMeasureDisableRecordForTest() { static bool f = false; return f; }

namespace {

// Per-frame active flag: set true by cookGpuMeasure (a GpuMeasure is in the graph this cook), cleared by
// gpuMeasureResetActive() at the start of the executor pass. Guards the executor's timing readback so every
// non-GpuMeasure graph pays zero cost (the hook is skipped).
bool& activeFlag() { static bool f = false; return f; }

}  // namespace

bool gpuMeasureActiveThisFrame() { return activeFlag(); }
void gpuMeasureResetActive() { activeFlag() = false; }

// Executor hook (cookRenderTarget, after waitUntilCompleted). bufferSeconds = GPUEndTime − GPUStartTime.
// RECORD + DISARM in one call: after recording (or skipping), the per-frame active flag is cleared so a
// later un-measured RenderTarget in the same frame does not re-record on a stale flag.
void gpuMeasureRecordBufferTime(double bufferSeconds) {
  const bool armed = activeFlag() && !gpuMeasureDisableRecordForTest();
  activeFlag() = false;                     // disarm (whether or not we record — one flag per cook)
  if (!armed) return;                       // no GpuMeasure / -bug → latch untouched
  if (bufferSeconds < 0.0) bufferSeconds = 0.0;  // clamp a stale/unsynced buffer to 0
  const long us = (long)(bufferSeconds * 1e6);   // GpuMeasure.cs:65 (int)(durationInS·1e6)
  gpuMeasureLastUs() = us;                        // cs:70 LastMeasureInMicroSeconds
  // cs:74 LastMeasureInMs = Lerp(LastMeasureInMs, us/1000.0, 0.03) — exponential smoothing toward the raw ms.
  const double targetMs = (double)us / 1000.0;
  gpuMeasureLastMs() += (targetMs - gpuMeasureLastMs()) * 0.03;
}

namespace {

// GpuMeasure (TiXL render/analyze/GpuMeasure.cs): Command PASSTHROUGH — forwards the wrapped subtree items
// (the driver already cooked them into cc.inputCommand, like SetRequestedResolution) AND arms the per-frame
// active flag so the executor's timing readback runs for this render. .t3 defaults Enabled=true (v1: always
// measure; the Enabled=false gate is a cheap follow-on — cs:37 skips the query when disabled).
RenderCommand cookGpuMeasure(CmdCookCtx& c) {
  activeFlag() = true;  // arm the executor timing hook for this cook
  RenderCommand rc;
  if (c.inputCommand) rc.items = c.inputCommand->items;  // forward the wrapped subtree (the timed region)
  return rc;
}

// Value-rail step (the stateful-value sink): LastMeasureInMs at spec port index 1, LastMeasureInMicroSeconds
// at index 2 (Output Command is port 0). cookStatefulValueNodes copies out[i] → extOut[i] absolutely.
void stepGpuMeasure(const std::map<std::string, float>&, float, float, StatefulValueState&, float out[8],
                    const TransportSnapshot&, ContextVarMap*, const std::string&) {
  out[1] = (float)gpuMeasureLastMs();          // LastMeasureInMs (smoothed)
  out[2] = (float)gpuMeasureLastUs();          // LastMeasureInMicroSeconds (raw)
}

StatefulOpReg _reg_GpuMeasureStep{"GpuMeasure", stepGpuMeasure};

}  // namespace

void registerGpuMeasureOp() { registerCmdOp("GpuMeasure", cookGpuMeasure); }

}  // namespace sw
