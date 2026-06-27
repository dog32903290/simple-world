// SelectPointsWithSDF — direct-Field gather LEAF (cloned VERBATIM from point_ops_movepointstosdf.cpp,
// kernel body swapped). Faithful port of external/tixl .../point/modify/SelectPointsWithSDF.cs (slots) +
// .../Assets/shaders/points/modify/SelectPointsWithField.hlsl (the per-point distance->selection kernel).
//
// Reads an input bag (c.inputs[0]), reads the wired SDF field's DISTANCE branch (GetField(float4(pos,0)).w,
// w=0, IDENTICAL to MoveToSDF's mtsGetDistance) at EACH point, maps it through Mode/Mapping/Range/Offset/
// GainAndBias into a selection scalar and writes it to FX1/FX2 (WriteTo). At the .t3 default
// DiscardNonSelected=FALSE the Scale=NaN discard branch is DEAD → count is PRESERVED (count INHERITED).
//
// THE SEAM (unchanged from MoveToSDF): this op has a DIRECT "Field" input port. The cook drivers' one-hop
// direct-Field gather (gatherPointFieldTree / gatherPointResidentFieldTree, field_graph_builder.cpp) builds
// the upstream SDF tree into c.inputFieldTree the moment the NodeSpec exposes a "Field" input — ZERO
// gather/codegen change here. NO field wired → BYTE-IDENTICAL pass-through copy (a selection with no field
// has nothing to select against; mirrors the MoveToSDF force-fallback contract).
//
// ZONE: runtime leaf. The PSO compile inside cachedSourceComputePSO goes through the dormant
// setFieldSourceCompiler fn-ptr seam (same indirection MoveToSDF uses), so this TU has NO platform include —
// main.cpp wires the real compiler. The golden (which DOES wire the platform compiler) lives in the shell
// tier: app/src/selectpointswithsdf_golden.cpp.
#include "runtime/point_ops.h"

#include <cstdint>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"             // calcDispatchCount
#include "runtime/field_graph.h"          // assembleFieldMSL, AssembledField, FieldNode
#include "runtime/selectpointswithsdf_params.h"  // SpwsdfParams, SpwsdfBinding
#include "runtime/point_graph.h"          // PointCookCtx, registerPointOp, cookParam
#include "runtime/tex_op_cache.h"         // cachedSourceComputePSO
#include "runtime/tixl_point.h"           // SwPoint (64B)

#include <cstring>

namespace sw {
namespace {

// Process-lifetime cached MSL template (read at most once; empty if the define is unset/unreadable → the
// cook takes the pass-through copy fallback). Mirrors moveToSdfTemplate().
const std::string& spwsdfTemplate() {
  static const std::string tmpl =
#ifdef SW_SELECT_POINTS_WITH_SDF_TEMPLATE
      [] {
        std::ifstream f(SW_SELECT_POINTS_WITH_SDF_TEMPLATE);
        if (!f) return std::string();
        std::ostringstream ss; ss << f.rdbuf(); return ss.str();
      }();
#else
      std::string();
#endif
  return tmpl;
}

// BYTE-IDENTICAL pass-through: blit the input bag into the output bag (no field → no selection).
void passThroughCopy(PointCookCtx& c, const MTL::Buffer* srcBag) {
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::BlitCommandEncoder* blit = cmd->blitCommandEncoder();
  blit->copyFromBuffer(const_cast<MTL::Buffer*>(srcBag), 0, c.output, 0,
                       (NS::UInteger)c.count * sizeof(SwPoint));
  blit->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
}

// SelectPointsWithSDF cook: write a per-point selection scalar from the wired SDF distance (in -> out).
void cookSelectPointsWithSdf(PointCookCtx& c) {
  if (!c.output || c.count == 0) return;
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!srcBag) return;

  // b0/b2 scalars (SelectPointsWithSDF.cs slots; defaults = SelectPointsWithSDF.t3, FloatsToBuffer/IntsToBuffer order).
  SpwsdfParams P{};
  P.Strength          = cookParam(c, "Strength", 1.0f);
  P.GainAndBiasX      = cookParam(c, "GainAndBias.x", 0.5f);
  P.GainAndBiasY      = cookParam(c, "GainAndBias.y", 0.5f);
  P.Scatter           = cookParam(c, "Scatter", 0.0f);
  P.Center            = cookParam(c, "Offset", 0.0f);   // .cs Offset routes to b0 "Center"
  P.Range             = cookParam(c, "Range", 1.0f);
  P.SelectMode        = (int32_t)(cookParam(c, "Mode", 0.0f) + 0.5f);
  P.ClampResult       = cookParam(c, "ClampNegative", 1.0f) > 0.5f ? 1 : 0;        // .t3 ClampNegative=true
  P.DiscardNonSelected = cookParam(c, "DiscardNonSelected", 0.0f) > 0.5f ? 1 : 0;  // .t3 false → discard DEAD
  P.StrengthFactor    = (int32_t)(cookParam(c, "StrengthFactor", 0.0f) + 0.5f);
  P.WriteTo           = (int32_t)(cookParam(c, "WriteTo", 1.0f) + 0.5f);            // .t3 WriteTo=1(F1)
  P.MappingMode       = (int32_t)(cookParam(c, "Mapping", 0.0f) + 0.5f);
  P.Count             = c.count;

  // No wired SDF (or no template) → faithful pass-through (a selection with no field can't select).
  const std::string& tmpl = spwsdfTemplate();
  if (!c.inputFieldTree || tmpl.empty()) { passThroughCopy(c, srcBag); return; }

  // Mirror MoveToSDF: assemble the field MSL into the template, compile/cache a srcHash-keyed PSO.
  AssembledField asmField = assembleFieldMSL(c.inputFieldTree, tmpl);
  if (asmField.msl.empty()) { passThroughCopy(c, srcBag); return; }
  MTL::ComputePipelineState* pso =
      cachedSourceComputePSO(c.dev, asmField.msl.c_str(), asmField.srcHash, "selectpointswithsdf");
  if (!pso) { passThroughCopy(c, srcBag); return; }  // compiler unwired / compile fail → faithful no-select

  // Field FloatParams buffer (rebuilt per cook; cheap). Metal needs a non-null buffer; >=16 bytes.
  const size_t paramBytes =
      asmField.floatParams.empty() ? 16 : asmField.floatParams.size() * sizeof(float);
  MTL::Buffer* fieldBuf = c.dev->newBuffer(paramBytes, MTL::ResourceStorageModeShared);
  if (!fieldBuf) { passThroughCopy(c, srcBag); return; }
  if (!asmField.floatParams.empty())
    std::memcpy(fieldBuf->contents(), asmField.floatParams.data(),
                asmField.floatParams.size() * sizeof(float));

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBag), 0, SPWSDF_SourcePoints);
  enc->setBuffer(c.output, 0, SPWSDF_ResultPoints);
  enc->setBytes(&P, sizeof(P), SPWSDF_Params);
  enc->setBuffer(fieldBuf, 0, SPWSDF_FieldParams);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  fieldBuf->release();
}

}  // namespace

void registerSelectPointsWithSdfOp() { registerPointOp("SelectPointsWithSDF", cookSelectPointsWithSdf); }

}  // namespace sw
