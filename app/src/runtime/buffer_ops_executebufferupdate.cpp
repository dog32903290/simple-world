// buffer_ops_executebufferupdate — ExecuteBufferUpdate (run a Command, then forward a Buffer unchanged).
//
// TiXL authority: external/tixl/Operators/Lib/render/_dx11/fxsetup/ExecuteBufferUpdate.cs:15-28.
//   private void Update(EvaluationContext context) {
//       if (!IsEnabled.GetValue(context)) { UpdateCommand.DirtyFlag.Clear(); BufferWithViews.DirtyFlag.Clear(); return; }  // :17-22
//       UpdateCommand.GetValue(context);                          // :25  EXECUTE the command (side effect)
//       Output2.Value = BufferWithViews.GetValue(context);        // :27  then forward the buffer unchanged
//   }
//   Inputs : UpdateCommand = InputSlot<Command> (.cs:30-31); BufferWithViews = InputSlot<BufferWithViews>
//            (.cs:33-34); IsEnabled = InputSlot<bool> (.cs:36-37).
//   Output : Output2 = Slot<BufferWithViews> (.cs:6-7).
//
// The op's whole job is ORDERING: it forces the wired Command (a GPU compute pass that writes INTO the
// buffer) to run, THEN hands the SAME buffer downstream. In the cook driver, a wired Command input is
// cooked+executed BEFORE the op runs (the driver puts the executed RenderCommand on
// BufferCookCtx::inputCommand), so here the body is just the forward — the "execute" already happened.
//
// SPIKE SCOPE: there is no compute-into-buffer op in the marshalling seam, so the Command side is a
// structural forward (the buffer passes through unchanged regardless of what the Command did). IsEnabled
// defaults true; an unwired Command makes this a pure passthrough (the selftest exercises that leg). The
// UAV write-back path (a Command that actually mutates the buffer's bytes) is a FUTURE seam, flagged here.
// Named: executebufferupdate-command-is-driver-executed.
#include <vector>

#include "runtime/buffer_op_registry.h"  // BufferCookCtx, BufferOp, bufferParam
#include "runtime/graph.h"               // NodeSpec, PortSpec, Widget
#include "runtime/sw_buffer.h"           // SwBuffer

namespace sw {
namespace {

void cookExecuteBufferUpdate(BufferCookCtx& c) {
  if (!c.output) return;
  // IsEnabled (.cs:17): default true. When false, TiXL early-returns without forwarding — output stays
  // the default-invalid SwBuffer (nothing produced this cook).
  if (bufferParam(c.params, "IsEnabled", 1.0f) < 0.5f) return;
  // TiXL runs the wired Command (UpdateCommand, .cs:25) — a GPU pass that writes INTO the buffer — THEN
  // forwards the buffer (.cs:27). sw fork `executebufferupdate-command-is-buffer`: the Command currency
  // collapses to a Buffer (ComputeShaderStage dispatches during ITS cook, fork
  // computeshaderstage-dispatch-in-cook, and outputs the written buffer). The buffer cook recurses BOTH
  // Buffer inputs (UpdateCommand + BufferWithViews) BEFORE this op, so the dispatch has ALREADY run by
  // the time we forward — TiXL's ordering guarantee is preserved. UpdateCommand's buffer (the written
  // result) and BufferWithViews (the SAME underlying MTL::Buffer) are byte-identical; forward the first
  // present one (prefer UpdateCommand = the executed result).
  const SwBuffer* cmdBuf = nullptr;
  const SwBuffer* viewBuf = nullptr;
  if (c.inputBuffers && c.inputBufferPorts &&
      c.inputBuffers->size() == c.inputBufferPorts->size()) {
    for (size_t i = 0; i < c.inputBuffers->size(); ++i) {
      const std::string& p = (*c.inputBufferPorts)[i];
      if (p == "UpdateCommand") cmdBuf = (*c.inputBuffers)[i];
      else if (p == "BufferWithViews") viewBuf = (*c.inputBuffers)[i];
    }
  }
  const SwBuffer* in = (cmdBuf && cmdBuf->bytes) ? cmdBuf : viewBuf;
  if (!in || !in->bytes) return;  // no buffer to forward → output stays default-invalid
  *c.output = *in;
}

NodeSpec makeSpec() {
  NodeSpec spec;
  spec.type = "ExecuteBufferUpdate";
  spec.title = "ExecuteBufferUpdate";
  spec.category = "render/buffer";
  spec.ports = {
      {"Output2", "Buffer", "Buffer", false},               // forwarded buffer (.cs:6-7)
      // UpdateCommand: TiXL Command (.cs:30-31). Fork executebufferupdate-command-is-buffer — on Metal the
      // Command collapses to the written Buffer (ComputeShaderStage's Output). Buffer-typed so the cook
      // recurses it (runs the dispatch) BEFORE forwarding — the ordering TiXL's Command execution gives.
      {"UpdateCommand", "UpdateCommand", "Buffer", true},    // executed-then-forwarded (.cs:25,30-31)
      {"BufferWithViews", "BufferWithViews", "Buffer", true},// the buffer to forward (.cs:33-34)
      // IsEnabled (.cs:36-37): pinless bool param (default true), read via bufferParam.
      {"IsEnabled", "IsEnabled", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
  };
  spec.evaluate = nullptr;
  return spec;
}

const BufferOp _reg_executebufferupdate(makeSpec(), cookExecuteBufferUpdate);

}  // namespace
}  // namespace sw
