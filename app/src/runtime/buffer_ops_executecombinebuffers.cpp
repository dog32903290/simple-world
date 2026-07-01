// buffer_ops_executecombinebuffers — _ExecuteCombineBuffers (187 量產第一波: CombineBuffers.t3 replay).
//
// TiXL authority: external/tixl/Operators/Lib/point/combine/_ExecuteCombineBuffers.cs:32-123. This is a
// C# CODE-OP (no wired sub-atoms): CombineBuffers.t3 has only TWO children — this op + a ComputeShader
// whose Source is CombineBuffersAsInt.hlsl — and ALL the GPU marshalling (result-buffer allocation, the
// per-input dispatch loop, the {StartIndex,Length} const buffer) lives INSIDE this op's Update(). That is
// the shape difference from TransformPoints.t3 (which spells ComputeShaderStage / StructuredBufferWithViews
// / ExecuteBufferUpdate out as wired atoms): CombineBuffers hides its compute-stage assembly in code, so it
// cannot decompose into the generic ComputeShaderStage seam — it needs a DEDICATED replay atom (this file).
//
//   TiXL Update() (.cs):
//     var connections = InputBuffers.GetCollectedTypedInputs();          // :35  the N wired point bags
//     _computeShader = ComputeShader.GetValue(context);                  // :37  which kernel (folded → KernelName)
//     // sum counts, enforce a SINGLE shared stride (mismatched-stride inputs are dropped, .cs:59-69):
//     for each input: usedStride = first input's stride; length = Srv.ElementCount; StartIndex = running total.
//     SetupStructuredBuffer(totalLength * usedStride, usedStride, ref _resultBuffer);        // :88  ONE result
//     for each input (.cs:101-113):
//         csStage.SetShaderResources(0, 1, input.Srv);                   // t0 = this input
//         SetupPassParamBuffer(StartIndex, Length);                      // b0 = {StartIndex, Length}
//         Dispatch(Length / 256 + 1, 1, 1);                              // copy this input into the result
//     Output.Value = _resultBuffer;                                      // :115
//
//   Inputs : InputBuffers = MultiInputSlot<BufferWithViews> (.cs:169-170, guid c8a5769e) — the N bags.
//            ComputeShader = InputSlot<ComputeShader> (.cs:172-173, guid d91e52f2) — folded to KernelName.
//   Output : Output = Slot<BufferWithViews> (.cs:11-12, guid d6770718) — the combined bag.
//
// ── sw COLLAPSE (named forks, cited from the ecosystem) ────────────────────────────────────────────
// • bufferwithviews-collapse-to-mtlbuffer (sw_buffer.h): each input's Srv IS its SwBuffer's MTL::Buffer;
//   the result's Buffer/Srv/Uav triple is ONE StorageModeShared MTL::Buffer (usable as t0 and u0).
// • combinebuffers-dispatch-in-cook: TiXL runs this inside a code-op Update; sw dispatches DURING THIS
//   COOK and outputs the written result (same as computeshaderstage-dispatch-in-cook — the buffer cook
//   recurses inputs first, so every input bag is already resident when this op runs).
// • computeshader-source-folded-onto-stage (t3_import): the ComputeShader child's Source folds onto this
//   op's KernelName (CombineBuffersAsInt.hlsl → the "combinebuffers" MSL kernel). Same fold pass as the
//   ComputeShaderStage seam — generalized to also recognize this op's ComputeShader input slot.
// • combinebuffers-count-is-buffer-elementcount: TiXL reads length from Srv.Description.Buffer.ElementCount;
//   sw reads it from the input SwBuffer's elementCount (the collapsed view metadata). The result inherits
//   the FIRST input's elementStride (TiXL's usedStride single-stride enforcement).
#include <cstdint>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/buffer_op_registry.h"        // BufferCookCtx, BufferOp, bufferStrParam
#include "runtime/computeshaderstage_params.h" // CS_CB_BASE / CS_SRV_BASE / CS_UAV_BASE
#include "runtime/dispatch.h"                  // calcDispatchCount
#include "runtime/graph.h"                     // NodeSpec, PortSpec, Widget
#include "runtime/sw_buffer.h"                 // SwBuffer
#include "runtime/tex_op_cache.h"              // cachedComputePSO

