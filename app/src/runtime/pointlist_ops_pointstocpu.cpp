// PointsToCPU pointlist op (read a GPU Points bag → host List<Point>). The DOWNLOAD readback leaf and the
// exact mirror of ListToBuffer's UPLOAD bridge: ListToBuffer memcpys host SwPoints INTO a shared bag's
// contents(); PointsToCPU reads whole SwPoints OUT OF a shared bag's contents(). Where ReadPointColors
// (colorlist rail) pulls only the .Color field (vec4) per point, PointsToCPU pulls the FULL 64-byte SwPoint
// per point — so its output rides the PointList currency (host std::vector<SwPoint> = TiXL StructuredList<Point>).
//
// TiXL authority: external/tixl/Operators/Lib/point/helper/PointsToCPU.cs (the clamp math, cs:106-132):
//
//   PointsToCPU.cs Update() — the SYNCHRONOUS (non-async) readback path (cs:55-137):
//     CopyResource(pointBuffer.Buffer → _bufferWithViewsCpuAccess.Buffer)   // DX11 staging copy (cs:98)
//     MapSubresource(_bufferWithViewsCpuAccess.Buffer, Read, out sourceStream) // map for CPU read (cs:102)
//     var startIndex = StartIndex.GetValue(context).ClampMin(0);            // cs:106
//     var requestedMaxCount = MaxCount.GetValue(context);                   // cs:107
//     var elementCount = SizeInBytes / StructureByteStride;                 // cs:109-110 (= the point count)
//     if (startIndex >= elementCount) Output.Value = new StructuredList<Point>(0); // cs:112-115 → EMPTY
//     else {
//       var maxCount = requestedMaxCount > 0 ? requestedMaxCount : int.MaxValue;   // cs:119-121
//       var outputCount = Math.Min(elementCount - startIndex, maxCount);           // cs:122
//       sourceStream.Position = (long)startIndex * StructureByteStride;            // cs:124 → byte offset
//       var points = outputCount > 0 ? sourceStream.ReadRange<Point>(outputCount)  // cs:126-128
//                                    : Array.Empty<Point>();
//       Output.Value = new StructuredList<Point>(points);                          // cs:131
//     }
//
//   Ports: PointBuffer        = InputSlot<BufferWithViews> (the Points bag)        // cs:171-172
//          TriggerUpdate      = InputSlot<bool>  (async trigger knob)              // cs:174-175
//          UpdateContinuously = InputSlot<bool>  (async cadence knob)              // cs:177-178
//          StartIndex         = InputSlot<int>   default 0  (ClampMin 0)           // cs:180-181, cs:106
//          MaxCount           = InputSlot<int>   default 0  (>0 ? value : INF)     // cs:183-184, cs:119
//          Async              = InputSlot<bool>  default false                     // cs:186-187
//          Output             = Slot<StructuredList>                               // cs:11-12
//
// ───────────────── NAMED FORKS (who / why / authority) ─────────────────
// FORK 1 — shared-storage direct contents() read replaces the DX11 staging buffer + CopyResource +
//   MapSubresource (cs:77-135). On Metal every SwPoint bag is MTL::StorageModeShared and the upstream
//   point op committed+waited during its own cook (point_graph cookNode dispatches the GPU point pipeline
//   then reads the cooked bag back), so we read the bag's contents() straight as SwPoint* — no staging
//   copy, no map/unmap, no second CPU-access buffer. The RESULT (the SwPoint range) is BIT-IDENTICAL;
//   we drop only the DX11 transport mechanism, a pure backend detail, not a value. This is the EXACT
//   download twin of ListToBuffer's listtobuffer-host-memcpy fork (which memcpys host SwPoints INTO a
//   shared bag with no DX11 DataStream) and the same posture as ReadPointColors' FORK 1.
//
// FORK 2 — Async / UpdateContinuously / TriggerUpdate ports omitted (cs:21-53, cs:146-161, cs:174-178).
//   These knobs drive TiXL's StructuredBufferReadAccess async pipeline + dirty-flag cadence (cs:39-52):
//   a value-less wrapper over the SAME synchronous read whose only effect is WHEN the read happens, not
//   WHAT it returns. On a synchronous shared read the bag is always immediately readable, so the async
//   path collapses to the synchronous path (the SAME StartIndex/MaxCount clamp + ReadRange). The three
//   ports therefore have no value effect → omitted (same omission as ReadPointColors' Async port, and the
//   GPU-atomic-encoding drop in BoundingBoxPoints' FORK 1).
//
// FORK 3 — int ports ride as Float params (the value rail is float-only). StartIndex/MaxCount are
//   InputSlot<int> in TiXL; this codebase's pointlist cook resolves Float params only, so they live as
//   pinless Float params read via pointListParam and truncated to int (the established int-as-Float fork,
//   same as every int port in the point/value families). Default StartIndex 0; MaxCount default is 0,
//   which TiXL reads as "all remaining" (cs:119: requestedMaxCount>0 ? value : int.MaxValue) — preserved.
//
// FORK 4 — output rides the existing PointList host currency (std::vector<SwPoint>), NOT a fresh
//   StructuredList<Point> Slot type. SwPoint is the byte-identical host twin of TiXL Point (tixl_point.h:
//   same 64B stride + offsets), and the PointList rail already carries it between _cpu point ops, so the
//   readback lands on the existing currency (host-list shape parity, not a new type — the same reuse the
//   pointlist family adopted in cpupoint-reuses-swpoint, pointlist_op_registry.h).
#include <cstdint>

