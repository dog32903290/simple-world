// buffer_ops_computeshaderstage — the GENERIC compute-stage atom (compute-stage keystone). Binds
// arbitrary wired buffers (constant buffers / SRVs / UAVs) to a named MSL kernel and DISPATCHES it,
// writing into the UAV buffer. This is the sw port of TiXL's ComputeShaderStage — the op the 187
// GPU-compute compounds route through — so it is DELIBERATELY generic (not TransformPoints-specific):
// the SRV/CB/UAV binding is driven by which buffers are WIRED, in wire order, exactly as TiXL's
// ComputeShaderStage.Update() sets t#/b#/u# from its MultiInputSlots then Dispatches.
//
// TiXL authority: external/tixl/Operators/TypeOperators/Gfx/ComputeShaderStage.cs:22-107.
//   _cs = ComputeShader.GetValue(context);                              // :28  which kernel
//   Int3 dispatchCount = Dispatch.GetValue(context);                     // :30
//   ShaderResources.GetValues(ref _shaderResourceViews, context);       // :37  SRVs (t0..)
//   Uavs.GetValues(ref _uavs, context);                                 // :39  UAVs (u0..)
//   GetValuesWithAdditionalSlot(ref _constantBuffers, ConstantBuffers…);// :36  const buffers (b0..)
//   csStage.Set(_cs); csStage.SetConstantBuffers/SetShaderResources/SetUnorderedAccessViews(…);// :58-93
//   deviceContext.Dispatch(dispatchCount.X, .Y, .Z);                     // :106
//   Output : Command (.cs:8-9) — a deferred GPU pass. ExecuteBufferUpdate.cs:25 pulls it (runs the
//            dispatch), then forwards the StructuredBufferWithViews buffer downstream.
//
// ── sw COLLAPSE (named forks) ────────────────────────────────────────────────────────────────────
// • bufferwithviews-collapse-to-mtlbuffer (sw_buffer.h): SRV/UAV/Buffer are ONE MTL::Buffer, so an SRV
//   input and a UAV input are each just a SwBuffer; binding is enc->setBuffer at the t#/u# index.
// • computeshaderstage-dispatch-in-cook: TiXL's Output is a deferred Command that ExecuteBufferUpdate
//   later runs. sw's Buffer cook has no Command currency; instead this op DISPATCHES DURING ITS COOK
//   (the buffer cook already runs in dependency order — cookResidentBuffer recurses inputs first) and
//   OUTPUTS the written UAV buffer. ExecuteBufferUpdate then forwards that same buffer — TiXL's
//   ordering guarantee is preserved (the dispatch happens before the buffer flows on). Named, not silent.
// • computeshader-source-folded-onto-stage (t3_import): TiXL's separate ComputeShader child carries the
//   HLSL Source; sw has no CS-handle currency, so the importer folds that Source onto this op's
//   KernelName string param, translating the HLSL asset path to the ported MSL kernel name.
// • computestage-dispatch-from-srv-elementcount: the .hlsl reads its thread bound via
//   SourcePoints.GetDimensions (TransformPoints.hlsl:40); sw passes the SRV's elementCount to the
//   kernel (CS_CB_BASE+3) and dispatches ceil(N/64) groups — equivalent to CalcDispatchCount's N/64+1.
//
// PORT GROUPING: the cook reads inputBufferPorts (parallel to inputBuffers) to tell CB vs SRV vs UAV
// wired buffers apart — the flat inputBuffers vector alone loses which port each arrived on. Generic:
// N ConstantBuffers → b0..b(N-1), M ShaderResources → t0..t(M-1), K Uavs → u0..u(K-1).
#include <cstring>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/buffer_op_registry.h"       // BufferCookCtx, BufferOp, bufferStrParam
#include "runtime/computeshaderstage_params.h"// CS_CB_BASE / CS_SRV_BASE / CS_UAV_BASE / CS_MAX_*
#include "runtime/dispatch.h"                 // calcDispatchCount
#include "runtime/graph.h"                    // NodeSpec, PortSpec, Widget
#include "runtime/sw_buffer.h"                // SwBuffer
#include "runtime/tex_op_cache.h"             // cachedComputePSO

