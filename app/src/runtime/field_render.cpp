// runtime/field_render — see field_render.h. The GPU-dispatch half of the shader-graph island.
#include "runtime/field_render.h"

#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/field_graph.h"    // FieldNode, assembleFieldMSL, AssembledField
#include "runtime/tex_op_cache.h"   // cachedSourcePSO (PSO keyed on srcHash; zero per-frame compile)

namespace sw {

namespace {
// Function names baked into the field render template (field_render_template.metal).
constexpr const char* kVsName = "sw_field_vertex";
constexpr const char* kFsName = "sw_field_fragment";
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
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, (NS::UInteger)0, (NS::UInteger)3);
  enc->endEncoding();
  cb->commit();
  cb->waitUntilCompleted();  // synchronous (orchestrator decision); golden reads back right after

  paramBuf->release();  // contents already consumed by the GPU; buffer not needed past the draw
  return out;           // caller owns
}

}  // namespace sw
