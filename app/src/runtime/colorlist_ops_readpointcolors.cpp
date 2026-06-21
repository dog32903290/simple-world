// ReadPointColors colorlist op (read a Points bag's .Color field -> List<Vector4>). The READBACK leaf:
// input is a GPU Points buffer, output is the ColorList currency.
// TiXL authority: external/tixl/Operators/Lib/point/helper/ReadPointColors.cs (verbatim below).
//
//   ReadPointColors.cs Update():                                                 // cs:19-73
//     var pointBuffer = PointBuffer.GetValue(context);                           // cs:23
//     if (pointBuffer?.Buffer == null || pointBuffer.Srv == null) { colors.Clear(); return; } // cs:24-28
//     var startIndex = StartIndex.GetValue(context).ClampMin(0);                 // cs:30
//     var requestedMaxCount = MaxCount.GetValue(context);                        // cs:31
//     var totalElements = pointBuffer.Srv.Description.Buffer.ElementCount;       // cs:38
//     if (startIndex >= totalElements) { colors.Clear(); return; }              // cs:39-43
//     var maxCount = requestedMaxCount > 0 ? requestedMaxCount : int.MaxValue;   // cs:45
//     var outputCount = Math.Min(totalElements - startIndex, maxCount);          // cs:46
//     ... CopyColorsFromStream: colors[i] = point[startIndex+i].Color (byte off 32)  // cs:83-96
//
//   CopyColorsFromStream (cs:83-96):                                             // the READ math
//     const int colorOffsetInPoint = 8 * 4;   // Color @ byte 32 within the Point struct  // cs:87-88
//     basePos = startIndex * stride + 32;                                        // cs:89
//     for i in [0,outputCount): colors[i] = read Vector4 at basePos + i*stride;  // cs:91-95
//
//   Ports: PointBuffer = InputSlot<BufferWithViews>  (the Points bag)            // cs:148-149
//          StartIndex  = InputSlot<int>  default 0   (ClampMin 0)                // cs:151-152, cs:30
//          MaxCount    = InputSlot<int>  default 50  (>0 ? value : int.MaxValue) // cs:154-155, cs:45
//          Async       = InputSlot<bool> default true                           // cs:157-158
//          Result      = Slot<List<Vector4>> capacity 64                         // cs:11-12
//
// ───────────────── NAMED FORKS (who / why / authority) ─────────────────
// FORK 1 — CPU-readback instead of DX11 staging copy (this leaf, colorlist batch). TiXL maps a DX11
//   staging buffer (sync) or runs an async StructuredBufferReadAccess (cs:48-72). On Metal every SwPoint
//   bag is MTL::StorageModeShared and the upstream point op committed+waited during its own cook (same as
//   point_ops_boundingboxpoints.cpp), so we read .Color straight off the bag's contents() — no staging
//   copy, no async path. The RESULT (the Vector4 list) is bit-identical; we drop only the DX11 transport
//   mechanism, a pure backend detail, not a value. Async (cs:157-158) is therefore a NO-OP here (CPU read
//   is always synchronous) — the port is omitted (a backend-transport knob with no value effect, mirrors
//   dropping the GPU atomic encoding in BoundingBoxPoints' FORK 1). StartIndex/MaxCount are PRESERVED
//   (they change the RESULT) and honored exactly per cs:30/45/46.
//
// FORK 2 — Color field @ byte offset 32. NOT a guess: SwPoint (= TiXL Point) has Color @32 (tixl_point.h
//   static_assert offsetof(SwPoint,Color)==32), the SAME offset CopyColorsFromStream reads (cs:87-88
//   "Color is at byte offset 32 within the Point struct"). We index the bag as SwPoint* and read .Color —
//   identical bytes, no offset arithmetic needed.
//
// FORK 3 — int ports ride as Float params (the value rail is float-only). StartIndex/MaxCount are
//   InputSlot<int> in TiXL; this codebase's colorlist cook resolves Float params only, so they live as
//   pinless Float params read via colorListParam and truncated to int (the established int-as-Float fork,
//   same as every int port in the point/value families). Defaults match the .cs: StartIndex 0, MaxCount 50.
//
// Output rides the ColorList currency (a List<Vector4>), faithful to ReadPointColors' Slot<List<Vector4>>
// output (cs:11-12) now that the currency exists — NOT a point-bag fork. PRODUCTION readback path: the
// flat PointGraph::cook (which actually DISPATCHES the GPU point pipeline and reads the cooked bag back).
#include <cmath>

