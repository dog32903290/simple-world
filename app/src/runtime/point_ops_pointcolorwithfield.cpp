// PointColorWithField — direct-Field gather LEAF (cloned VERBATIM from point_ops_movepointstosdf.cpp,
// kernel body swapped). Faithful port of external/tixl .../point/modify/PointColorWithField.cs (slots) +
// .../Assets/shaders/points/_research/ColorPointsWithField.hlsl (the per-point color-from-field kernel).
//
// Reads an input bag (c.inputs[0]), evaluates the wired SDF field's COLOR branch at EACH point's position
// (GetField(float4(pos,1)), w=1) and lerps the point Color toward it by `strength`. Count INHERITED.
//
// THE SEAM (unchanged from MoveToSDF): this op has a DIRECT "Field" input port. The cook drivers' one-hop
// direct-Field gather (gatherPointFieldTree / gatherPointResidentFieldTree, field_graph_builder.cpp) builds
// the upstream SDF tree into c.inputFieldTree the moment the NodeSpec exposes a "Field" input — ZERO
// gather/codegen change here. NO field wired → BYTE-IDENTICAL pass-through copy (a color-from-field with no
// field can't recolor; mirrors the MoveToSDF force-fallback contract: GetField=1 all-ones seed → field
// color = float3(1) but with no leaf assembled the cook short-circuits to the exact-copy fallback).
//
// PARITY (ColorPointsWithField.hlsl:60-78): isnan(Scale.x) passthrough (hlsl:62-66), strength factor
// (hlsl:69-72), lerp(Color, GetField(pos,1), strength) (hlsl:75-76) — all 1:1 in the template.
//
// ZONE: runtime leaf. The PSO compile inside cachedSourceComputePSO goes through the dormant
// setFieldSourceCompiler fn-ptr seam (same indirection MoveToSDF uses), so this TU has NO platform include —
// main.cpp wires the real compiler. The golden (which DOES wire the platform compiler) lives in the shell
// tier: app/src/pointcolorwithfield_golden.cpp.
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
#include "runtime/pointcolorwithfield_params.h"  // PcwfParams, PcwfBinding
#include "runtime/point_graph.h"          // PointCookCtx, registerPointOp, cookParam
#include "runtime/tex_op_cache.h"         // cachedSourceComputePSO
#include "runtime/tixl_point.h"           // SwPoint (64B)

#include <cstring>

namespace sw {
namespace {

// Process-lifetime cached MSL template (read at most once; empty if the define is unset/unreadable → the
// cook takes the pass-through copy fallback). Mirrors moveToSdfTemplate().
const std::string& pcwfTemplate() {
  static const std::string tmpl =
#ifdef SW_POINT_COLOR_WITH_FIELD_TEMPLATE
      [] {
        std::ifstream f(SW_POINT_COLOR_WITH_FIELD_TEMPLATE);
        if (!f) return std::string();
        std::ostringstream ss; ss << f.rdbuf(); return ss.str();
      }();
#else
      std::string();
#endif
  return tmpl;
}

// BYTE-IDENTICAL pass-through: blit the input bag into the output bag (no field → no recolor).
void passThroughCopy(PointCookCtx& c, const MTL::Buffer* srcBag) {
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::BlitCommandEncoder* blit = cmd->blitCommandEncoder();
  blit->copyFromBuffer(const_cast<MTL::Buffer*>(srcBag), 0, c.output, 0,
                       (NS::UInteger)c.count * sizeof(SwPoint));
  blit->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
}

// PointColorWithField cook: recolor the input bag from the wired SDF field's color branch (in -> out).
void cookPointColorWithField(PointCookCtx& c) {
  if (!c.output || c.count == 0) return;
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!srcBag) return;

  // b0/b2 scalars (PointColorWithField.cs slots; defaults = PointColorWithField.t3: Strength=1, StrengthFactor=0).
  PcwfParams P{};
  P.Strength       = cookParam(c, "Strength", 1.0f);
  P.Range          = 0.0f;  // b0 slot, DEAD in the kernel (no Range port; .t3 leaves it 0)
  P.Count          = c.count;
  P.StrengthFactor = (int32_t)(cookParam(c, "StrengthFactor", 0.0f) + 0.5f);

  // No wired SDF (or no template) → faithful pass-through (a color-from-field with no field can't recolor).
  const std::string& tmpl = pcwfTemplate();
  if (!c.inputFieldTree || tmpl.empty()) { passThroughCopy(c, srcBag); return; }

  // Mirror MoveToSDF: assemble the field MSL into the template, compile/cache a srcHash-keyed PSO.
  AssembledField asmField = assembleFieldMSL(c.inputFieldTree, tmpl);
  if (asmField.msl.empty()) { passThroughCopy(c, srcBag); return; }
  MTL::ComputePipelineState* pso =
      cachedSourceComputePSO(c.dev, asmField.msl.c_str(), asmField.srcHash, "pointcolorwithfield");
  if (!pso) { passThroughCopy(c, srcBag); return; }  // compiler unwired / compile fail → faithful no-recolor

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
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBag), 0, PCWF_SourcePoints);
  enc->setBuffer(c.output, 0, PCWF_ResultPoints);
  enc->setBytes(&P, sizeof(P), PCWF_Params);
  enc->setBuffer(fieldBuf, 0, PCWF_FieldParams);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  fieldBuf->release();
}

}  // namespace

void registerPointColorWithFieldOp() { registerPointOp("PointColorWithField", cookPointColorWithField); }

}  // namespace sw
