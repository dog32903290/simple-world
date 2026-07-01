// buffer_ops_structuredbufferwithviews — StructuredBufferWithViews (allocate an EMPTY GPU buffer of
// Stride * Count bytes; the write TARGET a ComputeShaderStage UAV fills).
//
// TiXL authority: external/tixl/Operators/TypeOperators/Gfx/StructuredBufferWithViews.cs:15-65.
//   var stride = Stride.GetValue(context);
//   var sizeInBytes = stride * Count.GetValue(context);
//   if (sizeInBytes <= 0) { BufferWithViews.Value = null; return; }               // :23-27
//   ResourceManager.SetupStructuredBuffer(sizeInBytes, stride, ref ...Buffer);     // :58 (allocate)
//   if (createSrv) CreateStructuredBufferSrv(...);  if (createUav) CreateStructuredBufferUav(...);
//   Inputs : Count = InputSlot<int> (.cs:74-75); Stride = InputSlot<int> (.cs:77-78);
//            CreateSrv/CreateUav/BufferFlags (view toggles, always-true in the point pipeline).
//   Output : BufferWithViews = Slot<BufferWithViews?> (.cs:7-8).
//
// On Metal (fork bufferwithviews-collapse-to-mtlbuffer, sw_buffer.h): SRV+UAV+Buffer are ONE
// MTL::Buffer; CreateSrv/CreateUav are no-ops (the single buffer is already read/write from a
// compute shader). So this op just ALLOCATES a StorageModeShared buffer of Stride*Count bytes and
// hands it downstream as the write target. The bytes start uninitialised (TiXL SetupStructuredBuffer
// does not clear either) — a ComputeShaderStage UAV writes every element before anyone reads it.
//
// COUNT SOURCE — fork `structuredbufferwithviews-count-from-input-buffer`. In TransformPoints.t3 the
// Count wires from GetSRVProperties.ElementCount (an int rail) which sw collapses to the input Point
// buffer's elementCount (getsrvproperties-elementcount-is-buffer-view). So sw resolves Count in this
// priority: (1) a wired Buffer input's elementCount (the view-rail collapse, the production path), else
// (2) the "Count" Float param (a host-fed stand-in, kept for a direct golden). Stride from the "Stride"
// param (=64 for a Point buffer, set as an InputValue in the .t3).
#include <cstring>
#include <vector>

#include "runtime/buffer_op_registry.h"  // BufferCookCtx, BufferOp, bufferParam
#include "runtime/graph.h"               // NodeSpec, PortSpec, Widget
#include "runtime/sw_buffer.h"           // SwBuffer

namespace sw {
namespace {

void cookStructuredBufferWithViews(BufferCookCtx& c) {
  if (!c.output || !c.requestBytes) return;

  const uint32_t stride = (uint32_t)(bufferParam(c.params, "Stride", 64.0f) + 0.5f);

  // Count: a wired Buffer input's elementCount (the .t3's GetSRVProperties.ElementCount rail, collapsed
  // to the input Point buffer's view metadata); else the host-fed CountValue param (a direct-golden
  // stand-in when no view rail is wired).
  uint32_t count = 0;
  if (c.inputBuffers && !c.inputBuffers->empty()) {
    const SwBuffer* in = c.inputBuffers->front();
    if (in && in->bytes) count = in->elementCount;
  }
  if (count == 0) count = (uint32_t)(bufferParam(c.params, "CountValue", 0.0f) + 0.5f);

  const uint32_t byteSize = stride * count;
  if (byteSize == 0) return;  // .cs:23-27 sizeInBytes<=0 → null buffer (output stays default-invalid)

  void* dst = c.requestBytes(byteSize);  // driver allocs a StorageModeShared buffer + sets output->bytes
  if (!dst) return;
  std::memset(dst, 0, byteSize);         // start zeroed (a UAV overwrites every element used)

  c.output->elementStride = stride;
  c.output->elementCount = count;
  c.output->elementFormat = 0;
}

NodeSpec makeSpec() {
  NodeSpec spec;
  spec.type = "StructuredBufferWithViews";
  spec.title = "StructuredBufferWithViews";
  spec.category = "render/buffer";
  spec.ports = {
      {"BufferWithViews", "Buffer", "Buffer", false},  // allocated output (.cs:7-8)
      {"Count", "Count", "Buffer", true},              // Count int rail = a wired Buffer input's elementCount (.cs:74-75)
      // Stride (.cs:77-78) + CountValue host stand-in (direct golden, no view rail): pinless Float params.
      {"Stride", "Stride", "Float", true, 64.0f, 0.0f, 4096.0f, Widget::Slider, {}, true},
      {"CountValue", "CountValue", "Float", true, 0.0f, 0.0f, 1048576.0f, Widget::Slider, {}, true},
  };
  spec.evaluate = nullptr;
  return spec;
}

const BufferOp _reg_structuredbufferwithviews(makeSpec(), cookStructuredBufferWithViews);

}  // namespace
}  // namespace sw
