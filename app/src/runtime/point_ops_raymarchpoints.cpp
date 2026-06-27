// RaymarchPoints — SDF point-modify + COUNT-MULTIPLY op (TWO modes). Faithful port of
// external/tixl .../field/use/RaymarchPoints (.cs slots + .t3 plumbing) +
// .../Assets/shaders/points/modify/MovePointsForwardToSDF.hlsl (the two-mode raymarch-and-reflect kernel).
//
// COMBINES TWO PROVEN SEAMS (clone of point_ops_sdfreflectionlinepoints.cpp):
//   1. SDF field gather: this op has a DIRECT "Field" input port, so the cook drivers' one-hop direct-Field
//      gather (gatherPointFieldTree) builds the upstream SDF tree into c.inputFieldTree. The cook mirrors
//      cookSdfReflectionLinePoints: assembleFieldMSL fills the raymarch_points_template.metal hooks ->
//      cachedSourceComputePSO (srcHash-keyed) -> dispatch. (Runtime string template, NOT a precompiled kernel.)
//   2. Count-multiply: each SOURCE point emits PointCountPerLineReflections output points. The driver's
//      countTransform can't see the params, so we use the STATIC-STASH pattern: cook() computes
//      outputCount = sourceCount*PointCountPerLineReflections from c.inputCounts[0] + the params and writes it
//      to a file-static; raymarchPointsCountTransform() returns it (one-frame sizing lag on a fresh build,
//      exactly like SdfReflectionLinePoints).
//
// THE ENTANGLED COUNT (.t3 backward-trace, MODE-INDEPENDENT):
//   PointCountPerLine            = CountForALine = CompareInt(PointMode<0 ? 0 : MaxSteps) + 1.
//     CompareInt (df787169): Value=PointMode, TestValue=0, Mode default 0=IsSmaller -> PointMode<0 ALWAYS
//     false (PointMode is an enum >=0) -> ResultForFalse=MaxSteps. So PointCountPerLine = MaxSteps + 1.
//   PointCountPerLineReflections = CountWithReflections = PointCountPerLine * (clamp(MaxReflectionCount,0,10) + 1).
//   total output count           = sourceCount * PointCountPerLineReflections.
// Raymarch mode over-allocates (writes only MaxReflections+1 of those slots) but the .t3 does NOT branch the
// count per mode (CompareInt(IsSmaller,0) is unconditionally false) -> SAME allocation both modes. Faithful.
//
// DISPATCH is over SOURCE points (one thread = one source point writes a whole line), NOT the output count;
// the kernel guards `sourceIndex >= SourcePointCount`. NO field wired (or no template / compile fail) ->
// faithful pass-through copy of the input bag's first sourceCount slots (a raymarch op with no SDF can't
// march — mirrors the SdfReflectionLinePoints no-field contract).
//
// .t3 DEFAULTS (load-bearing audit): MaxSteps=20, MaxReflectionCount=0, MinDistance=0.005,
// StepDistanceFactor=1.0, NormalSamplingDistance=0.01, MaxDistance=100, Mode=0 (Raymarch),
// WriteDistanceTo=1 (FX1), WriteStepCountTo=2 (FX2). BOTH Write* are DEFAULT-ACTIVE and target DIFFERENT
// FX slots -> both ported. NOTE MaxReflectionCount default 0 -> (clamp(0)+1)=1 reflection pass.
//
// ZONE: runtime leaf. The PSO compile goes through the dormant setFieldSourceCompiler fn-ptr seam (same as
// SdfReflectionLinePoints), so this TU has NO platform include — main.cpp wires the real compiler.
#include "runtime/point_ops.h"

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"                 // calcDispatchCount
#include "runtime/field_graph.h"              // assembleFieldMSL, AssembledField
#include "runtime/point_graph.h"              // PointCookCtx, registerPointOp, cookParam
#include "runtime/raymarchpoints_params.h"    // RaymarchPointsParams, RaymarchPointsBinding
#include "runtime/tex_op_cache.h"             // cachedSourceComputePSO
#include "runtime/tixl_point.h"               // SwPoint (64B)

#include <cstring>

