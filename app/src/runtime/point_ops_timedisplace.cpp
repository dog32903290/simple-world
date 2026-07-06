// TimeDisplace feedback fx — per-pixel TIME displacement. It owns an internal N-slice history RING (the
// SAME machine as KeepInTextureArray, ensureFeedbackArray): each cook it blits the current Image into the
// ring at an advancing write head, then a fragment pass reads the ring per-pixel — the DisplaceMap's
// brightness picks how many frames BACK each pixel reaches (slice = (SliceIndex + sliceOffset) % N). Moving
// regions smear across time. That ring SURVIVING across frames IS the feature.
//
// TiXL authority: external/tixl Operators/Lib/image/fx/distort/TimeDisplace.{cs,t3} + the atomic core
// Operators/Lib/Assets/shaders/img/fx/TimeDisplace.hlsl (ported to timedisplace.metal).
//   ★TimeDisplace.t3 is a COMPOUND: an INTERNAL KeepInTextureArray child (ArraySize 128, WriteIndex from a
//   frame CountInt, ReadIndex = counter+2, TriggerWrite=true — .t3:98-124) feeds its TextureArray output
//   into the shader's Image (the Texture2DArray, .t3:271-275). We COLLAPSE that compound: the op owns the
//   ring directly (no cross-op array wire — sw has none; the NAMED FORK from KeepInTextureArray), driving
//   WriteIndex with an internal per-node frame counter (mirror of the .t3's CountInt), and the shader reads
//   with SliceIndex = the current write head (the freshest frame is the ring "head", offsets reach BACK).
//   The .t3's ReadIndex=counter+2 is a fixed +2 latency; we expose SliceIndex directly so a golden can
//   pin an exact head. DisplaceMap + Displacement are the OUTER pins (TimeDisplace.cs:12-19).
//
// ★NAMED FORK (source-grounded): the .cs declares Twist/Shade/SampleRadius/WrapMode/RGSS/DisplaceMode/
//   GenerateMips/TextureFiltering. TimeDisplace.hlsl psMain (the ONLY entry) reads NONE of them — only
//   DisplaceAmount/SliceIndex/ArrayLength. We port psMain's load-bearing math and DROP the unread pins
//   (they belong to the .t3's dropped multi-sample/preview machinery). Faithful to the observable output.
//
// injectBug (timeDisplaceInjectBug): forces sliceOffset math into the WRONG slice (SliceIndex only, no
//   offset) via a param flag → the per-pixel time-reach collapses to a single frame → the displacement
//   tooth diverges. Off in production.
#include <Metal/Metal.hpp>

#include <cmath>
#include <map>
#include <string>

#include "runtime/image_filter_op_registry.h"    // FeedbackOp (spec + selftest + registerFeedbackOp)
#include "runtime/point_graph.h"                  // FeedbackCookCtx, cookParam, PointFeedbackFn
#include "runtime/point_graph_feedback_store.h"   // FeedbackStore::ensureFeedbackArray (the N-slice ring)
#include "runtime/tex_op_cache.h"                 // cachedTexPSO
#include "runtime/timedisplace_params.h"          // TimeDisplaceParams, TIMEDISPLACE_Params

