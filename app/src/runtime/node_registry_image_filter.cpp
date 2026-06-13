// runtime/node_registry_image_filter — NodeSpec table for IMAGE FILTER ops.
// Ops: Blur, Displace, Tint, ChromaticAbberation, AdjustColors.
// All take Texture2D in → Texture2D out.  Split from node_registry.cpp (批次16-R, rule 4).
#include "runtime/node_registry_image_filter.h"
#include "runtime/graph.h"

namespace sw {

const std::vector<NodeSpec>& imageFilterSpecs() {
  static const std::vector<NodeSpec> specs = {
      // Blur (TiXL Lib.image.fx.blur.Blur): the FIRST image filter — Texture2D in -> Texture2D out,
      // a 2-pass directional Gaussian (point_ops_blur.cpp). Params mirror Blur.cs: Size (reach),
      // Samples (taps), Offset (added constant), Opacity (rgb intensity -> shader Glow2). Resolution
      // picks the output texture size (same enum as RenderTarget; default WindowFollow). FORK
      // (named): TiXL's Wrap (TextureAddressMode) input is omitted — the op uses a fixed clamp
      // sampler (= MirrorOnce default for blur); non-default Wrap is a follow-up.
      {"Blur", "Blur",
       {{"Image", "Image", "Texture2D", true},
        {"out", "out", "Texture2D", false},
        {"Size", "Size", "Float", true, 1.0f, 0.0f, 100.0f},
        {"Samples", "Samples", "Float", true, 8.0f, 1.0f, 10.0f},
        {"Offset", "Offset", "Float", true, 0.0f, -1.0f, 1.0f},
        {"Opacity", "Opacity", "Float", true, 1.0f, 0.0f, 4.0f},
        {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
         {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
        {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
        {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
       nullptr},
      // Displace (TiXL Lib.image.fx.distort.Displace): the SECOND image filter and FIRST op with TWO
      // Texture2D inputs (Image + DisplaceMap). Warps Image by a direction read from DisplaceMap
      // (point_ops_displace.cpp). Params mirror Displace.cs/.t3 defaults: Displacement/
      // DisplacementOffset/Twist/Shade/SampleRadius/DisplaceMapOffset(Vec2)/DisplaceMode(enum)/
      // RGSS_4xAA(bool). Resolution picks the output texture size (same enum as RenderTarget/Blur).
      // FORKS (named): TiXL's Wrap/TextureFiltering/GenerateMips host plumbing is omitted (fixed clamp
      // + linear sampler, no mips, same fork class as Blur); an unwired DisplaceMap samples Image.
      {"Displace", "Displace",
       {{"Image", "Image", "Texture2D", true},
        {"DisplaceMap", "DisplaceMap", "Texture2D", true},
        {"out", "out", "Texture2D", false},
        {"Displacement", "Displacement", "Float", true, 0.0f, -2.0f, 2.0f},
        {"DisplacementOffset", "DisplacementOffset", "Float", true, 0.0f, -1.0f, 1.0f},
        {"Twist", "Twist", "Float", true, 0.0f, -180.0f, 180.0f},
        {"Shade", "Shade", "Float", true, 0.0f, -1.0f, 1.0f},
        {"SampleRadius", "SampleRadius", "Float", true, 1.0f, 0.0f, 10.0f},
        {"DisplaceMode", "DisplaceMode", "Float", true, 0.0f, 0.0f, 3.0f, Widget::Enum,
         {"IntensityGradient", "Intensity", "NormalMap", "SignedNormalMap"}, true},
        {"DisplaceMapOffset.x", "DisplaceMapOffset", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 2},
        {"DisplaceMapOffset.y", "DisplaceMapOffset.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"RGSS_4xAA", "RGSS_4xAA", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
         {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
        {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
        {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
       nullptr},
      // Tint (TiXL Lib.image.color.Tint): remaps input luminance through a black->white color
      // range and blends by Amount. Single Texture2D in → Texture2D out (point_ops_tint.cpp).
      // Params mirror Tint.cs: Amount/MapBlackTo(Vec4)/MapWhiteTo(Vec4)/Exposure/
      // ChannelWeights(Vec4)/GainAndBias(Vec2). Resolution = same enum as Blur. FORKS (named):
      // TiXL's Wrap omitted (fixed clamp sampler); bias-functions.hlsl inlined in tint.metal.
      {"Tint", "Tint",
       {{"Image", "Image", "Texture2D", true},
        {"out", "out", "Texture2D", false},
        {"Amount", "Amount", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Slider},
        // MapBlackTo (Vec4, TiXL default ~(0,0,0,1))
        {"MapBlackTo.r", "MapBlackTo", "Float", true, 1e-6f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"MapBlackTo.g", "MapBlackTo.g", "Float", true, 1e-6f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"MapBlackTo.b", "MapBlackTo.b", "Float", true, 1e-6f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"MapBlackTo.a", "MapBlackTo.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        // MapWhiteTo (Vec4, TiXL default (1,1,1,1))
        {"MapWhiteTo.r", "MapWhiteTo", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"MapWhiteTo.g", "MapWhiteTo.g", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"MapWhiteTo.b", "MapWhiteTo.b", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"MapWhiteTo.a", "MapWhiteTo.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        // ChannelWeights (Vec4, TiXL default (1,1,1,0))
        {"ChannelWeights.r", "ChannelWeights", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"ChannelWeights.g", "ChannelWeights.g", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"ChannelWeights.b", "ChannelWeights.b", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"ChannelWeights.a", "ChannelWeights.a", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        // GainAndBias (Vec2, TiXL default (0.5,0.5) = identity)
        {"GainAndBias.x", "GainAndBias", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
        {"GainAndBias.y", "GainAndBias.y", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Exposure", "Exposure", "Float", true, 1.0f, 0.0f, 4.0f},
        {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
         {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
        {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
        {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
       nullptr},
      // ChromaticAbberation (TiXL Lib.image.fx.stylize.ChromaticAbberation): radial chromatic
      // fringe effect splitting R and B channels outward from center. Single Texture2D in →
      // Texture2D out (point_ops_chromab.cpp). Params mirror ChromaticAbberation.cs: Image/Size/
      // Strength/SampleCount/Distort. Resolution = same enum. FORK (named): TiXL's host provides
      // TargetWidth/TargetHeight via a Resolution cbuffer; we fill it from c.output->width/height.
      {"ChromaticAbberation", "ChromaticAbberation",
       {{"Image", "Image", "Texture2D", true},
        {"out", "out", "Texture2D", false},
        // Defaults = ChromaticAbberation.t3 verbatim (refuter-R-PMF3: the first cut shipped
        // 1.0/0.3/8/0 — a freshly added node looked nothing like TiXL's).
        {"Size", "Size", "Float", true, 5.0f, 0.0f, 10.0f},
        {"Strength", "Strength", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Slider},
        {"SampleCount", "SampleCount", "Float", true, 3.0f, 3.0f, 20.0f, Widget::Slider},
        {"Distort", "Distort", "Float", true, -0.1f, -2.0f, 2.0f},
        {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
         {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
        {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
        {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
       nullptr},
      // AdjustColors (TiXL Lib.image.color.AdjustColors): comprehensive color grading — HSB ops
      // (hue/saturation/exposure/brightness/contrast), vignette, colorize, background composite.
      // Single Texture2D in → Texture2D out (point_ops_adjustcolors.cpp). Params mirror
      // AdjustColors.cs. Resolution = same enum. FORK (named): TiXL's Wrap omitted (fixed clamp).
      {"AdjustColors", "AdjustColors",
       {{"Image", "Image", "Texture2D", true},
        {"out", "out", "Texture2D", false},
        {"Exposure", "Exposure", "Float", true, 1.0f, 0.0f, 4.0f},
        {"Contrast", "Contrast", "Float", true, 0.0f, -1.0f, 2.0f},
        {"Saturation", "Saturation", "Float", true, 1.0f, 0.0f, 4.0f, Widget::Slider},
        {"Brightness", "Brightness", "Float", true, 0.0f, -1.0f, 1.0f},
        {"Hue", "Hue", "Float", true, 0.0f, -180.0f, 180.0f},
        {"Vignette", "Vignette", "Float", true, 0.0f, 0.0f, 5.0f, Widget::Slider},
        {"OrangeTeal", "OrangeTeal", "Float", true, 0.0f, -1.0f, 1.0f},
        // Colorize (Vec4, TiXL default (1,1,1,0) — alpha=0 means no colorize)
        {"Colorize.r", "Colorize", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"Colorize.g", "Colorize.g", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Colorize.b", "Colorize.b", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Colorize.a", "Colorize.a", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        // PreventClamping (Vec2, TiXL default (0, 5))
        {"PreventClamping.x", "PreventClamping", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
        {"PreventClamping.y", "PreventClamping.y", "Float", true, 5.0f, 1.0f, 10.0f, Widget::Vec, {}, true, 1},
        // Background (Vec4, TiXL default ~(0,0,0,1) — near-zero rgb transparent black)
        {"Background.r", "Background", "Float", true, 1e-6f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"Background.g", "Background.g", "Float", true, 1e-6f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Background.b", "Background.b", "Float", true, 1e-6f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Background.a", "Background.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
         {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
        {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
        {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
       nullptr},
  };
  return specs;
}

}  // namespace sw
