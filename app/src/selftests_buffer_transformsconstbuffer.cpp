// selftests_buffer_transformsconstbuffer — the TransformsConstBuffer byte-parity GATE
// (--selftest-transformsconstbuffer + -bug). This is the de-risk for the silent-corruption transpose:
// it pins the 640 produced bytes against the TiXL TransformBufferLayout formula recomputed IN-TEST, so a
// wrong transpose / wrong offset / wrong multiply-order FAILs here, BEFORE any shader reads the cbuffer.
//
// TiXL authority: external/tixl/Core/Rendering/TransformBufferLayout.cs:5-62 (the 640-byte layout, the
// 10 matrices, their derivations :14-17, the per-matrix transpose :19-30) +
// external/tixl/Operators/Lib/render/_dx11/api/TransformsConstBuffer.cs:58-60 (source = CameraToClipSpace
// / WorldToCamera / ObjectToWorld).
//
// Under test: a lone TransformsConstBuffer node (id 1), no inputs — the cook driver
// (point_graph_buffer_cook.cpp fillBufferCamera) fills the 3 source matrices from the DEFAULT camera at
// the cook aspect (PointGraph built 64x64 → aspect 1.0, deterministic) + identity ObjectToWorld (fork
// transformsconstbuffer-camera-from-default). We recompute the SAME 10 transposed matrices here from
// field_camera primitives and assert the produced 640 bytes are float-equal element-by-element (tight
// epsilon — same code path, so this is effectively exact; epsilon absorbs only fma reassociation).
//
// -bug (bufferInjectBug): the leaf perturbs ONE matrix element (upload[0] += 1) → the first float of the
// produced buffer differs from expected → FAIL on the REAL cook path (a real corruption, not a flipped
// expected). run_all --bite scans the tooth.
//
// Self-registers its --selftest row via REGISTER_SELFTESTS (orderBase 623, above the other Seam-1 fan-out
// teeth) in its OWN file, so it never collides with the keystone selftests_buffer.cpp.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/buffer_op_registry.h"  // bufferInjectBug
#include "runtime/eval_context.h"        // EvaluationContext
#include "runtime/field_camera.h"        // Mat4, mat4Identity/mat4Mul/mat4Inverse, defaultLayerCameraForward
#include "runtime/graph.h"               // Graph/Node
#include "runtime/point_graph.h"         // PointGraph, debugCookedSwBuffer
#include "runtime/sw_buffer.h"           // SwBuffer
#include "runtime/selftest_registry.h"   // REGISTER_SELFTESTS (this leaf self-registers its row)

namespace sw {
namespace {

// Store transpose(src) into dst[0..15] (mirror the leaf's storeTransposed — TiXL Matrix4x4.Transpose).
void storeT(float* dst, const Mat4& src) {
  for (int r = 0; r < 4; ++r)
    for (int c = 0; c < 4; ++c) dst[r * 4 + c] = src.m[c * 4 + r];
}

// Recompute the 640-byte expected layout from the 3 source matrices, EXACTLY TransformBufferLayout.cs.
void expectedBytes(const Mat4& cameraToClipSpace, const Mat4& worldToCamera, const Mat4& objectToWorld,
                   std::vector<float>& out /*160 floats*/) {
  Mat4 clipSpaceToCamera, cameraToWorld, worldToObject;
  mat4Inverse(cameraToClipSpace, clipSpaceToCamera);   // :10
  mat4Inverse(worldToCamera, cameraToWorld);           // :11
  mat4Inverse(objectToWorld, worldToObject);           // :12
  Mat4 worldToClipSpace = mat4Mul(worldToCamera, cameraToClipSpace);   // :14
  Mat4 clipSpaceToWorld = mat4Mul(clipSpaceToCamera, cameraToWorld);   // :15
  Mat4 objectToCamera = mat4Mul(objectToWorld, worldToCamera);         // :16
  Mat4 objectToClipSpace = mat4Mul(objectToCamera, cameraToClipSpace); // :17
  const Mat4* order[10] = {&cameraToClipSpace, &clipSpaceToCamera, &worldToCamera, &cameraToWorld,
                           &worldToClipSpace, &clipSpaceToWorld, &objectToWorld, &worldToObject,
                           &objectToCamera, &objectToClipSpace};
  out.assign(160, 0.0f);
  for (int i = 0; i < 10; ++i) storeT(&out[i * 16], *order[i]);
}

}  // namespace

int runTransformsConstBufferSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);  // aspect = 64/64 = 1.0 (deterministic default camera)

  // KNOWN camera = the DEFAULT camera at aspect 1.0 + identity ObjectToWorld (what the driver fills).
  const float aspect = 64.0f / 64.0f;
  LayerCameraForward fwd = defaultLayerCameraForward(aspect);
  Mat4 objectToWorld = mat4Identity();
  std::vector<float> want;
  expectedBytes(fwd.cameraToClipSpace, fwd.worldToCamera, objectToWorld, want);  // 160 floats = 640 bytes

  Graph g;
  Node t; t.id = 1; t.type = "TransformsConstBuffer";
  g.nodes.push_back(t);
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  bufferInjectBug() = injectBug;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);
  bufferInjectBug() = false;

  const SwBuffer* b = pg.debugCookedSwBuffer(1);
  bool haveBuf = b && b->bytes;
  uint32_t count = haveBuf ? b->elementCount : 0u;
  uint32_t stride = haveBuf ? b->elementStride : 0u;
  bool countOK = haveBuf && count == 1u;
  bool strideOK = haveBuf && stride == 640u;

  bool bytesOK = haveBuf && countOK && strideOK;
  int firstBad = -1;
  if (bytesOK) {
    const float* data = (const float*)b->bytes->contents();
    for (int k = 0; k < 160; ++k) {
      if (std::fabs(data[k] - want[k]) > 1e-5f) { bytesOK = false; firstBad = k; break; }
    }
  }
  bool pass = haveBuf && countOK && strideOK && bytesOK;
  std::printf("[selftest-transformsconstbuffer] count=%u(want 1) stride=%u(want 640) bytes=%d firstBad=%d -> %s\n",
              count, stride, bytesOK ? 1 : 0, firstBad, pass ? "PASS" : "FAIL");

  q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

// Self-register the WO-D TransformsConstBuffer GATE row (orderBase 623, above the other fan-out teeth).
REGISTER_SELFTESTS(/*orderBase=*/623,
    {"transformsconstbuffer", runTransformsConstBufferSelfTest});

}  // namespace sw
