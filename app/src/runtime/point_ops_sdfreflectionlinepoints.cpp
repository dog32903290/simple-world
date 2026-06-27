// SdfReflectionLinePoints — SDF point-modify + COUNT-MULTIPLY op. Faithful port of
// external/tixl .../field/use/SdfReflectionLinePoints (.cs slots + .t3 plumbing) +
// .../Assets/shaders/points/modify/SdfReflectionLinePoints.hlsl (the raymarch-and-reflect kernel).
//
// COMBINES TWO PROVEN SEAMS:
//   1. SDF field gather (= MoveToSDF / point_ops_movepointstosdf.cpp): this op has a DIRECT "Field" input
//      port, so the cook drivers' one-hop direct-Field gather (gatherPointFieldTree) builds the upstream
//      SDF tree into c.inputFieldTree. The cook MIRRORS cookMoveToSdf: assembleFieldMSL fills the
//      sdf_reflection_line_points_template.metal hooks -> cachedSourceComputePSO (srcHash-keyed) ->
//      dispatch. (Runtime string template, NOT a precompiled metallib kernel.)
//   2. Count-multiply (= SubdivideLinePoints / point_ops_subdividelinepoints.cpp): each SOURCE point emits
//      a polyline of pointsPerLine = clamp(MaxReflectionCount,0,10) + 3 output points
//      (SdfReflectionLinePoints.hlsl:84; .t3 ClampInt(0..10)->AddInts(+3)->MultiplyInt(srcCount)). The
//      driver's countTransform hook can't see the param, so we use the established STATIC-STASH pattern:
//      cook() computes outputCount = sourceCount*pointsPerLine from c.inputCounts[0] + the param and
//      writes it to a file-static; sdfReflectionLineCountTransform() returns it (one-frame sizing lag on a
//      fresh build, exactly like SubdivideLinePoints / PairPointsForLines).
//
// DISPATCH is over SOURCE points (one thread = one source point writes a whole line), NOT the output count;
// the kernel guards `sourceIndex >= SourcePointCount`. NO field wired (or no template / compile fail) ->
// faithful pass-through copy of the input bag's first sourceCount slots (a reflection op with no SDF can't
// reflect — mirrors the MoveToSDF no-field contract).
//
// .t3 DEFAULTS (load-bearing audit): MaxReflectionCount=2, MaxSteps=40, MinDistance=0.005,
// StepDistanceFactor=1.0, NormalSamplingDistance=0.01, MaxDistance=100, WriteDistanceTo=1 (FX1),
// WriteStepCountTo=2 (FX2). BOTH Write* are DEFAULT-ACTIVE and target DIFFERENT FX slots -> both ported.
//
// ZONE: runtime leaf. The PSO compile goes through the dormant setFieldSourceCompiler fn-ptr seam (same as
// MoveToSDF / the field-into-force cooks), so this TU has NO platform include — main.cpp wires the real
// compiler. The golden (which DOES wire the platform compiler) lives in the shell tier.
#include "runtime/point_ops.h"

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"                       // calcDispatchCount
#include "runtime/field_graph.h"                    // assembleFieldMSL, AssembledField
#include "runtime/point_graph.h"                    // PointCookCtx, registerPointOp, cookParam
#include "runtime/sdfreflectionlinepoints_params.h" // SdfReflectionLineParams, SdfReflectionLineBinding
#include "runtime/tex_op_cache.h"                    // cachedSourceComputePSO
#include "runtime/tixl_point.h"                      // SwPoint (64B)

#include <cstring>

