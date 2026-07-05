// buffer_ops_keeppreviouspointbuffer — KeepPreviousPointBuffer (the Buffer-rail cross-frame feedback, the
// STRUCTURED-BUFFER twin of the texture KeepPreviousFrame). It hands back the PREVIOUS frame's buffer while
// writing the CURRENT input into the other half of a persistent double-buffer pair, flipping a toggle each
// frame. TiXL authority: external/tixl/Operators/Lib/point/usse/KeepPreviousPointBuffer.cs (ported verbatim):
//
//   UpdateTexture(context) (cs:21-86):
//     keep = Keep.GetValue(context);
//     if (!InputBuffer.HasInputConnections || !keep) return;             // cs:24-25 (outputs keep prior value)
//     src = InputBuffer.GetValue(context); if (src?.Buffer == null) return;  // cs:27-29
//     // must be structured (cs:34) — on Metal every SwBuffer is a structured MTL::Buffer, so always true.
//     changed = pair absent OR SizeInBytes/StructureByteStride differ (cs:40-46) → recreate BOTH (cs:50-67);
//              our driver's ensureBufferFeedbackPair keys on (byteSize, stride) → same realloc rule.
//     dst = _toggle ? _a.Buffer : _b.Buffer;  CopyResource(src.Buffer, dst);  // cs:69-70 (blit copy)
//     BufferA.Value = _toggle ? _a : _b;   // cs:79 (the JUST-written buffer)
//     BufferB.Value = _toggle ? _b : _a;   // cs:80 (the OTHER = last frame's)
//     _toggle = !_toggle;                  // cs:85 (flip once per frame)
//
//   Inputs : InputBuffer = InputSlot<BufferWithViews> (cs:140-141); Keep = InputSlot<bool> (cs:143-144).
//   Outputs: BufferA (cs:9-10) + BufferB (cs:12-13) — the ping-pong pair.
//
// This is the EXACT mirror of point_ops_keeppreviousframe.cpp on the SwBuffer rail. The driver
// (point_graph_buffer_cook.cpp) sizes the persistent pair (ensureBufferFeedbackPair) to the input buffer's
// byteSize + hands it here via BufferCookCtx::pairA/pairB/toggle; the op blit-copies + routes + flips. The
// dual outputs ride *output (BufferA, port 0) and *secondOutput (BufferB, port 1), which the driver persists
// in feedbackBufOut[key][0/1] so a downstream Buffer input reads BufferA or BufferB by source output ordinal.
//
// ★CROSS-FRAME STATE FORK (named, FORCED by TiXL parity): the pair + toggle PERSIST between cooks ON PURPOSE
//   (that survival IS the feature). They live in Impl (feedbackBufBuf/feedbackBufToggle), sized via
//   ensureBufferFeedbackPair, released on realloc + in ~PointGraph → NO per-cook leak, NO UAF. The op does
//   NOT allocate the pair (the driver does — lifetime in one place); the op only blit-copies + routes.
//
// FORK (named): TiXL keys `changed` on SizeInBytes + StructureByteStride (cs:45-46); our
//   ensureBufferFeedbackPair keys on (byteSize, stride) — byte-identical for a real point buffer.
#include <Metal/Metal.hpp>

#include "runtime/buffer_op_registry.h"  // BufferCookCtx, BufferOp, bufferParam, bufferInjectBug
#include "runtime/graph.h"               // NodeSpec, PortSpec, Widget
#include "runtime/sw_buffer.h"           // SwBuffer

namespace sw {
namespace {

// cookKeepPreviousPointBuffer: blit InputBuffer into the toggle-selected pair buffer, route BufferA/BufferB
// to the just-written / other buffer, then flip the toggle. The driver has already sized the pair to the
// input's byteSize (ensureBufferFeedbackPair) and supplied the persistent toggle + BufferB slot.
void cookKeepPreviousPointBuffer(BufferCookCtx& c) {
  if (!c.output) return;
  const SwBuffer* in = (c.inputBuffers && !c.inputBuffers->empty()) ? c.inputBuffers->front() : nullptr;
  const bool keep = bufferParam(c.params, "Keep", 1.0f) > 0.5f;  // .t3 default TRUE (cs:143-144)

  // Guards (cs:24-29): no InputBuffer wired / Keep off / no pair sized → outputs stay default-invalid
  // (a downstream sees an empty SwBuffer, no crash). TiXL leaves the Slot values intact; sw has no prior
  // published value on this rail, so an empty output is the faithful degenerate result.
  if (!keep || !in || !in->bytes || !c.pairA || !c.pairB || !c.toggle) return;

  const uint32_t byteSize = in->elementStride * in->elementCount;
  if (byteSize == 0) return;

  const bool tog = *c.toggle;
  MTL::Buffer* writeTo = tog ? c.pairA : c.pairB;   // cs:69 dst = _toggle ? _a : _b
  MTL::Buffer* bufA = writeTo;                       // cs:79 BufferA = the just-written buffer
  MTL::Buffer* bufB = tog ? c.pairB : c.pairA;       // cs:80 BufferB = the OTHER (last frame's)

  // CopyResource(src.Buffer, dst) (cs:70) → Metal blit copyFromBuffer (whole byte range).
  {
    MTL::CommandBuffer* mc = c.queue->commandBuffer();
    MTL::BlitCommandEncoder* blit = mc->blitCommandEncoder();
    blit->copyFromBuffer(const_cast<MTL::Buffer*>(in->bytes), 0, writeTo, 0, byteSize);
    blit->endEncoding();
    mc->commit();
    mc->waitUntilCompleted();  // the getBytes golden + a same-frame downstream read need the copy done
  }

  // Route the dual outputs (same stride/count as the input; the pair buffers are byteSize-sized).
  c.output->bytes = bufA;             // BufferA (port 0)
  c.output->elementStride = in->elementStride;
  c.output->elementCount = in->elementCount;
  if (c.secondOutput) {
    c.secondOutput->bytes = bufB;     // BufferB (port 1)
    c.secondOutput->elementStride = in->elementStride;
    c.secondOutput->elementCount = in->elementCount;
  }

  // Flip the persistent toggle (cs:85) — UNLESS injectBug suppresses it (then next frame reads the wrong
  // buffer → the BufferB readback diverges → the RED tooth bites). Same tooth shape as keeppreviousframe.
  if (!bufferInjectBug()) *c.toggle = !*c.toggle;
}

}  // namespace

// Self-registration. BufferOp(..., /*feedback=*/true) registers it in bufferOpIsFeedback's set so the cook
// driver sizes the cross-frame pair + threads pairA/pairB/toggle + secondOutput.
//   Output ports FIRST (ordinal 0 = BufferA, 1 = BufferB — the driver keys feedbackBufOut by this ordinal):
//     [0] "BufferA" = Buffer output (the just-written buffer, cs:79)
//     [1] "BufferB" = Buffer output (the OTHER = previous frame, cs:80)
//   Inputs:
//     [2] "InputBuffer" = Buffer input (the structured buffer to keep)
//     [3] "Keep"        = Float Widget::Bool (.t3 default TRUE)
static const BufferOp _reg_keeppreviouspointbuffer{
    {"KeepPreviousPointBuffer", "KeepPreviousPointBuffer",
     {{"BufferA", "BufferA", "Buffer", false},
      {"BufferB", "BufferB", "Buffer", false},
      {"InputBuffer", "InputBuffer", "Buffer", true},
      {"Keep", "Keep", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool}},
     /*evaluate=*/nullptr},
    cookKeepPreviousPointBuffer, /*feedback=*/true};

}  // namespace sw
