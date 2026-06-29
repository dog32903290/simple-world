// buffer_ops_transformsconstbuffer — TransformsConstBuffer (the 640-byte camera const buffer).
//
// TiXL authority:
//   external/tixl/Operators/Lib/render/_dx11/api/TransformsConstBuffer.cs:48-70 (Update: build a
//     TransformBufferLayout from context.CameraToClipSpace / WorldToCamera / ObjectToWorld, write it
//     into a DX11 ping-pong const buffer, output Buffer + PrevBuffer).
//   external/tixl/Core/Rendering/TransformBufferLayout.cs:5-62 (the 10-matrix struct: Size = 4*4*4*10
//     = 640 bytes; 10 Matrix4x4 at FieldOffset 0/64/128/192/256/320/384/448/512/576; each derived in
//     the ctor :14-17 then ALL transposed :19-30).
//
//   TransformBufferLayout.cs ctor (:10-30), verbatim:
//     Matrix4x4.Invert(cameraToClipSpace, out var clipSpaceToCamera);   // :10
//     Matrix4x4.Invert(worldToCamera,     out var cameraToWorld);       // :11
//     Matrix4x4.Invert(objectToWorld,     out var worldToObject);       // :12
//     WorldToClipSpace = Matrix4x4.Multiply(worldToCamera, cameraToClipSpace);   // :14
//     ClipSpaceToWorld = Matrix4x4.Multiply(clipSpaceToCamera, cameraToWorld);   // :15
//     ObjectToCamera   = Matrix4x4.Multiply(objectToWorld, worldToCamera);       // :16
//     ObjectToClipSpace= Matrix4x4.Multiply(ObjectToCamera, cameraToClipSpace);  // :17
//     // then EVERY one of the 10 is Matrix4x4.Transpose(...)d :19-30 (HLSL CB row-based packing).
//
//   The 10 fields IN OFFSET ORDER (TransformBufferLayout.cs:33-61):
//     0   CameraToClipSpace    64  ClipSpaceToCamera   128 WorldToCamera     192 CameraToWorld
//     256 WorldToClipSpace     320 ClipSpaceToWorld    384 ObjectToWorld     448 WorldToObject
//     512 ObjectToCamera       576 ObjectToClipSpace
//
// ─────────────────────────────────────────────────────────────────────────────────────────────────
// ★ NAMED FORK `transformsconstbuffer-hlsl-rowmajor-bytes` (THE silent-corruption risk — read this):
//
//   WHY TiXL transposes: System.Numerics Matrix4x4 stores ROW-MAJOR (element M[r,c] at byte (r*4+c)*4)
//   and uses ROW-VECTOR transform (v·M). HLSL constant-buffer memory, however, reads each cbuffer
//   float4 as a COLUMN of the matrix. So TiXL transposes every matrix on store: the bytes it writes
//   are `transpose(M_rowmajor)`, i.e. byte slot (r*4+c) holds M[c,r].
//
//   sw's `Mat4` (field_camera.h) is ALSO row-major row-vector (m[r*4+c]), BYTE-IDENTICAL to a
//   System.Numerics Matrix4x4 BEFORE the transpose. We deliberately DO NOT route through simd::float4x4
//   (which is COLUMN-major — mixing it in here would double-confuse the convention). Instead we compute
//   each matrix as a plain row-major Mat4 (exactly mirroring the System.Numerics math), then emit its
//   TRANSPOSE byte-for-byte: out_floats[i*16 + r*4 + c] = Mi.m[c*4 + r]. That replicates
//   Matrix4x4.Transpose() exactly, so the 640 produced bytes are byte-identical to TiXL's cbuffer.
//   The byte-parity selftest (selftests_buffer_transformsconstbuffer.cpp) pins this against the same
//   formula computed in-test, so any row/col swap or wrong offset FAILs before a shader ever reads it.
//
// NAMED FORK `transformsconstbuffer-camera-from-default`: the 3 source matrices (CameraToClipSpace,
//   WorldToCamera, ObjectToWorld) come from the DEFAULT camera at the output aspect + IDENTITY
//   ObjectToWorld — the mirror of fillPointCamera's v1 fork (field_camera.h:204-215). The driver
//   (point_graph_buffer_cook.cpp) fills BufferCookCtx::cam* via a fillBufferCamera call; a wired Camera
//   op is a later seam (same default-vs-pushed black-hole as the point rail). hasCamera=false (no Camera
//   marker port) → the op emits the default-camera bytes.
//
// NAMED FORK `transformsconstbuffer-prevbuffer-deferred`: TiXL ping-pongs _cbA/_cbB and exposes
//   Buffer + PrevBuffer (TransformsConstBuffer.cs:54-65) for motion-blur consumers. We emit ONLY the
//   current `Buffer` output; PrevBuffer needs a 2nd keyed buffer + a per-node toggle (cook-core state)
//   and has a motion-blur-only consumer — out of scope here. Single output port.
//
// NAMED FORK `bufferwithviews-collapse-to-mtlbuffer` (sw_buffer.h): TiXL's DX11 const Buffer → one
//   StorageModeShared MTL::Buffer (stride=640, count=1). No DX11 256-byte CB alignment (the layout is
//   already 640, a 16-multiple).
#include <cstring>
#include <vector>

