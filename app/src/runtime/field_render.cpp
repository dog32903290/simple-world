// runtime/field_render — see field_render.h. The GPU-dispatch half of the shader-graph island.
#include "runtime/field_render.h"

#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/field_camera.h"   // RaymarchTransforms, Mat4 (host-built camera matrices)
#include "runtime/field_graph.h"    // FieldNode, assembleFieldMSL, AssembledField
#include "runtime/tex_op_cache.h"   // cachedSourcePSO (PSO keyed on srcHash; zero per-frame compile)

namespace sw {

namespace {
// Function names baked into the field render template (field_render_template.metal). The raymarch
// template (field_raymarch_template.metal) reuses the SAME entry-point names.
constexpr const char* kVsName = "sw_field_vertex";
constexpr const char* kFsName = "sw_field_fragment";

// Host mirror of the MSL `RaymarchParams` struct (field_raymarch_template.metal) — exact byte layout.
// 7 floats + 1 pad (= one 16B + ...) then 4 float4s, packed_float3 + pad, float2. Built with explicit
// padding so a plain std::memcpy lands every field on the MSL-expected offset (no simd surprises).
struct RaymarchParamsGpu {
  float maxSteps, stepSize, minDistance, maxDistance;  // 0..15
  float fog, distToColor, aoDistance, pad1;            // 16..31
  float specular[4];                                   // 32..47
  float glow[4];                                       // 48..63
  float ambientOcclusion[4];                           // 64..79
  float background[4];                                 // 80..95
  float lightPos[3]; float pad2;                       // 96..111 (packed_float3 + pad)
  float spec[2]; float pad3[2];                        // 112..127 (float2 + tail pad to 16B)
};
}  // namespace

MTL::Texture* renderField2d(MTL::Device* dev, MTL::CommandQueue* queue,
                            const std::shared_ptr<FieldNode>& root, const std::string& templateMsl,
                            uint32_t w, uint32_t h) {
  if (!dev || !queue || !root || w == 0 || h == 0 || templateMsl.empty()) return nullptr;

  // 1. Assemble the field MSL (pure string) + packed float-param buffer + srcHash (cache key).
  AssembledField asmField = assembleFieldMSL(root, templateMsl);
  if (asmField.msl.empty()) return nullptr;

  // 2. PSO from the source cache, keyed on srcHash. MISS compiles the source via the registered
  //    field source compiler (runtime->platform leaf seam) ONCE; HIT reuses it (zero recompile).
  const MTL::PixelFormat kFmt = MTL::PixelFormatR32Float;  // signed-distance carrier (see header)
  MTL::RenderPipelineState* pso =
      cachedSourcePSO(dev, asmField.msl.c_str(), asmField.srcHash, kVsName, kFsName,
                      (uint64_t)kFmt);
  if (!pso) return nullptr;  // no compiler registered, or source/pipeline failed (logged upstream)

  // 3. Output texture (R32Float, RenderTarget|ShaderRead, Shared so the CPU golden can getBytes).
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(kFmt, w, h, /*mipmapped=*/false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* out = dev->newTexture(td);  // OWNED (+1) -> handed to caller
  if (!out) return nullptr;

  // 4. Float-param buffer at buffer(0). Rebuilt every call (cheap) so param edits don't recompile.
  //    Metal requires a non-null buffer even if the field has zero params; allocate >=16 bytes.
  const size_t paramBytes = asmField.floatParams.empty()
                                ? 16
                                : asmField.floatParams.size() * sizeof(float);
  MTL::Buffer* paramBuf = dev->newBuffer(paramBytes, MTL::ResourceStorageModeShared);
  if (!paramBuf) { out->release(); return nullptr; }
  if (!asmField.floatParams.empty())
    std::memcpy(paramBuf->contents(), asmField.floatParams.data(),
                asmField.floatParams.size() * sizeof(float));

  // 5. Fullscreen-triangle draw: clear to a far/positive sentinel, then the fragment overwrites
  //    every texel with the field distance (the triangle covers the whole viewport).
  MTL::RenderPassDescriptor* rpd = MTL::RenderPassDescriptor::renderPassDescriptor();
  MTL::RenderPassColorAttachmentDescriptor* ca = rpd->colorAttachments()->object(0);
  ca->setTexture(out);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setStoreAction(MTL::StoreActionStore);
  ca->setClearColor(MTL::ClearColor::Make(1.0e9, 0.0, 0.0, 1.0));  // sentinel: overwritten everywhere

  MTL::CommandBuffer* cb = queue->commandBuffer();
  MTL::RenderCommandEncoder* enc = cb->renderCommandEncoder(rpd);
  enc->setRenderPipelineState(pso);
  enc->setFragmentBuffer(paramBuf, 0, 0);
  // Seam A: bind each assembled field texture at its depth-first slot [[texture(i)]]. Empty for every
  // existing SDF leaf (zero-texture field) -> this loop does nothing, the draw is unchanged. The
  // opaque void* handle is cast back to MTL::Texture* HERE (the only place that names the platform
  // type), mirroring the point_ops_dither setFragmentTexture pattern.
  for (size_t i = 0; i < asmField.textures.size(); ++i) {
    if (asmField.textures[i].texture)
      enc->setFragmentTexture((MTL::Texture*)asmField.textures[i].texture, (NS::UInteger)i);
  }
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, (NS::UInteger)0, (NS::UInteger)3);
  enc->endEncoding();
  cb->commit();
  cb->waitUntilCompleted();  // synchronous (orchestrator decision); golden reads back right after

  paramBuf->release();  // contents already consumed by the GPU; buffer not needed past the draw
  return out;           // caller owns
}

MTL::Texture* renderField3d(MTL::Device* dev, MTL::CommandQueue* queue,
                            const std::shared_ptr<FieldNode>& root, const std::string& templateMsl,
                            const RaymarchTransforms& xf, const RaymarchRenderParams& params,
                            uint32_t w, uint32_t h) {
  if (!dev || !queue || !root || w == 0 || h == 0 || templateMsl.empty()) return nullptr;

  // 1. Assemble the field MSL — SAME assembleFieldMSL as the 2D path (reused unchanged). The raymarch
  //    template carries the same 6 hooks, so a given field topology cooks identically into either path.
  AssembledField asmField = assembleFieldMSL(root, templateMsl);
  if (asmField.msl.empty()) return nullptr;

  // 2. PSO from the source cache, keyed on srcHash. The raymarch fragment outputs RGBA32Float color,
  //    so the cache key includes that pixel format (distinct from the 2D R32Float key -> no collision).
  const MTL::PixelFormat kFmt = MTL::PixelFormatRGBA32Float;  // glow grayscale (R=G=B); float for readback
  MTL::RenderPipelineState* pso =
      cachedSourcePSO(dev, asmField.msl.c_str(), asmField.srcHash, kVsName, kFsName, (uint64_t)kFmt);
  if (!pso) return nullptr;

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(kFmt, w, h, /*mipmapped=*/false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* out = dev->newTexture(td);
  if (!out) return nullptr;

  // 3. buffer(0): field float-params (same as 2D). buffer(1): raymarch scalars. buffer(2): camera xf.
  const size_t paramBytes =
      asmField.floatParams.empty() ? 16 : asmField.floatParams.size() * sizeof(float);
  MTL::Buffer* paramBuf = dev->newBuffer(paramBytes, MTL::ResourceStorageModeShared);
  if (!paramBuf) { out->release(); return nullptr; }
  if (!asmField.floatParams.empty())
    std::memcpy(paramBuf->contents(), asmField.floatParams.data(),
                asmField.floatParams.size() * sizeof(float));

  RaymarchParamsGpu rp{};
  rp.maxSteps = params.maxSteps; rp.stepSize = params.stepSize;
  rp.minDistance = params.minDistance; rp.maxDistance = params.maxDistance;
  rp.fog = 1.0f; rp.distToColor = params.distToColor; rp.aoDistance = params.aoDistance;
  for (int i = 0; i < 4; ++i) {
    rp.specular[i] = params.specular[i]; rp.glow[i] = params.glow[i];
    rp.ambientOcclusion[i] = params.ambientOcclusion[i]; rp.background[i] = params.background[i];
  }
  rp.lightPos[0] = params.lightPos[0]; rp.lightPos[1] = params.lightPos[1];
  rp.lightPos[2] = params.lightPos[2];
  rp.spec[0] = params.spec[0]; rp.spec[1] = params.spec[1];
  MTL::Buffer* rmBuf = dev->newBuffer(&rp, sizeof(rp), MTL::ResourceStorageModeShared);

  // Transforms buffer: two row-major float[16] (clipSpaceToWorld, cameraToWorld) — the exact MSL
  // `struct Transforms` layout. Mat4.m is already row-major float[16].
  float xfData[32];
  std::memcpy(xfData, xf.clipSpaceToWorld.m, 16 * sizeof(float));
  std::memcpy(xfData + 16, xf.cameraToWorld.m, 16 * sizeof(float));
  MTL::Buffer* xfBuf = dev->newBuffer(xfData, sizeof(xfData), MTL::ResourceStorageModeShared);
  if (!rmBuf || !xfBuf) {
    if (rmBuf) rmBuf->release();
    if (xfBuf) xfBuf->release();
    paramBuf->release(); out->release();
    return nullptr;
  }

  // 4. Fullscreen-triangle draw. Clear to the background color so a fully-missing ray reads it back.
  MTL::RenderPassDescriptor* rpd = MTL::RenderPassDescriptor::renderPassDescriptor();
  MTL::RenderPassColorAttachmentDescriptor* ca = rpd->colorAttachments()->object(0);
  ca->setTexture(out);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setStoreAction(MTL::StoreActionStore);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));  // overwritten by the fragment everywhere

  MTL::CommandBuffer* cb = queue->commandBuffer();
  MTL::RenderCommandEncoder* enc = cb->renderCommandEncoder(rpd);
  enc->setRenderPipelineState(pso);
  enc->setFragmentBuffer(paramBuf, 0, 0);
  enc->setFragmentBuffer(rmBuf, 0, 1);
  enc->setFragmentBuffer(xfBuf, 0, 2);
  // Seam A: bind each assembled field texture at its depth-first slot [[texture(i)]] (empty for the
  // 16 SDF leaves; the texture-into-field Image2dSDF leaf would populate it). Same cast site as 2D.
  for (size_t i = 0; i < asmField.textures.size(); ++i) {
    if (asmField.textures[i].texture)
      enc->setFragmentTexture((MTL::Texture*)asmField.textures[i].texture, (NS::UInteger)i);
  }
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, (NS::UInteger)0, (NS::UInteger)3);
  enc->endEncoding();
  cb->commit();
  cb->waitUntilCompleted();

  paramBuf->release();
  rmBuf->release();
  xfBuf->release();
  return out;  // caller owns
}

}  // namespace sw
