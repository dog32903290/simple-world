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
      // ToneMapping (TiXL Lib.image.color.ToneMapping): per-pixel tone mapping curve.
      // Single Texture2D in → Texture2D out (point_ops_tonemapping.cpp). Kernel: ToneMap.hlsl
      // per-mode if/else chain (Aces/Reinhard/Filmic/Uncharted2/AgX/AgX_Punchy/None) with
      // optional gamma correction. Params mirror ToneMapping.cs: Texture2d/Mode(enum)/
      // CorrectGamma(bool)/Gamma(float)/Exposure(float). FORKS (named):
      // 1. DX11 PS -> Metal fullscreen-triangle VS+FS (same fork class as Tint/ChannelMixer).
      // 2. Mode passed as float in cbuffer (TiXL int enum -> float threshold dispatch, _ForceKind pattern).
      // 3. TiXL bug verbatim: ToneMap.hlsl:105 'Mode<4.5' makes AgX_Punchy(5) unreachable.
      //    We clone the dead branch faithfully (named fork[verbatim-TiXL-bug] in shader).
      // 4. Fixed linear+clamp sampler (TiXL host wrap knobs omitted).
      // 5. HLSL mul(vec,mat) row-major -> Metal column-layout transpose (named in tonemapping.metal).
      {"ToneMapping", "ToneMapping",
       {{"Image", "Image", "Texture2D", true},
        {"out", "out", "Texture2D", false},
        // Mode (int enum, TiXL default 0 = Aces)
        {"Mode", "Mode", "Float", true, 0.0f, 0.0f, 6.0f, Widget::Enum,
         {"Aces", "Reinhard", "Filmic", "Uncharted2", "AgX", "AgX_Punchy", "None"}, true},
        // CorrectGamma (bool, TiXL default false)
        {"CorrectGamma", "CorrectGamma", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        // Gamma (float, TiXL default not explicit in .cs; ToneMap.hlsl uses it as-is; common 2.2)
        {"Gamma", "Gamma", "Float", true, 2.2f, 0.1f, 4.0f},
        // Exposure (float, TiXL default 1.0)
        {"Exposure", "Exposure", "Float", true, 1.0f, 0.0f, 4.0f},
        {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
         {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
        {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
        {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
       nullptr},
      // ChannelMixer (TiXL Lib.image.color.ChannelMixer): per-pixel 4x4 channel matrix mix.
      // Single Texture2D in → Texture2D out (point_ops_channelmixer.cpp). Kernel:
      // out.r = dot(clamp(src,0,∞), col(MultiplyR,r)) + Add.r, etc. (MixChannels.hlsl verbatim).
      // Params mirror ChannelMixer.cs/.t3: Texture2d/MultiplyR/G/B/A(Vec4)/Add(Vec4)/
      // GenerateMipmaps/ClampResult. FORKS (named): TiXL GenerateMipmaps not dispatched
      // (mip-gen follow-up); fixed linear+clamp sampler (same fork class as Tint/AdjustColors).
      {"ChannelMixer", "ChannelMixer",
       {{"Image", "Image", "Texture2D", true},
        {"out", "out", "Texture2D", false},
        // MultiplyR (Vec4, TiXL t3 default (1,0,0,0) — identity row for red output)
        {"MultiplyR.r", "MultiplyR", "Float", true, 1.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 4},
        {"MultiplyR.g", "MultiplyR.g", "Float", true, 0.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 1},
        {"MultiplyR.b", "MultiplyR.b", "Float", true, 0.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 1},
        {"MultiplyR.a", "MultiplyR.a", "Float", true, 0.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 1},
        // MultiplyG (Vec4, TiXL t3 default (0,1,0,0) — identity row for green output)
        {"MultiplyG.r", "MultiplyG", "Float", true, 0.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 4},
        {"MultiplyG.g", "MultiplyG.g", "Float", true, 1.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 1},
        {"MultiplyG.b", "MultiplyG.b", "Float", true, 0.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 1},
        {"MultiplyG.a", "MultiplyG.a", "Float", true, 0.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 1},
        // MultiplyB (Vec4, TiXL t3 default (0,0,1,0) — identity row for blue output)
        {"MultiplyB.r", "MultiplyB", "Float", true, 0.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 4},
        {"MultiplyB.g", "MultiplyB.g", "Float", true, 0.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 1},
        {"MultiplyB.b", "MultiplyB.b", "Float", true, 1.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 1},
        {"MultiplyB.a", "MultiplyB.a", "Float", true, 0.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 1},
        // MultiplyA (Vec4, TiXL t3 default (0,0,0,1) — identity row for alpha output)
        {"MultiplyA.r", "MultiplyA", "Float", true, 0.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 4},
        {"MultiplyA.g", "MultiplyA.g", "Float", true, 0.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 1},
        {"MultiplyA.b", "MultiplyA.b", "Float", true, 0.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 1},
        {"MultiplyA.a", "MultiplyA.a", "Float", true, 1.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 1},
        // Add (Vec4, TiXL t3 default (0,0,0,0))
        {"Add.r", "Add", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"Add.g", "Add.g", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Add.b", "Add.b", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Add.a", "Add.a", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        // GenerateMipmaps (bool, TiXL default false) — read, not dispatched (fork named above)
        {"GenerateMipmaps", "GenerateMipmaps", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        // ClampResult (bool, TiXL default true)
        {"ClampResult", "ClampResult", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
         {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
        {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
        {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
       nullptr},
      // Pixelate (TiXL Lib.image.fx.stylize.Pixelate): tile-quantize the image into a grid, point-
      // sample each cell center, multiply by Color. Single Texture2D in → Texture2D out
      // (point_ops_pixelate.cpp). Kernel: Pixelate.hlsl — Divisor>0.5 -> floor(res/(Divisor*2))
      // tiles, else TileAmount tiles; uv snapped to tile center; SampleLevel point sample.
      // Params mirror Pixelate.cs/.t3: Image/Color(Vec4)/Divisor(int)/TileAmount(Int2). FORKS
      // (named): TiXL's Shape texture input omitted (default Shape=white.png = no-op multiplier,
      // see point_ops_pixelate.cpp); Int Divisor/TileAmount modeled as Float (same as SampleCount);
      // fixed clamp sampler (Pixelate.t3 WrapMode=Clamp verbatim).
      {"Pixelate", "Pixelate",
       {{"Image", "Image", "Texture2D", true},
        {"out", "out", "Texture2D", false},
        // Divisor (int, TiXL t3 default 0 — Divisor<=0.5 path uses TileAmount).
        {"Divisor", "Divisor", "Float", true, 0.0f, 0.0f, 64.0f},
        // TileAmount (Int2, TiXL t3 default (160,90)).
        {"TileAmount.x", "TileAmount", "Float", true, 160.0f, 1.0f, 1024.0f, Widget::Vec, {}, true, 2},
        {"TileAmount.y", "TileAmount.y", "Float", true, 90.0f, 1.0f, 1024.0f, Widget::Vec, {}, true, 1},
        // Color (Vec4, TiXL t3 default (1,1,1,1) — output multiplier, identity = white).
        {"Color.r", "Color", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"Color.g", "Color.g", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.b", "Color.b", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.a", "Color.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
         {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
        {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
        {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
       nullptr},
      // Sharpen (TiXL Lib.image.fx.blur.Sharpen): 3x3 desaturated-Laplacian unsharp mask. Single
      // Texture2D in → Texture2D out (point_ops_sharpen.cpp). Kernel: Sharpen.hlsl —
      // final = col + col*Strength*(8*L(center) - 8 neighbour luminances), optional Clamping
      // saturate. Params mirror Sharpen.cs/.t3: Image/SampleRadius/Strength/Clamping. FORKS
      // (named): fixed clamp sampler vs TiXL Wrap=MirrorOnce (1px edge ring); TiXL OutputFormat
      // R16F not adopted (uses output texture's own format).
      {"Sharpen", "Sharpen",
       {{"Image", "Image", "Texture2D", true},
        {"out", "out", "Texture2D", false},
        // SampleRadius (float, TiXL t3 default 1.0).
        {"SampleRadius", "SampleRadius", "Float", true, 1.0f, 0.0f, 10.0f},
        // Strength (float, TiXL t3 default 1.0).
        {"Strength", "Strength", "Float", true, 1.0f, 0.0f, 4.0f},
        // Clamping (bool, TiXL t3 default false).
        {"Clamping", "Clamping", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
         {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
        {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
        {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
       nullptr},
      // DetectEdges (TiXL Lib.image.fx.stylize.DetectEdges): 4-neighbour absolute-difference edge
      // detector. Single Texture2D in → Texture2D out (point_ops_detectedges.cpp). Kernel:
      // DetectEdges.hlsl — average = sum_rgb(|x1-m|+|x2-m|+|y1-m|+|y2-m|)*Strength + Contrast,
      // tinted by Color, lerp to original by MixOriginal, optional transparent output.
      // Params mirror DetectEdges.cs/.t3: Image/SampleRadius/Strength/Contrast/Color(Vec4)/
      // MixOriginal/OutputAsTransparent. FORKS (named): HLSL b2 `int Invert` is NOT a .cs input
      // (never wired, always 0) — not exposed; texture dims read in-shader (no Resolution cbuffer
      // port); fixed clamp sampler (TiXL Wrap=MirrorOnce host knob not exposed).
      {"DetectEdges", "DetectEdges",
       {{"Image", "Image", "Texture2D", true},
        {"out", "out", "Texture2D", false},
        // SampleRadius (float, TiXL t3 default 1.0).
        {"SampleRadius", "SampleRadius", "Float", true, 1.0f, 0.0f, 10.0f},
        // Strength (float, TiXL t3 default 1.0).
        {"Strength", "Strength", "Float", true, 1.0f, 0.0f, 10.0f},
        // Contrast (float, TiXL t3 default 0.0).
        {"Contrast", "Contrast", "Float", true, 0.0f, -1.0f, 1.0f},
        // Color (Vec4, TiXL t3 default (1,1,1,1) — edge tint).
        {"Color.r", "Color", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"Color.g", "Color.g", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.b", "Color.b", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.a", "Color.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        // MixOriginal (float, TiXL t3 default 0.0).
        {"MixOriginal", "MixOriginal", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Slider},
        // OutputAsTransparent (bool, TiXL t3 default false).
        {"OutputAsTransparent", "OutputAsTransparent", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
         {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
        {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
        {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
       nullptr},
      // ChromaticDistortion (TiXL Lib.image.fx.distort.ChromaticDistortion): radial bulge warp +
      // N-sample chromatic radial blur. Single Texture2D in → Texture2D out
      // (point_ops_chromaticdistortion.cpp). Kernel: ChromaticDistortion.hlsl — chromaShift()
      // splits R/B from opposite ends of the radial sample line, lerp blurred<->chromarized by
      // Colorize. Params mirror ChromaticDistortion.cs/.t3: Texture2d/Center(Vec2)/Size/Colorize/
      // Distort/DistortOffset/ScaleImage/SampleCount(int). FORKS (named): b1 TimeConstants cbuffer
      // unused -> omitted; fixed clamp sampler; SampleCount Int modeled as Float.
      {"ChromaticDistortion", "ChromaticDistortion",
       {{"Image", "Image", "Texture2D", true},
        {"out", "out", "Texture2D", false},
        // Center (Vec2, TiXL t3 default (0,0)).
        {"Center.x", "Center", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 2},
        {"Center.y", "Center.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        // Size (float, TiXL t3 default 0.05).
        {"Size", "Size", "Float", true, 0.05f, 0.0f, 1.0f},
        // Colorize (float, TiXL t3 default 0.1).
        {"Colorize", "Colorize", "Float", true, 0.1f, 0.0f, 1.0f, Widget::Slider},
        // Distort (float, TiXL t3 default 0.1).
        {"Distort", "Distort", "Float", true, 0.1f, -2.0f, 2.0f},
        // DistortOffset (float, TiXL t3 default 0.5).
        {"DistortOffset", "DistortOffset", "Float", true, 0.5f, 0.0f, 2.0f},
        // ScaleImage (float, TiXL t3 default 1.0).
        {"ScaleImage", "ScaleImage", "Float", true, 1.0f, 0.1f, 4.0f},
        // SampleCount (int, TiXL t3 default 16; clamped to even 1..100 in shader).
        {"SampleCount", "SampleCount", "Float", true, 16.0f, 1.0f, 100.0f},
        {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
         {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
        {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
        {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
       nullptr},
      // VoronoiCells (TiXL Lib.image.fx.stylize.VoronoiCells): iq Voronoi cell mosaic with correct
      // border distances. Single Texture2D in → Texture2D out (point_ops_voronoicells.cpp). Kernel:
      // VoronoiCells.hlsl — input texture is the feature-point + cell-colour field; cell borders
      // tinted by EdgeColor. Params mirror VoronoiCells.cs/.t3: Image/EdgeColor(Vec4)/
      // Background(Vec4)/Scale/EdgeWidth/Phase. The .cs `Resolution` Int2 input is the OUTPUT
      // texture size selector → modeled as the standard Resolution enum + CustomW/H (NOT a b0
      // cbuffer field). FORKS (named): HLSL b2 Resolution(TargetWidth/Height) filled host-side from
      // the output size (aspect); fixed clamp sampler (TiXL Wrap=Clamp verbatim).
      {"VoronoiCells", "VoronoiCells",
       {{"Image", "Image", "Texture2D", true},
        {"out", "out", "Texture2D", false},
        // EdgeColor (Vec4, TiXL t3 default (0,0,0,1) — black edges).
        {"EdgeColor.r", "EdgeColor", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"EdgeColor.g", "EdgeColor.g", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"EdgeColor.b", "EdgeColor.b", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"EdgeColor.a", "EdgeColor.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        // Background (Vec4, TiXL t3 default (1,1,1,1) — white cell tint multiplier).
        {"Background.r", "Background", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"Background.g", "Background.g", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Background.b", "Background.b", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Background.a", "Background.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        // Scale (float, TiXL t3 default 10.0).
        {"Scale", "Scale", "Float", true, 10.0f, 0.1f, 100.0f},
        // EdgeWidth (float, TiXL t3 default 0.68).
        {"EdgeWidth", "EdgeWidth", "Float", true, 0.68f, 0.0f, 4.0f, Widget::Slider},
        // Phase (float, TiXL t3 default 0.0).
        {"Phase", "Phase", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
         {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
        {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
        {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
       nullptr},
  };
  return specs;
}

}  // namespace sw