#include <Metal/Metal.hpp>

#include "runtime/graph.h"                  // NodeSpec, PortSpec, Widget
#include "runtime/pointlist_op_registry.h"  // PointListOp / PointListCookCtx / pointListParam / pointListInjectBug
#include "runtime/tixl_point.h"             // SwPoint (64B stride, byte-identical to TiXL Point)

namespace sw {

int runPointsToCpuSelfTest(bool injectBug);

namespace {

// PointsToCPU: read outputCount whole SwPoints from the upstream Points bag, honoring StartIndex / MaxCount
// clamping (cs:106/112-115/119-122). Unwired/empty bag → empty list (cs:34-37 / startIndex>=count cs:112).
void cookPointsToCpu(PointListCookCtx& c) {
  if (!c.output) return;
  c.output->clear();  // empty list is the unwired / startIndex>=count default (cs:36 / cs:114)

  const MTL::Buffer* bag = c.inputPointsBag;
  if (!bag) return;  // cs:34-37 — no point buffer → empty list
  const uint32_t totalElements = c.inputPointsCount;

  // cs:106 — startIndex = StartIndex.ClampMin(0). cs:107/119 — maxCount = requestedMaxCount>0 ? value : INF.
  int startIndex = (int)pointListParam(c.params, "StartIndex", 0.0f);  // FORK 3 (int-as-Float)
  if (startIndex < 0) startIndex = 0;                                  // ClampMin(0)
  int requestedMaxCount = (int)pointListParam(c.params, "MaxCount", 0.0f);

  // cs:112-115 — startIndex >= elementCount → empty list.
  if ((uint32_t)startIndex >= totalElements) return;

  // cs:119-122 — maxCount = requestedMaxCount>0 ? requestedMaxCount : int.MaxValue;
  //              outputCount = min(elementCount - startIndex, maxCount).
  uint32_t available = totalElements - (uint32_t)startIndex;
  uint32_t maxCount = requestedMaxCount > 0 ? (uint32_t)requestedMaxCount : 0xFFFFFFFFu;
  uint32_t outputCount = available < maxCount ? available : maxCount;

  // cs:124-131 — sourceStream.Position = startIndex*stride; points = ReadRange<Point>(outputCount).
  // FORK 1: read the shared bag's contents() directly as SwPoint* (no DX11 staging copy / map). The
  // startIndex*stride byte offset is exactly indexing pts[startIndex+i] (stride == sizeof(SwPoint) == 64).
  const SwPoint* pts = (const SwPoint*)const_cast<MTL::Buffer*>(bag)->contents();
  for (uint32_t i = 0; i < outputCount; ++i)
    c.output->push_back(pts[(uint32_t)startIndex + i]);

  // Test-only: corrupt the REAL output on the actual cook path (drop the last point) so the golden's RED
  // case bites here, NOT by flipping the expected value. Off in production.
  if (pointListInjectBug() && !c.output->empty())
    c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static PointListOp — independent leaf .cpp (no shared edit point).
//   Ports: "PointBuffer" = the Points bag input (the readback source, the GPU→host crossing);
//          "StartIndex"  = pinless Float param (int-as-Float fork), default 0;
//          "MaxCount"    = pinless Float param, default 0 (0 = read all remaining, cs:119);
//          "Output"      = the PointList output (the host SwPoints read back).
//   Async / UpdateContinuously / TriggerUpdate are omitted (FORK 2: value-less async-cadence knobs;
//   a synchronous shared read is always immediately readable).
// PortSpec field order: id,name,dataType,isInput,def,minV,maxV,widget,labels,pinless,vecArity,multiInput.
static const PointListOp _reg_pointstocpu{
    {"PointsToCPU", "PointsToCPU",
     {{"PointBuffer", "PointBuffer", "Points", true},
      {"StartIndex", "StartIndex", "Float", true, 0.0f, 0.0f, 100000.0f, Widget::Slider, {},
       /*pinless=*/true},
      {"MaxCount", "MaxCount", "Float", true, 0.0f, 0.0f, 100000.0f, Widget::Slider, {},
       /*pinless=*/true},
      {"Output", "Output", "PointList", false}},
     /*evaluate=*/nullptr},  // PointList output cannot ride NodeSpec::evaluate (returns ONE float)
    cookPointsToCpu};

}  // namespace sw