namespace sw {
namespace {

// Process-lifetime cached RaymarchPoints MSL template (read at most once; empty if the define is
// unset/unreadable -> the cook takes the pass-through copy fallback). Mirrors sdfReflectionLineTemplate().
const std::string& raymarchPointsTemplate() {
  static const std::string tmpl =
#ifdef SW_RAYMARCH_POINTS_TEMPLATE
      [] {
        std::ifstream f(SW_RAYMARCH_POINTS_TEMPLATE);
        if (!f) return std::string();
        std::ostringstream ss; ss << f.rdbuf(); return ss.str();
      }();
#else
      std::string();
#endif
  return tmpl;
}

// clamp(MaxReflectionCount, 0, 10) (RaymarchPoints.t3 ClampInt Max=10 Min=0).
int clampReflections(float maxReflectionsF) {
  int r = (int)(maxReflectionsF + 0.5f);
  if (r < 0) r = 0;
  if (r > 10) r = 10;
  return r;
}

// PointCountPerLine = MaxSteps + 1; PointCountPerLineReflections = PointCountPerLine * (clampRefl + 1).
// (.t3 backward-trace; mode-INDEPENDENT — CompareInt(PointMode<0) is unconditionally false.)
uint32_t perLineReflections(int maxSteps, int clampRefl) {
  int perLine = maxSteps + 1;
  if (perLine < 1) perLine = 1;
  return (uint32_t)perLine * (uint32_t)(clampRefl + 1);
}

// STATIC-STASH (= SdfReflectionLinePoints): cook() writes the wanted output count here; the countTransform
// hook reads it. Cook fns run single-threaded so this is selftest-safe.
static uint32_t g_raymarchPointsResultCount = 1;

uint32_t raymarchPointsCountTransform(uint32_t /*naturalCount*/) {
  return g_raymarchPointsResultCount;
}

// BYTE-IDENTICAL pass-through of the first sourceCount slots (no field -> no march). The output buffer is
// sized to sourceCount*perLineReflections; we copy the leading sourceCount points (the rest stay zeroed).
void passThroughCopy(PointCookCtx& c, const MTL::Buffer* srcBag, uint32_t sourceCount) {
  if (sourceCount == 0) return;
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::BlitCommandEncoder* blit = cmd->blitCommandEncoder();
  blit->copyFromBuffer(const_cast<MTL::Buffer*>(srcBag), 0, c.output, 0,
                       (NS::UInteger)sourceCount * sizeof(SwPoint));
  blit->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
}

void cookRaymarchPoints(PointCookCtx& c) {
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  uint32_t sourceCount = (c.inputCounts && c.inputCount > 0) ? c.inputCounts[0] : 0u;

  int maxSteps   = (int)(cookParam(c, "MaxSteps", 20.0f) + 0.5f);
  if (maxSteps < 1) maxSteps = 1;  // .t3 ClampInt(MaxSteps, Min=1, Max=100) feeds the b2 MaxSteps slot.
  if (maxSteps > 100) maxSteps = 100;
  int clampRefl  = clampReflections(cookParam(c, "MaxReflectionCount", 0.0f));
  uint32_t perLineRefl = perLineReflections(maxSteps, clampRefl);

  // Stash the wanted output count for the countTransform hook (drives next-frame buffer sizing).
  uint32_t wantCount = (sourceCount == 0) ? 1u : sourceCount * perLineRefl;
  g_raymarchPointsResultCount = wantCount;

  if (!c.output || c.count == 0 || !srcBag || sourceCount == 0) return;

  // ONE-FRAME SIZING LAG (= SdfReflectionLinePoints): the driver sized c.output to the PREVIOUS frame's
  // stashed count (1 on a fresh build) BEFORE this cook updated the static. Each source thread writes a whole
  // line of perLineRefl slots, so dispatching now — when c.count < wantCount — would write PAST the output
  // buffer (an OOB GPU write). So we SKIP the dispatch this frame; the driver reallocates to wantCount next
  // frame and the dispatch lands then. (Goldens cook twice; production re-cooks per frame.)
  if (c.count < wantCount) return;

  // b0/b2 scalars (.cs slots; defaults = .t3, carried by the resolved param spine).
  RaymarchPointsParams P{};
  P.MinDistance            = cookParam(c, "MinDistance", 0.005f);
  P.StepDistanceFactor     = cookParam(c, "StepDistanceFactor", 1.0f);
  P.NormalSamplingDistance = cookParam(c, "NormalSamplingDistance", 0.01f);
  P.MaxDistance            = cookParam(c, "MaxDistance", 100.0f);
  P.SourcePointCount       = sourceCount;
  P.MaxSteps               = maxSteps;
  P.MaxReflections         = clampRefl;
  P.PointMode              = (int32_t)(cookParam(c, "Mode", 0.0f) + 0.5f);             // .t3 default 0=Raymarch
  P.WriteDistanceTo        = (int32_t)(cookParam(c, "WriteDistanceTo", 1.0f) + 0.5f);  // .t3 default 1=FX1
  P.WriteStepCountTo       = (int32_t)(cookParam(c, "WriteStepCountTo", 2.0f) + 0.5f); // .t3 default 2=FX2
  P.PointCountPerLine            = maxSteps + 1;
  P.PointCountPerLineReflections = (int32_t)perLineRefl;

  // No wired SDF (or no template) -> faithful pass-through (a raymarch op with no field can't march).
  const std::string& tmpl = raymarchPointsTemplate();
  if (!c.inputFieldTree || tmpl.empty()) { passThroughCopy(c, srcBag, sourceCount); return; }

  AssembledField asmField = assembleFieldMSL(c.inputFieldTree, tmpl);
  if (asmField.msl.empty()) { passThroughCopy(c, srcBag, sourceCount); return; }
  MTL::ComputePipelineState* pso =
      cachedSourceComputePSO(c.dev, asmField.msl.c_str(), asmField.srcHash, "raymarch_points");
  if (!pso) { passThroughCopy(c, srcBag, sourceCount); return; }  // compiler unwired / fail -> no-march

  const size_t paramBytes =
      asmField.floatParams.empty() ? 16 : asmField.floatParams.size() * sizeof(float);
  MTL::Buffer* fieldBuf = c.dev->newBuffer(paramBytes, MTL::ResourceStorageModeShared);
  if (!fieldBuf) { passThroughCopy(c, srcBag, sourceCount); return; }
  if (!asmField.floatParams.empty())
    std::memcpy(fieldBuf->contents(), asmField.floatParams.data(),
                asmField.floatParams.size() * sizeof(float));

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBag), 0, RMP_SourcePoints);
  enc->setBuffer(c.output, 0, RMP_ResultPoints);
  enc->setBytes(&P, sizeof(P), RMP_Params);
  enc->setBuffer(fieldBuf, 0, RMP_FieldParams);
  // DISPATCH over SOURCE points (one thread writes a whole line), NOT the output count.
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(sourceCount, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  fieldBuf->release();
}

}  // namespace

void registerRaymarchPointsOp() {
  registerPointOp("RaymarchPoints", cookRaymarchPoints,
                  /*stateNew=*/nullptr, /*stateFree=*/nullptr,
                  raymarchPointsCountTransform,
                  /*countFromFirstPointsInput=*/false);
}

}  // namespace sw
