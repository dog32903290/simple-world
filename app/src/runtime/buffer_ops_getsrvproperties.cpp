// buffer_ops_getsrvproperties — GetSRVProperties (Buffer passthrough + element-count read-out).
//
// TiXL authority: external/tixl/Operators/Lib/render/_dx11/api/GetSRVProperties.cs:18-37
//   var srv = SRV.GetValue(context);                                                 // :20
//   if (srv == null) return;                                                          // :22-24
//   ElementCount.Value = srv.Description.Buffer.ElementCount;                          // :28
//   Inputs : SRV = InputSlot<ShaderResourceView> (.cs:36-37).
//   Outputs: ElementCount = Slot<int> (.cs:6-7) + Buffer = Slot<Buffer> (.cs:9-10). NOTE: TiXL's Update()
//            (.cs:18-34) only assigns ElementCount (:28) — it NEVER assigns Buffer.Value, so the Buffer
//            output is DEAD in TiXL (backward-trace: 129 connections read ElementCount, ZERO read Buffer).
//            The real contract is ElementCount only. sw forwards the input buffer as a harmless convenience
//            (no consumer reads it); the load-bearing parity is ElementCount == producer's elementCount.
//
// On Metal (forks below): TiXL's BufferWithViews triple (Buffer + Srv + Uav) is ONE MTL::Buffer. The "SRV"
// this op reads IS that buffer's view — so the sw input port is a plain Buffer (the SwBuffer the producer
// already filled), and srv.Description.Buffer.ElementCount (.cs:28) is exactly SwBuffer.elementCount the
// producer set (FloatsToBuffer/IntsToBuffer wrote it). This makes the op a near-noop PASSTHROUGH — the
// thin sibling of GetBufferComponents: copy the input SwBuffer view to the output (same bytes ptr, same
// stride/count); a golden reads output->elementCount straight off the forwarded view.
//
// NAMED FORKS:
//   - bufferwithviews-collapse-to-mtlbuffer (sw_buffer.h): TiXL's Buffer+Srv+Uav triple → one MTL::Buffer,
//     so the SRV input is just the SwBuffer and there is no separate view object to inspect.
//   - getsrvproperties-srv-is-buffer: TiXL declares the input as InputSlot<ShaderResourceView> (.cs:36-37);
//     on Metal the SRV is the buffer's read view, so the sw input port is dataType "Buffer" (the SwBuffer
//     the producer wrote). srv.Description.Buffer.ElementCount (.cs:28) == SwBuffer.elementCount.
//   - getsrvproperties-scalar-output-deferred: the scalar ElementCount value-rail OUTPUT (.cs:6-7) is
//     DEFERRED — like GetBufferComponents' Length/Stride/IsValid scalars (getbuffercomponents-scalar-
//     outputs-deferred). The metadata is fully observable on the forwarded SwBuffer (elementCount), read by
//     goldens off the view; no production consumer needs the int rail in this spike.
#include "runtime/buffer_op_registry.h"  // BufferCookCtx, BufferOp
#include "runtime/graph.h"               // NodeSpec, PortSpec
#include "runtime/sw_buffer.h"           // SwBuffer

namespace sw {
namespace {

void cookGetSRVProperties(BufferCookCtx& c) {
  if (!c.output) return;
  // First wired Buffer input (single-input; the gather put it at inputBuffers[0]). TiXL SRV.GetValue (:20).
  const SwBuffer* in =
      (c.inputBuffers && !c.inputBuffers->empty()) ? c.inputBuffers->front() : nullptr;
  if (!in || !in->bytes) return;  // srv == null (.cs:22-24) → output stays default-invalid (no forward)
  // Passthrough (.cs:14-15,28): forward the same buffer + its metadata. ElementCount(.cs:28) rides
  // output->elementCount (the producer's count); the scalar int output is deferred.
  *c.output = *in;
}

NodeSpec makeSpec() {
  NodeSpec spec;
  spec.type = "GetSRVProperties";
  spec.title = "GetSRVProperties";
  spec.category = "render/buffer";
  spec.ports = {
      {"Buffer", "Buffer", "Buffer", false},  // passthrough output (the .cs Buffer slot, .cs:9-10)
      {"SRV", "SRV", "Buffer", true},          // single Buffer input (the SRV-is-buffer view, .cs:36-37)
      // ElementCount scalar output DEFERRED (getsrvproperties-scalar-output-deferred): the count rides the
      // forwarded SwBuffer (elementCount), read by goldens off the view.
  };
  spec.evaluate = nullptr;
  return spec;
}

const BufferOp _reg_getsrvproperties(makeSpec(), cookGetSRVProperties);

}  // namespace
}  // namespace sw
