// SwapTextures feedback/routing op (the SECOND multi-tex-output op — a STATELESS verification leaf for
// the feedback seam). It has two Texture2D inputs + two Texture2D outputs and routes one to the other,
// optionally swapped. NO cross-frame state (no double-buffer pair) — it proves the multi-tex-output
// path in isolation from the cross-frame-pair path. Named point_ops_*.cpp for the CMake glob.
//
// TiXL authority: external/tixl/Operators/Lib/image/use/SwapTextures.cs (ported VERBATIM below — line
// refs to that file) + SwapTextures.t3 default mirror:
//   - TextureA = Slot<Texture2D>  (cs:6-7)   → our "TextureA" Texture2D OUTPUT (spec port idx 2).
//   - TextureB = Slot<Texture2D>  (cs:9-10)  → our "TextureB" Texture2D OUTPUT (spec port idx 3).
//   - TextureAInput = InputSlot<Texture2D> (cs:39-40) → "TextureAInput" Texture2D INPUT (idx 0); .t3 null.
//   - TextureBInput = InputSlot<Texture2D> (cs:42-43) → "TextureBInput" Texture2D INPUT (idx 1); .t3 null.
//   - EnableSwap    = InputSlot<bool>      (cs:45-46) → Float Widget::Bool; .t3 DefaultValue = FALSE.
//   - Note the CROSS-WIRE (cs:21-22): textureAValue ← TextureBInput, textureBValue ← TextureAInput.
//     Net effect after the EnableSwap branch (cs:24-33):
//       EnableSwap FALSE (.t3 default): TextureA ← TextureAInput, TextureB ← TextureBInput (PASSTHROUGH);
//       EnableSwap TRUE              : TextureA ← TextureBInput, TextureB ← TextureAInput (SWAPPED).
//     We transcribe the cs:21-33 routing exactly (the cross-wire + the branch) rather than the
//     simplified net, so the op stays line-for-line auditable against the source.
//
// No cross-frame pair: needsPair=false → the driver never allocates a feedback pair for this type, the
// op only routes its inputs to outputs (zero Metal allocation, zero state). The driver runs it once per
// frame (the per-frame feedback memo) and returns outputs[ordinal] for the consumer's output port.
#include <Metal/Metal.hpp>

#include "runtime/image_filter_op_registry.h"  // FeedbackOp (spec + selftest + registerFeedbackOp sink)
#include "runtime/point_graph.h"                // FeedbackCookCtx, cookParam, PointFeedbackFn

namespace sw {

int runSwapTexturesSelfTest(bool injectBug);

// Test-only injection seam: when set, FORCE passthrough regardless of EnableSwap (so the golden's
// EnableSwap=true SWAP assertion diverges → the RED tooth bites). Off in production.
bool& swapTexturesInjectBug() {
  static bool b = false;
  return b;
}

namespace {

void cookSwapTextures(FeedbackCookCtx& c) {
  const MTL::Texture* inA = c.inputTextures[0];  // TextureAInput
  const MTL::Texture* inB = c.inputTextures[1];  // TextureBInput

  // cs:21-22 cross-wire: textureAValue ← TextureBInput, textureBValue ← TextureAInput.
  const MTL::Texture* textureAValue = inB;
  const MTL::Texture* textureBValue = inA;

  bool enableSwap = cookParam(c, "EnableSwap", 0.0f) > 0.5f;  // .t3 default FALSE
  if (swapTexturesInjectBug()) enableSwap = false;            // bug: force passthrough branch

  // cs:24-33 branch (outputs[0]=TextureA, outputs[1]=TextureB).
  if (enableSwap) {
    c.outputs[0] = const_cast<MTL::Texture*>(textureAValue);  // TextureA ← textureAValue (cs:26)
    c.outputs[1] = const_cast<MTL::Texture*>(textureBValue);  // TextureB ← textureBValue (cs:27)
  } else {
    c.outputs[0] = const_cast<MTL::Texture*>(textureBValue);  // TextureA ← textureBValue (cs:31)
    c.outputs[1] = const_cast<MTL::Texture*>(textureAValue);  // TextureB ← textureAValue (cs:32)
  }
}

}  // namespace

// Self-registration. Stateless feedback/routing op: needsPair=false (no double-buffer pair).
//   Ports: TextureAInput + TextureBInput = Texture2D inputs; EnableSwap = Float Widget::Bool (.t3
//          default 0=FALSE); TextureA + TextureB = Texture2D outputs (TextureA FIRST, cs:6-10 order).
static const FeedbackOp _reg_swaptextures{
    {"SwapTextures", "SwapTextures",
     {{"TextureAInput", "TextureAInput", "Texture2D", true},
      {"TextureBInput", "TextureBInput", "Texture2D", true},
      {"EnableSwap", "EnableSwap", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},  // .t3 default FALSE
      {"TextureA", "TextureA", "Texture2D", false},
      {"TextureB", "TextureB", "Texture2D", false}},
     /*evaluate=*/nullptr},
    "SwapTextures", cookSwapTextures, /*needsPair=*/false, /*pairFormat=*/0, "swaptextures",
    runSwapTexturesSelfTest};

}  // namespace sw