namespace sw {
namespace {

// Map a TiXL HLSL asset path (ComputeShader.Source) to the ported MSL kernel name. The importer folds
// the Source onto KernelName; a direct golden can pass the MSL name straight. Extend this table as more
// compute compounds port (one row per kernel — data-driven, ARCHITECTURE rule 7).
std::string kernelNameFor(const std::string& src) {
  if (src.empty()) return src;
  // Already an MSL kernel name (no path separator / .hlsl) → pass through.
  if (src.find('/') == std::string::npos && src.find(".hlsl") == std::string::npos) return src;
  if (src.find("points/modify/TransformPoints.hlsl") != std::string::npos)
    return "computeshaderstage_transformpoints";
  return src;  // unmapped path → let the PSO lookup fail loudly (no silent wrong kernel)
}

void cookComputeShaderStage(BufferCookCtx& c) {
  if (!c.output || !c.lib || !c.dev || !c.queue) return;

  const std::string kernel = kernelNameFor(bufferStrParam(c.strParams, "KernelName", std::string()));
  if (kernel.empty()) return;  // no shader → nothing to dispatch (TiXL _cs==null early-return, .cs:42)

  // Split the wired buffers into CB / SRV / UAV by their arrival port (inputBufferPorts, parallel to
  // inputBuffers). Wire order within a port group is preserved = TiXL's MultiInput collection order.
  std::vector<const SwBuffer*> cbs, srvs, uavs;
  if (c.inputBuffers && c.inputBufferPorts &&
      c.inputBuffers->size() == c.inputBufferPorts->size()) {
    for (size_t i = 0; i < c.inputBuffers->size(); ++i) {
      const std::string& p = (*c.inputBufferPorts)[i];
      const SwBuffer* b = (*c.inputBuffers)[i];
      if (!b || !b->bytes) continue;
      if (p == "ConstantBuffers") cbs.push_back(b);
      else if (p == "ShaderResources") srvs.push_back(b);
      else if (p == "Uavs") uavs.push_back(b);
    }
  }
  if (uavs.empty() || srvs.empty()) return;  // .cs:42 (_uavs.Length==0) → no dispatch; also need an SRV to size

  MTL::ComputePipelineState* pso = cachedComputePSO(c.dev, c.lib, kernel.c_str());
  if (!pso) return;

  // Dispatch bound = the FIRST SRV's element count (the input Point buffer's N). = GetDimensions(numStructs).
  const uint32_t numStructs = srvs.front()->elementCount;
  if (numStructs == 0) { *c.output = *uavs.front(); return; }  // nothing to do; still forward the (empty) UAV

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  for (size_t i = 0; i < cbs.size() && i < (size_t)CS_MAX_CB; ++i)
    enc->setBuffer(const_cast<MTL::Buffer*>(cbs[i]->bytes), 0, CS_CB_BASE + (int)i);
  for (size_t i = 0; i < srvs.size() && i < (size_t)CS_MAX_SRV; ++i)
    enc->setBuffer(const_cast<MTL::Buffer*>(srvs[i]->bytes), 0, CS_SRV_BASE + (int)i);
  for (size_t i = 0; i < uavs.size() && i < (size_t)CS_MAX_UAV; ++i)
    enc->setBuffer(const_cast<MTL::Buffer*>(uavs[i]->bytes), 0, CS_UAV_BASE + (int)i);
  enc->setBytes(&numStructs, sizeof(numStructs), CS_CB_BASE + 3);  // dispatch bound (see fork)
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(numStructs, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  // Output the WRITTEN UAV buffer (the StructuredBufferWithViews buffer, now filled). ExecuteBufferUpdate
  // forwards this same buffer downstream (fork computeshaderstage-dispatch-in-cook).
  *c.output = *uavs.front();
  // PSO owned by the device-global computePsoCache (released in clearTexOpCache); do NOT release here.
}

NodeSpec makeSpec() {
  NodeSpec spec;
  spec.type = "ComputeShaderStage";
  spec.title = "ComputeShaderStage";
  spec.category = "render/buffer";
  spec.ports = {
      {"Output", "Buffer", "Buffer", false},                 // the written UAV buffer (.cs:8-9 Command→buffer)
      // ConstantBuffers / ShaderResources / Uavs: MultiInput Buffer ports (.cs ConstantBuffers/
      // ShaderResources/Uavs MultiInputSlots). Bound at b#/t#/u# in wire order.
      {"ConstantBuffers", "Buffer", "Buffer", true, 0.0f, 0.0f, 0.0f, Widget::Slider, {}, false, 1, true},
      {"ShaderResources", "Buffer", "Buffer", true, 0.0f, 0.0f, 0.0f, Widget::Slider, {}, false, 1, true},
      {"Uavs", "Buffer", "Buffer", true, 0.0f, 0.0f, 0.0f, Widget::Slider, {}, false, 1, true},
      // KernelName: the MSL kernel to dispatch (folded from ComputeShader.Source at import). String port.
      {"KernelName", "KernelName", "String", true},
  };
  spec.evaluate = nullptr;
  return spec;
}

const BufferOp _reg_computeshaderstage(makeSpec(), cookComputeShaderStage);

}  // namespace
}  // namespace sw
