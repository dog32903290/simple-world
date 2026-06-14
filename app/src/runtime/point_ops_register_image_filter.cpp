// runtime/point_ops_register_image_filter — per-family registrar for IMAGE FILTER ops
// (Texture2D → Texture2D). Split from point_ops.cpp's central registerBuiltinPointOps
// (node_registry.cpp pattern, ARCHITECTURE rule 7). Adding an image-filter op edits ONLY this
// file. Central builder unchanged.
//
// Zero behaviour change: op names + cook bindings verbatim from the original central function
// (all registrars declared in point_ops.h).
#include "runtime/point_ops.h"  // registerBlurOp/Displace/Tint/ChromaBA/AdjustColors/ChannelMixer

namespace sw {

void registerImageFilterPointOps() {
  registerBlurOp();          // Texture2D → Texture2D (first image filter, lane I)
  registerDisplaceOp();      // Image + DisplaceMap → Texture2D (lane D2, dual tex in)
  registerTintOp();          // Texture2D → Texture2D (color tint/remap, lane F3-1)
  registerChromaBAOp();      // Texture2D → Texture2D (chromatic fringe, lane F3-2)
  registerAdjustColorsOp();  // Texture2D → Texture2D (color grading, lane F3-3)
  registerChannelMixerOp();  // Texture2D → Texture2D (channel matrix mix, lane image_filter)
}

}  // namespace sw