namespace sw {
namespace {

// Map the folded ComputeShader.Source path to the ported MSL kernel name (mirror of ComputeShaderStage's
// kernelNameFor). CombineBuffers.t3 selects CombineBuffersAsInt.hlsl (a whole-Point copy). An already-MSL
// name (no path / no .hlsl) passes through so a direct golden can name the kernel straight.
std::string combineKernelFor(const std::string& src) {
  if (src.empty()) return src;
  if (src.find('/') == std::string::npos && src.find(".hlsl") == std::string::npos) return src;
  if (src.find("points/combine/CombineBuffersAsInt.hlsl") != std::string::npos ||
      src.find("points/combine/CombineBuffers.hlsl") != std::string::npos)
    return "combinebuffers";
  return src;  // unmapped path → PSO lookup fails loudly (no silent wrong kernel)
}

// Per-pass const buffer bytes (mirror _ExecuteCombineBuffers.IndexDataForPass: int StartIndex, int Length,
// + 8B pad to 16). sw binds via setBytes so no 32-bit alignment padding is needed — the kernel reads
// cb0[0]/cb0[1]; we pass exactly two ints.
struct PassParams { int32_t startIndex; int32_t length; };

void cookExecuteCombineBuffers(BufferCookCtx& c) {
  if (!c.output || !c.lib || !c.dev || !c.queue || !c.requestBytes) return;

  const std::string kernel =
      combineKernelFor(bufferStrParam(c.strParams, "KernelName", std::string()));
  if (kernel.empty()) return;  // no shader → nothing to dispatch (TiXL _cs==null early-return, .cs:39-43)

  // Gather the wired InputBuffers (a single MultiInput port) in wire order = TiXL GetCollectedTypedInputs().
  std::vector<const SwBuffer*> inputs;
  if (c.inputBuffers && c.inputBufferPorts &&
      c.inputBuffers->size() == c.inputBufferPorts->size()) {
    for (size_t i = 0; i < c.inputBuffers->size(); ++i) {
      if ((*c.inputBufferPorts)[i] != "InputBuffers") continue;
      const SwBuffer* b = (*c.inputBuffers)[i];
      if (b && b->bytes && b->elementCount > 0) inputs.push_back(b);
    }
  }
  if (inputs.empty()) return;  // .cs:39-43 connections.Count==0 → Output=null

  // Single-stride enforcement (.cs:59-69): usedStride = first input's stride; drop mismatched-stride inputs.
  const uint32_t usedStride = inputs.front()->elementStride;
  uint32_t totalCount = 0;
  std::vector<const SwBuffer*> kept;
  for (const SwBuffer* b : inputs) {
    if (b->elementStride != usedStride) continue;  // .cs:65-69 "Ignoring buffer not matching stride"
    kept.push_back(b);
    totalCount += b->elementCount;
  }
  if (kept.empty() || usedStride == 0 || totalCount == 0) return;

  // Allocate the ONE result buffer (.cs:88 SetupStructuredBuffer(totalLength*usedStride, usedStride)).
  const uint32_t byteSize = totalCount * usedStride;
  void* dst = c.requestBytes(byteSize);  // driver allocs the StorageModeShared result → sets output->bytes
  if (!dst || !c.output->bytes) return;
  MTL::Buffer* resultBuf = const_cast<MTL::Buffer*>(c.output->bytes);

  MTL::ComputePipelineState* pso = cachedComputePSO(c.dev, c.lib, kernel.c_str());
  if (!pso) return;

  // Per-input dispatch loop (.cs:101-113): copy each input into the result at its running StartIndex.
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(resultBuf, 0, CS_UAV_BASE + 0);  // u0: shared result (bound once, all passes)
  uint32_t startIndex = 0;
  const uint32_t tg = 256;                         // TiXL threadGroupSizeX (.cs:108)
  for (const SwBuffer* b : kept) {
    const uint32_t length = b->elementCount;
    PassParams pp{(int32_t)startIndex, (int32_t)length};
    enc->setBuffer(const_cast<MTL::Buffer*>(b->bytes), 0, CS_SRV_BASE + 0);  // t0: this input
    enc->setBytes(&pp, sizeof(pp), CS_CB_BASE + 0);                          // b0: {StartIndex, Length}
    enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(length, tg), 1, 1),
                              MTL::Size::Make(tg, 1, 1));
    startIndex += length;
  }
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  c.output->elementStride = usedStride;   // .cs:88 result stride = usedStride (first input's)
  c.output->elementCount = totalCount;    // .cs:82 sum of kept inputs' counts
  c.output->elementFormat = inputs.front()->elementFormat;
  // PSO owned by the device-global computePsoCache (released in clearTexOpCache); do NOT release here.
}

NodeSpec makeSpec() {
  NodeSpec spec;
  spec.type = "_ExecuteCombineBuffers";
  spec.title = "_ExecuteCombineBuffers";
  spec.category = "render/buffer";
  spec.ports = {
      {"Output", "Buffer", "Buffer", false},  // combined result buffer (.cs:11-12, guid d6770718)
      // InputBuffers: TiXL MultiInputSlot<BufferWithViews> (.cs:169-170) — the N point bags, in wire order.
      {"InputBuffers", "Buffer", "Buffer", true, 0.0f, 0.0f, 0.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/true},
      // KernelName: the MSL kernel to dispatch, folded from the ComputeShader child's Source at import.
      {"KernelName", "KernelName", "String", true},
  };
  spec.evaluate = nullptr;
  return spec;
}

const BufferOp _reg_executecombinebuffers(makeSpec(), cookExecuteCombineBuffers);

}  // namespace
}  // namespace sw
