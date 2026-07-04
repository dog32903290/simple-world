// buffer_ops_intlisttobuffer — IntListToBuffer (host List<int> → GPU "Buffer", TIGHT int32, no pad).
//
// TiXL authority: external/tixl/Operators/Lib/numbers/int/process/IntListToBuffer.cs (verbatim below).
//   var intList = IntList.GetValue(context);                                   // :19  single List<int> wire
//   if (intList == null || intList.Count < 1) { Result.Value = null; return; } // :20-24 empty → no buffer
//   var arraySize = intList.Count;                                             // :26  NO 16-byte pad (cf IntsToBuffer)
//   _intList = new StructuredList<int>(arraySize);                             // :27-30
//   for i in 0..Count: _intList[i] = intList[i];                              // :32-35 tight copy
//   var totalSizeInBytes = arraySize * 4;                                      // :38  4 bytes / int
//   _intList.WriteToStream(data); ResourceManager.SetupStructuredBuffer(data, totalSizeInBytes, 4, ...); // :42-50
//   Result.Value = _bufferWithViews;                                          // :62
//
//   Input : IntList = InputSlot<List<int>> (.cs:71-72, a SINGLE list wire). Output: Result = Slot<Buffer> (.cs:9-10).
//
// ★THE DISTINCTION FROM IntsToBuffer (buffer_ops_intstobuffer.cpp) — LOAD-BEARING, this is why IntListToBuffer
// needs the NEW list-currency seam rather than the existing Float-MultiInput branch:
//   • IntsToBuffer.Params  = MultiInputSlot<int>  (N scalar int WIRES) → sw Float MultiInput → floatInputs;
//     count is padded UP to a multiple of 4 (16-byte const-buffer slices) with a 0-tail (.cs:24-25).
//   • IntListToBuffer.IntList = InputSlot<List<int>> (ONE list wire) → sw FloatList → inputFloatList; count
//     is EXACTLY intList.Count, stride 4, NO padding, NO 0-tail (IntListToBuffer.cs:26). A consumer relies on
//     the exact tight element count, so the byte-parity golden asserts NO pad ([7,8,9] → count 3, bytes
//     [7,8,9]) — the opposite of IntsToBuffer's [7,8,9]→count 4 [7,8,9,0].
//
// NAMED FORKS:
//   - intlisttobuffer-int-via-floatrail: sw has NO `int` currency (Cut32, LIST_SEAM_BLUEPRINT §1) — a
//     List<int> rides the FloatList host rail as integer-valued floats (the gather = cookFlatBuffer's new
//     FloatList branch → inputFloatList; the resident twin = cookResidentFloatList). The leaf casts each
//     float→int32 via (int)std::lround (a whole number the producer emitted round-trips exactly through
//     float at list-index magnitudes; no overflow, IntListToBuffer does NO arithmetic — free fold).
//   - intlisttobuffer-const-to-shared: TiXL fills a DX11 StructuredBuffer (SetupStructuredBuffer, stride 4).
//     sw fills a StorageModeShared MTL::Buffer (the Seam-1 generic "Buffer" currency), exactly as
//     FloatsToBuffer/IntsToBuffer do; the int32 BYTES are byte-identical (a tight int[] memcpy, stride 4).
//   - intlisttobuffer-empty-is-no-buffer: intList==null / Count<1 → Result.Value=null (.cs:20-24). sw's
//     unwired FloatList input → empty inputFloatList → this leaf leaves output default-invalid (no buffer).
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "runtime/buffer_op_registry.h"  // BufferCookCtx, BufferOp, bufferInjectBug
#include "runtime/graph.h"               // NodeSpec, PortSpec, Widget
#include "runtime/sw_buffer.h"           // SwBuffer

namespace sw {
namespace {

// IntListToBuffer cook: gather the List<int> payload off the FloatList rail (inputFloatList; cast float→
// int32), TIGHT-pack into a driver-allocated StorageModeShared buffer (stride 4, count = list size, NO pad).
// Empty payload → no buffer (output stays default-invalid; .cs:20-24 null/Count<1). The -bug path drops the
// last int (a real byte corruption: [7,8,9]→count 2 bytes [7,8], NOT a flipped expected value).
void cookIntListToBuffer(BufferCookCtx& c) {
  if (!c.output || !c.requestBytes) return;

  const std::vector<float>* list = c.inputFloatList;
  uint32_t count = list ? (uint32_t)list->size() : 0u;

  // -bug: drop the last int (real corruption of the produced bytes AND the element count → both the count
  // assert and the byte assert fire, unlike IntsToBuffer where the tail pad masks the count).
  if (bufferInjectBug() && count > 0) --count;

  // Empty → no buffer (.cs:20-24 → Result.Value = null). TIGHT: arraySize = intList.Count exactly (.cs:26),
  // NO ceil-to-4 pad (the IntsToBuffer distinction).
  if (count == 0) return;

  std::vector<int32_t> array(count);
  for (uint32_t i = 0; i < count; ++i)
    array[i] = (int32_t)std::lround((*list)[i]);  // FloatList rail → int32 (intlisttobuffer-int-via-floatrail)

  const uint32_t byteSize = count * (uint32_t)sizeof(int32_t);  // .cs:38 arraySize*4
  void* dst = c.requestBytes(byteSize);  // driver allocs the StorageModeShared buffer + sets output->bytes
  if (!dst) return;
  std::memcpy(dst, array.data(), byteSize);

  c.output->elementStride = (uint32_t)sizeof(int32_t);  // stride 4 (.cs:49 SetupStructuredBuffer stride arg)
  c.output->elementCount = count;                       // = intList.Count (.cs:26 — TIGHT, no pad)
  c.output->elementFormat = 0;                          // 0 = raw (int32 payload)
}

NodeSpec makeSpec() {
  NodeSpec spec;
  spec.type = "IntListToBuffer";
  spec.title = "IntListToBuffer";
  spec.category = "render/buffer";
  spec.ports = {
      {"Buffer", "Buffer", "Buffer", false},  // output (the GPU StructuredBuffer, .cs:9-10 Result slot)
      // IntList: TiXL InputSlot<List<int>> (.cs:71-72, a SINGLE list wire, NOT a MultiInput). sw has no int
      // currency, so it rides the FloatList host rail (fork intlisttobuffer-int-via-floatrail) — declared
      // "FloatList" single-input so cookFlatBuffer's new FloatList branch (point_graph_buffer_cook.cpp)
      // gathers it into inputFloatList; the leaf casts float→int32.
      {"IntList", "IntList", "FloatList", true},
  };
  spec.evaluate = nullptr;
  return spec;
}

const BufferOp _reg_intlisttobuffer(makeSpec(), cookIntListToBuffer);

}  // namespace
}  // namespace sw