#include "runtime/buffer_op_registry.h"  // BufferCookCtx, BufferOp, bufferInjectBug
#include "runtime/field_camera.h"        // Mat4, mat4Identity/mat4Mul/mat4Inverse, defaultLayerCameraForward
#include "runtime/graph.h"               // NodeSpec, PortSpec
#include "runtime/sw_buffer.h"           // SwBuffer

namespace sw {
namespace {

// Store the TRANSPOSE of a row-major Mat4 into dst[0..15] (TiXL Matrix4x4.Transpose, byte-for-byte):
// dst[r*4+c] = src.m[c*4+r]. This IS the fork `transformsconstbuffer-hlsl-rowmajor-bytes`.
void storeTransposed(float* dst, const Mat4& src) {
  for (int r = 0; r < 4; ++r)
    for (int c = 0; c < 4; ++c) dst[r * 4 + c] = src.m[c * 4 + r];
}

// TransformsConstBuffer cook: build the 10 derived matrices from the 3 source matrices (carried in the
// BufferCookCtx camera fields), transpose each, and lay them out at offsets 0/64/.../576 into a 640-byte
// StorageModeShared buffer. stride = 640, count = 1.
void cookTransformsConstBuffer(BufferCookCtx& c) {
  if (!c.output || !c.requestBytes) return;

  // Source matrices (row-major Mat4). `transformsconstbuffer-camera-from-default`: the driver fills these
  // from the default camera + identity ObjectToWorld; if it didn't (hasCamera=false and zeroed), fall back
  // to identity so the op is still well-defined (selftest always drives them).
  Mat4 cameraToClipSpace, worldToCamera, objectToWorld;
  std::memcpy(cameraToClipSpace.m, c.camCameraToClipSpace, sizeof(float) * 16);
  std::memcpy(worldToCamera.m, c.camWorldToCamera, sizeof(float) * 16);
  std::memcpy(objectToWorld.m, c.camObjectToWorld, sizeof(float) * 16);

  // TransformBufferLayout.cs:10-12 — three inverses.
  Mat4 clipSpaceToCamera, cameraToWorld, worldToObject;
  mat4Inverse(cameraToClipSpace, clipSpaceToCamera);
  mat4Inverse(worldToCamera, cameraToWorld);
  mat4Inverse(objectToWorld, worldToObject);

  // TransformBufferLayout.cs:14-17 — four products (row-vector mat4Mul(a,b) = Matrix4x4.Multiply(a,b)).
  Mat4 worldToClipSpace = mat4Mul(worldToCamera, cameraToClipSpace);
  Mat4 clipSpaceToWorld = mat4Mul(clipSpaceToCamera, cameraToWorld);
  Mat4 objectToCamera = mat4Mul(objectToWorld, worldToCamera);
  Mat4 objectToClipSpace = mat4Mul(objectToCamera, cameraToClipSpace);

  // 10 matrices in TransformBufferLayout.cs:33-61 OFFSET ORDER, each stored TRANSPOSED.
  const Mat4* order[10] = {
      &cameraToClipSpace,  // 0
      &clipSpaceToCamera,  // 64
      &worldToCamera,      // 128
      &cameraToWorld,      // 192
      &worldToClipSpace,   // 256
      &clipSpaceToWorld,   // 320
      &objectToWorld,      // 384
      &worldToObject,      // 448
      &objectToCamera,     // 512
      &objectToClipSpace,  // 576
  };

  std::vector<float> upload(16 * 10);  // 160 floats = 640 bytes
  for (int i = 0; i < 10; ++i) storeTransposed(&upload[i * 16], *order[i]);

  // -bug: perturb ONE matrix element (real byte corruption of the produced buffer — NOT a flipped
  // expected). Bumps float[0] (CameraToClipSpace[0,0] after transpose) so the byte-parity assert FAILs.
  if (bufferInjectBug()) upload[0] += 1.0f;

  const uint32_t byteSize = 4u * 4u * 4u * 10u;  // 640 (TransformBufferLayout.cs:5 Size)
  void* dst = c.requestBytes(byteSize);
  if (!dst) return;
  std::memcpy(dst, upload.data(), byteSize);

  c.output->elementStride = byteSize;  // one 640-byte const-buffer element
  c.output->elementCount = 1;          // TiXL writes one TransformBufferLayout
  c.output->elementFormat = 0;         // raw bytes
}

NodeSpec makeSpec() {
  NodeSpec spec;
  spec.type = "TransformsConstBuffer";
  spec.title = "TransformsConstBuffer";
  spec.category = "render/buffer";
  spec.ports = {
      // Single output (PrevBuffer deferred — fork transformsconstbuffer-prevbuffer-deferred).
      {"Buffer", "Buffer", "Buffer", false},
  };
  spec.evaluate = nullptr;
  return spec;
}

const BufferOp _reg_transformsconstbuffer(makeSpec(), cookTransformsConstBuffer);

}  // namespace
}  // namespace sw
