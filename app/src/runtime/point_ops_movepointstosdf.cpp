// MoveToSDF — SDF point-MODIFY op (the SDF point-modify seam, proving op). Faithful port of
// external/tixl .../point/modify/MoveToSDF (.cs slots) + .../Assets/shaders/points/modify/
// MovePointsToSDF.hlsl (the raymarch-to-surface kernel body).
//
// Reads an input bag (c.inputs[0]), raymarches EACH point to the wired SDF field's surface, and writes a
// count-preserving bag (c.output) where Position = lerp(Position, surfacePoint, Amount). Count INHERITED.
//
// THE SEAM: this op has a DIRECT "Field" input port. The cook drivers' one-hop direct-Field gather
// (point_graph.cpp / point_graph_resident.cpp, ADDED for this seam) builds the upstream SDF tree into
// c.inputFieldTree. This op MIRRORS runFieldForce (point_ops.cpp:221-248): a wired field → assembleFieldMSL
// fills the move_points_to_sdf_template.metal hooks → cachedSourceComputePSO (srcHash-keyed) → dispatch the
// input bag → output bag, binding b0 (Amount/MinDistance/StepDistanceFactor/NormalSamplingDistance + Count +
// MaxSteps) and the field's packed FloatParams. NO field wired → BYTE-IDENTICAL pass-through copy (mirrors
// the force-fallback contract — a MoveToSDF with no SDF can't move points, exactly like TiXL's GetField=1
// (the all-ones seed) which yields a constant field whose normalize(0) gradient is NaN → no move).
//
// PARITY (MovePointsToSDF.hlsl:77-145): raymarch loop (hlsl:98-109), GetDistance/GetNormal (hlsl:54-66),
// lerp(P,pp,amount) (hlsl:131), isnan(Scale.x) passthrough (hlsl:86-90) — all 1:1 in the template. The
// WriteDistanceMode/SetOrientation/SetColor/AmountFactor extras are omitted (faithful at TiXL defaults;
// see the template header for the per-fork justification).
//
// ZONE: runtime leaf. The PSO compile inside cachedSourceComputePSO goes through the dormant
// setFieldSourceCompiler fn-ptr seam (the SAME indirection RaymarchField / the field-into-force cooks use),
// so this TU has NO platform include — main.cpp wires the real compiler. The golden (which DOES wire the
// platform compiler) lives in the shell tier: app/src/movepointstosdf_golden.cpp.
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
#include "runtime/movepointstosdf_params.h"  // MoveToSdfParams, MoveToSdfBinding
#include "runtime/point_graph.h"          // PointCookCtx, registerPointOp, cookParam
#include "runtime/tex_op_cache.h"         // cachedSourceComputePSO
#include "runtime/tixl_point.h"           // SwPoint (64B)

#include <cstring>

namespace sw {
namespace {

// Process-lifetime cached MovePointsToSDF MSL template (read at most once; empty if the define is unset/
// unreadable → the cook takes the pass-through copy fallback). Mirrors the force-template loaders.
const std::string& moveToSdfTemplate() {
  static const std::string tmpl =
#ifdef SW_MOVE_POINTS_TO_SDF_TEMPLATE
      [] {
        std::ifstream f(SW_MOVE_POINTS_TO_SDF_TEMPLATE);
        if (!f) return std::string();
        std::ostringstream ss; ss << f.rdbuf(); return ss.str();
      }();
#else
      std::string();
#endif
  return tmpl;
}

// BYTE-IDENTICAL pass-through: blit the input bag into the output bag (no field → no move). Matches the
// force-fallback contract. Uses a blit encoder (cheapest exact copy of c.count points).
void passThroughCopy(PointCookCtx& c, const MTL::Buffer* srcBag) {
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::BlitCommandEncoder* blit = cmd->blitCommandEncoder();
  blit->copyFromBuffer(const_cast<MTL::Buffer*>(srcBag), 0, c.output, 0,
                       (NS::UInteger)c.count * sizeof(SwPoint));
  blit->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
}

// MoveToSDF cook: raymarch the input bag to the wired SDF surface (input bag -> output bag).
void cookMoveToSdf(PointCookCtx& c) {
  if (!c.output || c.count == 0) return;
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!srcBag) return;

  // b0 scalars (MoveToSDF.cs slots; defaults = MoveToSDF.t3, carried by the resolved param spine).
  MoveToSdfParams P{};
  P.Amount                 = cookParam(c, "Amount", 1.0f);
  P.MinDistance            = cookParam(c, "MinDistance", 0.005f);
  P.StepDistanceFactor     = cookParam(c, "StepDistanceFactor", 0.5f);
  P.NormalSamplingDistance = cookParam(c, "NormalSamplingDistance", 0.1f);
  P.Count                  = c.count;
  P.MaxSteps               = (int32_t)(cookParam(c, "MaxSteps", 20.0f) + 0.5f);

  // No wired SDF (or no template) → faithful pass-through (a MoveToSDF with no field can't move points).
  const std::string& tmpl = moveToSdfTemplate();
  if (!c.inputFieldTree || tmpl.empty()) { passThroughCopy(c, srcBag); return; }

  // Mirror runFieldForce: assemble the field MSL into the template, compile/cache a srcHash-keyed PSO.
  AssembledField asmField = assembleFieldMSL(c.inputFieldTree, tmpl);
  if (asmField.msl.empty()) { passThroughCopy(c, srcBag); return; }
  MTL::ComputePipelineState* pso =
      cachedSourceComputePSO(c.dev, asmField.msl.c_str(), asmField.srcHash, "move_points_to_sdf");
  if (!pso) { passThroughCopy(c, srcBag); return; }  // compiler unwired / compile fail → faithful no-move

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
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBag), 0, MTS_SourcePoints);
  enc->setBuffer(c.output, 0, MTS_ResultPoints);
  enc->setBytes(&P, sizeof(P), MTS_Params);
  enc->setBuffer(fieldBuf, 0, MTS_FieldParams);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  fieldBuf->release();  // contents consumed by the GPU; not needed past the dispatch
}

}  // namespace

void registerMoveToSdfOp() { registerPointOp("MoveToSDF", cookMoveToSdf); }

}  // namespace sw
