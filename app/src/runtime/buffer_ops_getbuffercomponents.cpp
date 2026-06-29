// buffer_ops_getbuffercomponents — GetBufferComponents (Buffer metadata read-out + passthrough).
//
// TiXL authority: external/tixl/Operators/Lib/render/_dx11/buffer/GetBufferComponents.cs:37-86.
//   var bufferWithViews = BufferWithViews.GetValue(context);
//   if (bufferWithViews != null && bufferWithViews.Srv != null && ... && bufferWithViews.Uav != null) {
//       IsValid.Value = true;                                                   // :45
//       Buffer.Value = bufferWithViews.Buffer;                                  // :46
//       ShaderResourceView.Value = bufferWithViews.Srv;                         // :47
//       UnorderedAccessView.Value = bufferWithViews.Uav;                        // :48
//       Length.Value = ShaderResourceView.Value.Description.Buffer.ElementCount;// :60
//       Stride.Value = bufferWithViews.Buffer.Description.StructureByteStride;  // :61
//   } else { SetAsInvalid(); }                                                  // :79 (IsValid=false, Length=0)
//
//   Inputs : BufferWithViews = InputSlot<BufferWithViews> (.cs:97-98).
//   Outputs: Buffer (passthrough) + Length(int) + Stride(int) + IsValid(bool) + SRV/UAV (= same buffer).
//
// On Metal (fork bufferwithviews-collapse-to-mtlbuffer): SRV/UAV/Buffer are ONE MTL::Buffer, and
// ElementCount/StructureByteStride are the SwBuffer's elementCount/elementStride the PRODUCER already set
// (FloatsToBuffer wrote them). So this op is a near-noop PASSTHROUGH: it copies the input SwBuffer view to
// its output (same bytes ptr, same stride/count). A golden reads output->elementCount (= Length, .cs:60)
// and output->elementStride (= Stride, .cs:61) straight off the forwarded view. The scalar Length/Stride/
// IsValid value-rail OUTPUTS are DEFERRED (no production consumer in the spike); the metadata is fully
// observable on the SwBuffer. Named: getbuffercomponents-scalar-outputs-deferred.
//
// Validity (.cs:41-43,79): a present input buffer (bytes != null) → valid passthrough; an absent/empty
// input → output stays the default-invalid SwBuffer (bytes=null, count=0), the Metal restatement of
// SetAsInvalid (IsValid=false, Length=0).
#include <vector>

#include "runtime/buffer_op_registry.h"  // BufferCookCtx, BufferOp
#include "runtime/graph.h"               // NodeSpec, PortSpec
#include "runtime/sw_buffer.h"           // SwBuffer

namespace sw {
namespace {

void cookGetBufferComponents(BufferCookCtx& c) {
  if (!c.output) return;
  // First wired Buffer input (single-input; the gather put it at inputBuffers[0]).
  const SwBuffer* in =
      (c.inputBuffers && !c.inputBuffers->empty()) ? c.inputBuffers->front() : nullptr;
  if (!in || !in->bytes) return;  // absent/invalid input → output stays default-invalid (SetAsInvalid)
  // Passthrough (.cs:46-48,60-61): forward the same buffer + its metadata (SRV/UAV/Buffer are one on Metal).
  *c.output = *in;
}

NodeSpec makeSpec() {
  NodeSpec spec;
  spec.type = "GetBufferComponents";
  spec.title = "GetBufferComponents";
  spec.category = "render/buffer";
  spec.ports = {
      {"Buffer", "Buffer", "Buffer", false},   // passthrough output (the .cs Buffer slot, .cs:13)
      {"BufferWithViews", "Buffer", "Buffer", true},  // single Buffer input (.cs:97-98)
      // Length/Stride/IsValid scalar outputs DEFERRED (getbuffercomponents-scalar-outputs-deferred): the
      // metadata rides the forwarded SwBuffer (elementCount/elementStride), read by goldens off the view.
  };
  spec.evaluate = nullptr;
  return spec;
}

const BufferOp _reg_getbuffercomponents(makeSpec(), cookGetBufferComponents);

}  // namespace
}  // namespace sw