#include <simd/simd.h>

#include <Metal/Metal.hpp>

#include "runtime/colorlist_op_registry.h"  // ColorListOp / ColorListCookCtx / colorListParam / colorListInjectBug
#include "runtime/graph.h"                   // NodeSpec, PortSpec, Widget
#include "runtime/tixl_point.h"              // SwPoint (Color @ byte 32, proven by static_assert)

namespace sw {

int runReadPointColorsSelfTest(bool injectBug);

namespace {

// ReadPointColors: read outputCount colors (.Color of each SwPoint) from the bag, honoring StartIndex /
// MaxCount clamping (cs:30/39-43/45-46). Unwired/empty bag -> empty list (cs:24-28).
void cookReadPointColors(ColorListCookCtx& c) {
  if (!c.output) return;
  c.output->clear();  // colors.Clear() is the unwired/early-return default (cs:26/42)

  const MTL::Buffer* bag = c.inputPointsBag;
  if (!bag) return;  // cs:24-28 — no point buffer -> empty list
  const uint32_t totalElements = c.inputPointsCount;

  // cs:30 — startIndex = StartIndex.ClampMin(0). cs:31/45 — maxCount = requestedMaxCount>0 ? value : INF.
  int startIndex = (int)colorListParam(c.params, "StartIndex", 0.0f);  // FORK 3 (int-as-Float)
  if (startIndex < 0) startIndex = 0;                                  // ClampMin(0)
  int requestedMaxCount = (int)colorListParam(c.params, "MaxCount", 50.0f);

  // cs:39-43 — startIndex >= totalElements -> empty list.
  if ((uint32_t)startIndex >= totalElements) return;

  // cs:45-46 — maxCount = requestedMaxCount>0 ? requestedMaxCount : int.MaxValue;
  //            outputCount = min(totalElements - startIndex, maxCount).
  uint32_t available = totalElements - (uint32_t)startIndex;
  uint32_t maxCount = requestedMaxCount > 0 ? (uint32_t)requestedMaxCount : 0xFFFFFFFFu;
  uint32_t outputCount = available < maxCount ? available : maxCount;

  // cs:83-95 — read colors[i] = point[startIndex+i].Color. FORK 1: read the shared bag's contents()
  // directly (no DX11 staging). FORK 2: SwPoint.Color is @ byte 32 (== CopyColorsFromStream's offset).
  const SwPoint* pts = (const SwPoint*)const_cast<MTL::Buffer*>(bag)->contents();
  for (uint32_t i = 0; i < outputCount; ++i)
    c.output->push_back(pts[(uint32_t)startIndex + i].Color);

  // Test-only: corrupt the REAL output on the actual cook path (drop the last color) so the golden's RED
  // case bites here, NOT by flipping the expected value. Off in production.
  if (colorListInjectBug() && !c.output->empty())
    c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static ColorListOp — independent leaf .cpp (no shared edit point).
//   Ports: "PointBuffer" = the Points bag input (the readback source);
//          "StartIndex"  = pinless Float param (int-as-Float fork), default 0;
//          "MaxCount"    = pinless Float param, default 50;
//          "out"         = the ColorList output (the colors read back).
//   Async is omitted (FORK 1: a value-less DX11-transport knob; CPU read is always synchronous).
// PortSpec field order: id,name,dataType,isInput,def,minV,maxV,widget,labels,pinless,vecArity,multiInput.
static const ColorListOp _reg_readpointcolors{
    {"ReadPointColors", "ReadPointColors",
     {{"PointBuffer", "PointBuffer", "Points", true},
      {"StartIndex", "StartIndex", "Float", true, 0.0f, 0.0f, 100000.0f, Widget::Slider, {},
       /*pinless=*/true},
      {"MaxCount", "MaxCount", "Float", true, 50.0f, 0.0f, 100000.0f, Widget::Slider, {},
       /*pinless=*/true},
      {"out", "out", "ColorList", false}},
     /*evaluate=*/nullptr},  // ColorList output cannot ride NodeSpec::evaluate (returns ONE float)
    cookReadPointColors};

}  // namespace sw
