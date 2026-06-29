// buffer_ops_intstobuffer — IntsToBuffer (host ints → GPU "Buffer", 16-byte-padded int32 slices).
//
// TiXL authority: external/tixl/Operators/Lib/numbers/int/process/IntsToBuffer.cs:19-49
//   (the CONST-BUFFER variant, GUID 2eb20a76 — 78 consumers; NOT IntsToBufferWithViews GUID c036b4f2
//   which has 0 consumers, backward-trace verdict in SEAM1_FANOUT_BUILD_PLAN.md §Backward-trace).
//   var intParams      = Params.GetCollectedTypedInputs();                          // :21
//   var intParamCount  = intParams.Count;
//   var arraySize = (intParamCount/4 + (intParamCount%4==0 ? 0 : 1)) * 4;           // :24  16-byte slices
//   var array = new int[arraySize];                                                  // :25  zero-init → tail pad 0
//   if (array.Length == 0) { ...Clear(); return; }                                   // :27-31 empty → no buffer
//   for (var intIndex = 0; intIndex < intParamCount; intIndex++)                      // :33
//       array[intIndex] = intParams[intIndex].GetValue(context);                      // :35  only first intParamCount filled
//   var size = sizeof(int) * array.Length;                                            // :41
//   GetDynamicConstantBuffer(device, ref Result.Value, size); WriteDynamicBufferData<int>(...)  // :43,48
//
//   Inputs : Params = MultiInputSlot<int> (.cs:52-53). Output: Result = Slot<Buffer> (.cs:11-12).
//   Output GUID f5531ffb == FloatsToBuffer's output GUID (IntsToBuffer.cs:11 / FloatsToBuffer.cs:8) — same
//   "Buffer" slot type, NOT a shared spec (spec dedup keys on node `type`, distinct here).
//
// THE 16-BYTE PADDING IS LOAD-BEARING (.cs:24): the int count is rounded UP to the next multiple of 4
// (16 bytes = 4 int32), and the tail ints are 0. A consumer reading the buffer relies on this exact
// element count + zero-padded tail, so the byte-parity golden asserts it ([7,8,9] → count 4, bytes
// [7,8,9,0]). This is the DX11 const-buffer 16-byte alignment requirement made faithful, not a sw quirk.
//
// NAMED FORKS:
//   - intstobuffer-const-to-shared: TiXL fills a DX11 DYNAMIC CONSTANT buffer (GetDynamicConstantBuffer,
//     .cs:43). sw fills a StorageModeShared MTL::Buffer (the Seam-1 generic "Buffer" currency), exactly as
//     the FloatsToBuffer keystone does (floatstobuffer-const-to-shared). The int32 BYTES are byte-identical
//     (a tight int[] memcpy with stride=4); only the backing allocation differs. Named, not silent.
//   - intstobuffer-int-via-floatrail: sw has no `int` currency. The int payload arrives on the Float
//     MultiInput rail (the Params port is declared "Float" multiInput exactly like FloatsToBuffer.Params,
//     so cookFlatBuffer's existing Float branch (point_graph_buffer_cook.cpp:65-68) fills floatInputs via
//     evalFloat — ZERO cook-core edit). The leaf casts each float→int32 here. The cast is correct for the
//     integral consumers (IntsToBuffer feeds counts/indices/sizes); a Const carrying a whole number
//     round-trips exactly through float (|v| < 2^24 holds for any realistic int param), and we use
//     (int)std::lround so a value the user typed as 7.0 lands as exactly 7 even past float rounding.
//   - intstobuffer-16byte-pad-faithful: the ceil-to-4 arraySize + zero-padded tail (.cs:24-25) is
//     reproduced exactly; count = padded arraySize, stride = 4, tail elements = 0.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "runtime/buffer_op_registry.h"  // BufferCookCtx, BufferOp, bufferInjectBug
#include "runtime/graph.h"               // NodeSpec, PortSpec, Widget
#include "runtime/sw_buffer.h"           // SwBuffer

namespace sw {
namespace {

// IntsToBuffer cook: gather the int payload off the Float rail (cast float→int32), pad the count up to a
// multiple of 4 (16-byte slices, .cs:24) with 0-tail, memcpy the int32[] into a driver-allocated
// StorageModeShared buffer. stride = 4 (one int32), count = padded arraySize. Empty payload → no buffer
// (output stays default-invalid; .cs:27-31 early-returns arraySize==0). The -bug path drops the last int
// BEFORE padding (a real byte corruption: [7,8,9]→[7,8] still pads to 4 but bytes become [7,8,0,0], so the
// byte-parity assert FAILs — NOT a flipped expected value).
void cookIntsToBuffer(BufferCookCtx& c) {
  if (!c.output || !c.requestBytes) return;

  const std::vector<float>* floats = c.floatInputs;
  uint32_t intCount = floats ? (uint32_t)floats->size() : 0u;

  // -bug: drop the last int (real corruption of the produced bytes; the tail pad masks the missing slot
  // with a 0 so the count survives but the payload bytes differ → byte assert fires).
  if (bufferInjectBug() && intCount > 0) --intCount;

  // 16-byte slices (.cs:24): arraySize = ceil(intCount/4)*4. Empty → no buffer (.cs:27-31).
  const uint32_t arraySize = ((intCount + 3u) / 4u) * 4u;  // == (intCount/4 + (intCount%4?1:0))*4
  if (arraySize == 0) return;

  std::vector<int32_t> array(arraySize, 0);  // zero-init → tail ints stay 0 (.cs:25)
  for (uint32_t i = 0; i < intCount; ++i)
    array[i] = (int32_t)std::lround((*floats)[i]);  // float rail → int32 (intstobuffer-int-via-floatrail)

  const uint32_t byteSize = arraySize * (uint32_t)sizeof(int32_t);  // .cs:41 sizeof(int)*Length
  void* dst = c.requestBytes(byteSize);  // driver allocs the StorageModeShared buffer + sets output->bytes
  if (!dst) return;
  std::memcpy(dst, array.data(), byteSize);

  c.output->elementStride = (uint32_t)sizeof(int32_t);  // raw-int32 structured buffer (stride = 4)
  c.output->elementCount = arraySize;                    // = padded array.Length (.cs:25)
  c.output->elementFormat = 0;                           // 0 = raw (int32 payload, same width as float)
}

NodeSpec makeSpec() {
  NodeSpec spec;
  spec.type = "IntsToBuffer";
  spec.title = "IntsToBuffer";
  spec.category = "render/buffer";
  spec.ports = {
      {"Buffer", "Buffer", "Buffer", false},  // output (the GPU StructuredBuffer, .cs:11-12 Result slot)
      // Params: TiXL MultiInputSlot<int> (.cs:52-53). sw has no int currency, so the int payload rides the
      // Float MultiInput rail (fork intstobuffer-int-via-floatrail) — declared "Float" multiInput EXACTLY
      // like FloatsToBuffer.Params so cookFlatBuffer's Float branch (point_graph_buffer_cook.cpp:65-68)
      // gathers it into floatInputs via evalFloat; the leaf casts float→int32. NO cook-core edit.
      {"Params", "Params", "Float", true, 0.0f, 0.0f, 0.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/true},
  };
  spec.evaluate = nullptr;
  return spec;
}

const BufferOp _reg_intstobuffer(makeSpec(), cookIntsToBuffer);

}  // namespace
}  // namespace sw