namespace sw {
namespace {

// Process-lifetime cached SdfReflectionLinePoints MSL template (read at most once; empty if the define is
// unset/unreadable -> the cook takes the pass-through copy fallback). Mirrors moveToSdfTemplate().
const std::string& sdfReflectionLineTemplate() {
  static const std::string tmpl =
#ifdef SW_SDF_REFLECTION_LINE_POINTS_TEMPLATE
      [] {
        std::ifstream f(SW_SDF_REFLECTION_LINE_POINTS_TEMPLATE);
        if (!f) return std::string();
        std::ostringstream ss; ss << f.rdbuf(); return ss.str();
      }();
#else
      std::string();
#endif
  return tmpl;
}

// pointsPerLine = clamp(MaxReflectionCount, 0, 10) + 3 (SdfReflectionLinePoints.t3 ClampInt(0..10) -> AddInts +3).
uint32_t pointsPerLineFromReflections(float maxReflections) {
  int r = (int)(maxReflections + 0.5f);
  if (r < 0) r = 0;
  if (r > 10) r = 10;
  return (uint32_t)r + 3u;
}

// STATIC-STASH (= SubdivideLinePoints): cook() writes the wanted output count here; the countTransform
// hook reads it. Cook fns run single-threaded so this is selftest-safe.
static uint32_t g_sdfReflectionLineResultCount = 1;

uint32_t sdfReflectionLineCountTransform(uint32_t /*naturalCount*/) {
  return g_sdfReflectionLineResultCount;
}

// BYTE-IDENTICAL pass-through of the first sourceCount slots (no field -> no reflection). The output buffer
// is sized to sourceCount*pointsPerLine; we copy the leading sourceCount points (the rest stay zeroed).
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

void cookSdfReflectionLinePoints(PointCookCtx& c) {
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  uint32_t sourceCount = (c.inputCounts && c.inputCount > 0) ? c.inputCounts[0] : 0u;

  float maxReflectionsF = cookParam(c, "MaxReflectionCount", 2.0f);
  uint32_t pointsPerLine = pointsPerLineFromReflections(maxReflectionsF);

  // Stash the wanted output count for the countTransform hook (drives next-frame buffer sizing).
  uint32_t wantCount = (sourceCount == 0) ? 1u : sourceCount * pointsPerLine;
  g_sdfReflectionLineResultCount = wantCount;

  if (!c.output || c.count == 0 || !srcBag || sourceCount == 0) return;

  // ONE-FRAME SIZING LAG (= SubdivideLinePoints): the driver sized c.output to the PREVIOUS frame's
  // stashed count (1 on a fresh build) BEFORE this cook updated the static. Each source thread writes a
  // whole line of pointsPerLine slots (indices i*pointsPerLine .. +pointsPerLine-1), so dispatching now —
  // when c.count < wantCount — would write PAST the output buffer (an OOB GPU write, unlike Subdivide which
  // dispatches over the output count). So we SKIP the dispatch this frame; the driver reallocates to
  // wantCount next frame and the dispatch lands then. (Goldens cook twice; production re-cooks per frame.)
  if (c.count < wantCount) return;

  // b0/b2 scalars (.cs slots; defaults = .t3, carried by the resolved param spine).
  SdfReflectionLineParams P{};
  P.MinDistance            = cookParam(c, "MinDistance", 0.005f);
  P.StepDistanceFactor     = cookParam(c, "StepDistanceFactor", 1.0f);
  P.NormalSamplingDistance = cookParam(c, "NormalSamplingDistance", 0.01f);
  P.MaxDistance            = cookParam(c, "MaxDistance", 100.0f);
  P.SourcePointCount       = sourceCount;
  P.MaxSteps               = (int32_t)(cookParam(c, "MaxSteps", 40.0f) + 0.5f);
  // MaxReflections clamped 0..10 to match the .t3 (the count formula AND the kernel loop bound agree).
  int rClamp = (int)(maxReflectionsF + 0.5f);
  if (rClamp < 0) rClamp = 0; if (rClamp > 10) rClamp = 10;
  P.MaxReflections         = rClamp;
  P.WriteDistanceTo        = (int32_t)(cookParam(c, "WriteDistanceTo", 1.0f) + 0.5f);  // .t3 default 1=FX1
  P.WriteStepCountTo       = (int32_t)(cookParam(c, "WriteStepCountTo", 2.0f) + 0.5f); // .t3 default 2=FX2

  // No wired SDF (or no template) -> faithful pass-through (a reflection op with no field can't reflect).
  const std::string& tmpl = sdfReflectionLineTemplate();
  if (!c.inputFieldTree || tmpl.empty()) { passThroughCopy(c, srcBag, sourceCount); return; }

  AssembledField asmField = assembleFieldMSL(c.inputFieldTree, tmpl);
  if (asmField.msl.empty()) { passThroughCopy(c, srcBag, sourceCount); return; }
  MTL::ComputePipelineState* pso =
      cachedSourceComputePSO(c.dev, asmField.msl.c_str(), asmField.srcHash, "sdf_reflection_line_points");
  if (!pso) { passThroughCopy(c, srcBag, sourceCount); return; }  // compiler unwired / fail -> no-reflect

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
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBag), 0, SRL_SourcePoints);
  enc->setBuffer(c.output, 0, SRL_ResultPoints);
  enc->setBytes(&P, sizeof(P), SRL_Params);
  enc->setBuffer(fieldBuf, 0, SRL_FieldParams);
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

void registerSdfReflectionLinePointsOp() {
  registerPointOp("SdfReflectionLinePoints", cookSdfReflectionLinePoints,
                  /*stateNew=*/nullptr, /*stateFree=*/nullptr,
                  sdfReflectionLineCountTransform,
                  /*countFromFirstPointsInput=*/false);
}

}  // namespace sw
