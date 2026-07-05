// buffer_ops_executerawbufferupdate — ExecuteRawBufferUpdate (execute a Command subtree for side-effect,
// then forward a SEPARATE Buffer input unchanged).
//
// TiXL authority: external/tixl/Operators/Lib/flow/context/ExecuteRawBufferUpdate.cs:14-19.
//   private void Update(EvaluationContext context) {
//       UpdateCommands.GetValue(context);          // :17  EXECUTE the command subtree (side effect only)
//       Output.Value = Buffer.GetValue(context);   // :18  then forward the SEPARATE Buffer input unchanged
//   }
//   Inputs : UpdateCommands = InputSlot<Command> (.cs:21-22); Buffer = InputSlot<Buffer> (.cs:24-25).
//   Output : Output = Slot<Buffer> (.cs:6-7).
//
// The flow/context sibling of the render/_dx11/fxsetup ExecuteBufferUpdate (buffer_ops_executebufferupdate.cpp).
// The DIFFERENCE from ExecuteBufferUpdate: that op forwards the SAME buffer its Command wrote (Output2 =
// BufferWithViews); THIS op forwards a SEPARATE Buffer input while the Command runs purely for its side effect.
//
// SW FORK `executerawbufferupdate-command-is-buffer` (mirror of executebufferupdate-command-is-buffer): sw's
// Command currency for a buffer-writing pass collapses to a Buffer (ComputeShaderStage dispatches during ITS
// OWN cook, fork computeshaderstage-dispatch-in-cook, and outputs the written buffer). So UpdateCommands rides
// a Buffer-typed input here too: the buffer cook driver RECURSES both Buffer inputs BEFORE this op runs, which
// is exactly TiXL's ordering guarantee (the dispatch has already happened by the time we forward). We then
// forward the `Buffer` port (NOT the UpdateCommands port) — the .cs:18 semantics. UpdateCommands' cooked
// result is discarded here (side-effect only), matching .cs:17's bare GetValue.
//
// SPIKE SCOPE: as with ExecuteBufferUpdate, the "side effect" a Command subtree has on a buffer is realized
// by the ComputeShaderStage's own dispatch-in-cook (there is no separate render-command→buffer write path in
// the marshalling seam yet). An unwired UpdateCommands makes this a pure Buffer passthrough (the selftest
// exercises the forward).
#include "runtime/buffer_ops_executerawbufferupdate.h"  // executeRawBufferForwardWrongPortBug
#include "runtime/buffer_op_registry.h"  // BufferCookCtx, BufferOp, bufferParam
#include "runtime/graph.h"               // NodeSpec, PortSpec
#include "runtime/sw_buffer.h"           // SwBuffer

namespace sw {

// GOLDEN tooth (mirror of switchIgnoreIndexForTest): when true, forward the WRONG port — the UpdateCommands
// buffer (the side-effect input) instead of the `Buffer` input. Corrupts the REAL port-selection logic so
// the golden's -bug bites (output != the wired Buffer). OFF in production.
bool& executeRawBufferForwardWrongPortBug() { static bool v = false; return v; }

namespace {

void cookExecuteRawBufferUpdate(BufferCookCtx& c) {
  if (!c.output) return;
  // Forward the `Buffer` input (.cs:18). UpdateCommands (.cs:17) was already cooked by the driver's Buffer
  // recursion (its dispatch ran) — its result is discarded here (side-effect only). Find each port's gathered
  // SwBuffer by its port id.
  const SwBuffer* bufferPort = nullptr;
  const SwBuffer* cmdPort = nullptr;
  const SwBuffer* anyPort = nullptr;
  if (c.inputBuffers && c.inputBufferPorts &&
      c.inputBuffers->size() == c.inputBufferPorts->size()) {
    for (size_t i = 0; i < c.inputBuffers->size(); ++i) {
      const SwBuffer* b = (*c.inputBuffers)[i];
      if (b && b->bytes && !anyPort) anyPort = b;
      const std::string& p = (*c.inputBufferPorts)[i];
      if (p == "Buffer") bufferPort = b;
      else if (p == "UpdateCommands") cmdPort = b;
    }
  }
  // -bug: forward UpdateCommands (the side-effect input) instead of Buffer → the golden bites.
  const SwBuffer* pick = executeRawBufferForwardWrongPortBug() ? cmdPort : bufferPort;
  const SwBuffer* in = (pick && pick->bytes) ? pick : anyPort;
  if (!in || !in->bytes) return;  // no buffer to forward → output stays default-invalid
  *c.output = *in;
}

NodeSpec makeSpec() {
  NodeSpec spec;
  spec.type = "ExecuteRawBufferUpdate";
  spec.title = "ExecuteRawBufferUpdate";
  spec.category = "flow.context";
  spec.ports = {
      {"Output", "Buffer", "Buffer", false},                // forwarded buffer (.cs:6-7)
      // UpdateCommands: TiXL Command (.cs:21-22). Fork executerawbufferupdate-command-is-buffer — the Command
      // collapses to the written Buffer; Buffer-typed so the cook recurses it (runs the dispatch) BEFORE the
      // forward, matching TiXL's execute-then-forward ordering. Its cooked result is discarded (side-effect).
      {"UpdateCommands", "UpdateCommands", "Buffer", true},  // executed-for-side-effect (.cs:17,21-22)
      {"Buffer", "Buffer", "Buffer", true},                  // the buffer to forward (.cs:18,24-25)
  };
  spec.evaluate = nullptr;
  return spec;
}

const BufferOp _reg_executerawbufferupdate(makeSpec(), cookExecuteRawBufferUpdate);

}  // namespace
}  // namespace sw