namespace sw {

int runTimeDisplaceSelfTest(bool injectBug);

// Test-only injection seam: zero out the sliceOffset (read only SliceIndex), so every pixel reaches the
// SAME frame regardless of the DisplaceMap → the time-displacement tooth diverges. Off in production.
bool& timeDisplaceInjectBug() {
  static bool b = false;
  return b;
}

// Per-node cross-frame WRITE head (mirror of the .t3 CountInt frame counter). Persists between cooks,
// keyed by the node's resource key. A file-static map (the ring itself lives in the driver's
// FeedbackStore; only the tiny int counter lives here). Reset semantics: never reset (a monotonic frame
// counter, exactly like the .t3's Counter node); the ring modulo keeps it bounded.
static std::map<std::string, int>& writeHeads() {
  static std::map<std::string, int> m;
  return m;
}

namespace {

void cookTimeDisplace(FeedbackCookCtx& c) {
  const MTL::Texture* image = c.inputTextures[0];    // Image      = first Texture2D input
  const MTL::Texture* dmap = c.inputTextures[1];     // DisplaceMap = second Texture2D input
  if (!image || !c.store || !c.queue || !c.lib) return;  // no Image → nothing to route

  const uint32_t w = (uint32_t)image->width(), h = (uint32_t)image->height();
  if (w == 0 || h == 0) return;
  const MTL::PixelFormat fmt = image->pixelFormat();

  int n = (int)std::lround(cookParam(c, "ArraySize", 128.0f));  // .t3 ArrayLength 128 (.t3:83-88)
  if (n < 1) n = 1;
  if (n > 256) n = 256;

  MTL::Texture* ring = c.store->ensureFeedbackArray(c.nodeKey, w, h, fmt, (uint32_t)n);
  if (!ring) return;

  // WRITE: blit current Image into ring slice (writeHead mod N), then advance the head (the .t3 CountInt
  // increments every frame; TriggerWrite is always true in the compound). This is the ring's "current".
  int& head = writeHeads()[c.nodeKey];
  const uint32_t writeSlice = (uint32_t)(((head % n) + n) % n);
  {
    MTL::CommandBuffer* cmd = c.queue->commandBuffer();
    MTL::BlitCommandEncoder* blit = cmd->blitCommandEncoder();
    blit->copyFromTexture(const_cast<MTL::Texture*>(image), 0, 0, MTL::Origin::Make(0, 0, 0),
                          MTL::Size::Make(w, h, 1), ring, writeSlice, 0, MTL::Origin::Make(0, 0, 0));
    blit->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
  }

  // READ head = the slice we JUST wrote (the freshest frame). SliceIndex param lets a golden pin an exact
  // head; default = the live write head (the .t3 reads counter+2, a fixed latency — we expose it instead).
  int sliceIndex = (int)std::lround(cookParam(c, "SliceIndex", (float)writeSlice));
  // Displacement (outer pin, TimeDisplace.cs:18-19 / .t3:5-6 default 0.0). injectBug forces amount→0 so
  // sliceOffset collapses to 0 (every pixel reads SliceIndex only) → the time-reach vanishes.
  float displaceAmount = cookParam(c, "Displacement", 0.0f);
  if (timeDisplaceInjectBug()) displaceAmount = 0.0f;

  head = head + 1;  // advance the frame counter AFTER pinning this frame's write/read

  MTL::RenderPipelineState* pso = cachedTexPSO(c.dev, c.lib, "timedisplace_vs", "timedisplace_fs", fmt);
  if (!pso) return;

  // Point sampler (.t3 _multiImageFxSetupStatic TextureFilter=MinMagMipPoint, .t3:135-138): the ring
  // slices are solid in the golden; a linear filter across DISTINCT slices would blend.
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterNearest);
  sd->setMagFilter(MTL::SamplerMinMagFilterNearest);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // Output: the op's persistent single texture (reuse the driver's feedback PAIR slot, pairA — one output
  // texture; pairB unused, no ping-pong). Sized to (w,h,fmt) via the store, released on realloc + teardown.
  MTL::Texture* out = nullptr;
  {
    MTL::Texture* a = nullptr;
    MTL::Texture* b = nullptr;
    if (c.store->ensureFeedbackPair(c.nodeKey, w, h, fmt, a, b)) out = a;
  }
  if (!out) { samp->release(); return; }

  TimeDisplaceParams P{};
  P.DisplaceAmount = displaceAmount;
  P.SliceIndex = sliceIndex;
  P.ArrayLength = n;
  P._pad = 0;

  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(out);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  enc->setRenderPipelineState(pso);
  enc->setFragmentTexture(ring, 0);                                  // the N-slice history ring
  enc->setFragmentTexture(dmap ? const_cast<MTL::Texture*>(dmap) : ring, 1);  // DisplaceMap (fallback: ring)
  enc->setFragmentSamplerState(samp, 0);
  enc->setFragmentBytes(&P, sizeof(TimeDisplaceParams), TIMEDISPLACE_Params);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();  // readback golden + a same-frame downstream sample need it done

  samp->release();
  c.outputs[0] = out;
}

}  // namespace

// Self-registration. FeedbackOp: registerFeedbackOp + spec sink + selftest sink. needsPair=false (the op
// sizes both the ring AND its output-texture slot itself via c.store; the driver's 2-buffer pre-size is
// unused). Ports: Image + DisplaceMap = Texture2D inputs; Displacement/SliceIndex/ArraySize = Float; the
// Output = Texture2D. Twist/Shade/etc. DROPPED (named fork — psMain reads none). Outer defaults from
// TimeDisplace.t3: Displacement 0.0 (.t3:5-6), ArraySize 128 (.t3 internal ArrayLength .t3:83-88).
static const FeedbackOp _reg_timedisplace{
    {"TimeDisplace", "TimeDisplace",
     {{"Image", "Image", "Texture2D", true},
      {"DisplaceMap", "DisplaceMap", "Texture2D", true},
      {"Displacement", "Displacement", "Float", true, 0.0f, -256.0f, 256.0f},  // .t3:5-6 default 0
      {"SliceIndex", "SliceIndex", "Float", true, 0.0f, 0.0f, 255.0f},
      {"ArraySize", "ArraySize", "Float", true, 128.0f, 1.0f, 256.0f},         // .t3 ArrayLength 128
      {"Output", "Output", "Texture2D", false}},
     /*evaluate=*/nullptr},  // Texture2D output cannot ride NodeSpec::evaluate (returns ONE float)
    "TimeDisplace", cookTimeDisplace, /*needsPair=*/false,
    /*pairFormat=*/0, "timedisplace", runTimeDisplaceSelfTest};

}  // namespace sw
