// buffer_ops_floatstobuffer — FloatsToBuffer (the Seam-1 KEYSTONE: host floats → GPU "Buffer").
//
// TiXL authority: external/tixl/Operators/Lib/render/_dx11/api/FloatsToBuffer.cs:27-70 (verbatim fill).
//   var totalFloatCount = floatParamCount + vec4ArrayLength * 4 * 4;          // :27
//   var totalFloatIndex = 0;
//   foreach (var aInput in matrixParams) {                                     // :38  MATRICES FIRST
//       var mat = aInput.GetValue(context);
//       foreach (var vec4 in mat) {
//           _uploadBuffer[totalFloatIndex++] = vec4.X;                         // :43
//           _uploadBuffer[totalFloatIndex++] = vec4.Y;                         // :44
//           _uploadBuffer[totalFloatIndex++] = vec4.Z;                         // :45
//           _uploadBuffer[totalFloatIndex++] = vec4.W;                         // :46
//       }
//   }
//   for (var floatIndex = 0; floatIndex < floatParamCount; floatIndex++)        // :51  FLOATS SECOND
//       _uploadBuffer[totalFloatIndex++] = floatParams[floatIndex].GetValue(context);   // :53
//   ... GetDynamicConstantBuffer(device, ..., size) ; WriteDynamicBufferData<float>(...);  // :62,:68
//
//   Inputs : Vec4Params = MultiInputSlot<Vector4[]> (.cs:78-79) — each Vector4[] is one matrix (16 floats,
//            row-major .X.Y.Z.W). Params = MultiInputSlot<float> (.cs:81-82) — the scalar floats.
//   Output : Buffer = Slot<Buffer> (.cs:8-9) — the GPU StructuredBuffer the GPU graph consumes.
//
// THE FILL ORDER IS LOAD-BEARING: ALL matrix floats (16 per matrix, in .X.Y.Z.W order) come FIRST, then
// ALL scalar floats. A consumer reading the buffer relies on this byte layout, so the byte-parity golden
// asserts it exactly (bytes[0..vec4Count*16) = the matrices, bytes[vec4Count*16..) = the floats).
//
// NAMED FORKS (cited from sw_buffer.h + here):
//   - bufferwithviews-collapse-to-mtlbuffer (sw_buffer.h): TiXL's Buffer+Srv+Uav triple → one MTL::Buffer.
//   - floatstobuffer-const-to-shared: TiXL fills a DX11 DYNAMIC CONSTANT buffer (GetDynamicConstantBuffer,
//     .cs:62). sw fills a StorageModeShared MTL::Buffer (the Seam-1 generic "Buffer" currency). The BYTES
//     are byte-identical (a tight float[] memcpy, no 16-byte CB padding here — sw treats it as a raw/
//     structured buffer with stride=4); the DX11 const-buffer 256-byte alignment is a backend detail the
//     Metal path does not need. Named, not silent.
//   - floatstobuffer-vec4-from-nodeparams (the cook driver, point_graph.cpp cookBufferNode): the matrix
//     payload is host-fed via Node::params (no Vector4[] producer op exists yet). The FILL ORDER is
//     faithful; only the SOURCE of the matrices is a stand-in until a Vector4[] producer lands.
#include <cstring>
#include <vector>

#include "runtime/buffer_op_registry.h"  // BufferCookCtx, BufferOp, bufferInjectBug
#include "runtime/graph.h"               // NodeSpec, PortSpec, Widget
#include "runtime/sw_buffer.h"           // SwBuffer

namespace sw {
namespace {

// FloatsToBuffer cook: assemble [ all matrix floats (16/matrix, .X.Y.Z.W) | all scalar floats ] into one
// float[] and memcpy it into a driver-allocated StorageModeShared buffer. stride = 4 (one float), count =
// totalFloatCount. Empty payload → no buffer (output stays the default invalid SwBuffer, TiXL .cs:28-29
// early-returns totalFloatCount==0). The -bug path drops the LAST scalar float (a real byte corruption:
// the buffer is one float short, so the byte-parity assert FAILs — NOT a flipped expected value).
void cookFloatsToBuffer(BufferCookCtx& c) {
  if (!c.output || !c.requestBytes) return;

  const std::vector<std::array<float, 16>>* mats = c.vec4Inputs;
  const std::vector<float>* floats = c.floatInputs;
  const uint32_t vec4Count = mats ? (uint32_t)mats->size() : 0u;
  uint32_t floatCount = floats ? (uint32_t)floats->size() : 0u;

  // -bug: drop the last scalar float (real corruption of the produced bytes).
  if (bufferInjectBug() && floatCount > 0) --floatCount;

  const uint32_t totalFloatCount = floatCount + vec4Count * 16u;  // .cs:27 (4*4 = 16 per matrix)
  if (totalFloatCount == 0) return;  // .cs:28-29 empty → null buffer (output stays default-invalid)

  std::vector<float> upload(totalFloatCount);
  uint32_t idx = 0;
  // Matrices FIRST (.cs:38-48): each matrix's 16 floats in stored .X.Y.Z.W order.
  for (uint32_t m = 0; m < vec4Count; ++m)
    for (int k = 0; k < 16; ++k) upload[idx++] = (*mats)[m][k];
  // Scalars SECOND (.cs:51-54).
  for (uint32_t f = 0; f < floatCount; ++f) upload[idx++] = (*floats)[f];

  const uint32_t byteSize = totalFloatCount * (uint32_t)sizeof(float);
  void* dst = c.requestBytes(byteSize);  // driver allocs the StorageModeShared buffer + sets output->bytes
  if (!dst) return;
  std::memcpy(dst, upload.data(), byteSize);

  c.output->elementStride = (uint32_t)sizeof(float);  // raw-float structured buffer (stride = 4)
  c.output->elementCount = totalFloatCount;           // = TiXL Length (totalSizeInBytes / 4)
  c.output->elementFormat = 0;                         // 0 = raw/float
}

NodeSpec makeSpec() {
  NodeSpec spec;
  spec.type = "FloatsToBuffer";
  spec.title = "FloatsToBuffer";
  spec.category = "render/buffer";
  spec.ports = {
      {"Buffer", "Buffer", "Buffer", false},  // output (the GPU StructuredBuffer)
      // Vec4Params: TiXL MultiInputSlot<Vector4[]> (.cs:78-79). No Vector4[] producer op yet, so the
      // driver host-feeds matrices via Node::params (fork floatstobuffer-vec4-from-nodeparams). The port
      // is declared so the gather + Add-menu surface it; its dataType "Vec4Params" routes the driver gather.
      {"Vec4Params", "Vec4Params", "Vec4Params", true, 0.0f, 0.0f, 0.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/true},
      // Params: TiXL MultiInputSlot<float> (.cs:81-82) — the scalar float payload (gathered via evalFloat).
      {"Params", "Params", "Float", true, 0.0f, 0.0f, 0.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/true},
  };
  spec.evaluate = nullptr;
  return spec;
}

const BufferOp _reg_floatstobuffer(makeSpec(), cookFloatsToBuffer);

}  // namespace
}  // namespace sw
