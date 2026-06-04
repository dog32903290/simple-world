# Lib.image Porting Research

Scope: all TiXL nodes whose full path starts with `Lib.image`, including analyze, color, fx, generate, transform, and use. This is research only; no Vuo node code is proposed here.

Evidence policy: C# slot declarations and `.t3` graph/shader references are treated as the strongest local evidence; TiXL `.help/docs` summaries explain user-facing intent; Vuo source is used only to identify available VuoImage / built-in image mappings. Unknown means not verified in the checked sources.

## Namespace Totals And Initial Grades

| namespace | nodes | A | B | C | D |
|---|---:|---:|---:|---:|---:|
| `Lib.image.analyze` | 7 | 0 | 0 | 7 | 0 |
| `Lib.image.color` | 11 | 0 | 0 | 11 | 0 |
| `Lib.image.fx._` | 2 | 0 | 0 | 0 | 2 |
| `Lib.image.fx._obsolete` | 1 | 0 | 0 | 0 | 1 |
| `Lib.image.fx.blur` | 5 | 0 | 0 | 5 | 0 |
| `Lib.image.fx.distort` | 9 | 0 | 0 | 9 | 0 |
| `Lib.image.fx.feedback` | 7 | 0 | 0 | 4 | 3 |
| `Lib.image.fx.glitch` | 4 | 0 | 0 | 3 | 1 |
| `Lib.image.fx.stylize` | 15 | 0 | 0 | 14 | 1 |
| `Lib.image.generate` | 1 | 0 | 0 | 1 | 0 |
| `Lib.image.generate._obsolete` | 2 | 0 | 0 | 0 | 2 |
| `Lib.image.generate.basic` | 9 | 0 | 0 | 8 | 1 |
| `Lib.image.generate.fractal` | 1 | 0 | 0 | 1 | 0 |
| `Lib.image.generate.load` | 4 | 0 | 0 | 4 | 0 |
| `Lib.image.generate.misc` | 3 | 0 | 0 | 1 | 2 |
| `Lib.image.generate.noise` | 5 | 0 | 0 | 5 | 0 |
| `Lib.image.generate.pattern` | 9 | 0 | 0 | 9 | 0 |
| `Lib.image.transform` | 6 | 0 | 0 | 5 | 1 |
| `Lib.image.use` | 19 | 0 | 5 | 8 | 6 |
| **Total** | **120** | **0** | **5** | **95** | **20** |

Grade reading for this file: A is effectively unused because these are image/runtime nodes; B is reserved for simple VuoImage selection/list nodes with no shader evidence; C means useful image/shader work that can be designed in Vuo/ISF/Metal/GLSL; D means DX11, compute, render-target, texture-reference, or internal helper coupling makes it document-only for the first pass.

## Dependency Split

- Shader/HLSL evidence found: 94 nodes. Full list: `Lib.image.analyze.GetImageBrightness`, `Lib.image.analyze.ImageLevels`, `Lib.image.analyze.OpticalFlow`, `Lib.image.analyze.RemoveStaticBackground`, `Lib.image.analyze.WaveForm`, `Lib.image.color.AdjustColors`, `Lib.image.color.ChannelMixer`, `Lib.image.color.ColorGrade`, `Lib.image.color.ColorGradeDepth`, `Lib.image.color.ConvertColors`, `Lib.image.color.ConvertFormat`, `Lib.image.color.HSE`, `Lib.image.color.KeyColor`, `Lib.image.color.RemapColor`, `Lib.image.color.Tint`, `Lib.image.color.ToneMapping`, `Lib.image.fx._obsolete.TriangleGridTransition`, `Lib.image.fx.blur.Bloom`, `Lib.image.fx.blur.Blur`, `Lib.image.fx.blur.DirectionalBlur`, `Lib.image.fx.blur.FastBlur`, `Lib.image.fx.blur.Sharpen`, `Lib.image.fx.distort.BubbleZoom`, `Lib.image.fx.distort.ChromaticDistortion`, `Lib.image.fx.distort.Displace`, `Lib.image.fx.distort.DistortAndShade`, `Lib.image.fx.distort.EdgeRepeat`, `Lib.image.fx.distort.FieldToImage`, `Lib.image.fx.distort.KochKaleidoskope`, `Lib.image.fx.distort.PolarCoordinates`, `Lib.image.fx.distort.TimeDisplace`, `Lib.image.fx.feedback.AdvancedFeedback2`, `Lib.image.fx.feedback.SimpleLiquid`, `Lib.image.fx.feedback.SimpleLiquid2`, `Lib.image.fx.glitch.GlitchDisplace`, `Lib.image.fx.glitch.RgbTV`, `Lib.image.fx.glitch.SortPixelGlitch`, `Lib.image.fx.glitch.SubdivisionStretch`, `Lib.image.fx.stylize.AsciiRender`, `Lib.image.fx.stylize.ChromaticAbberation`, `Lib.image.fx.stylize.ColorPhysarum`, `Lib.image.fx.stylize.DetectEdges`, `Lib.image.fx.stylize.Dither`, `Lib.image.fx.stylize.FakeLight`, `Lib.image.fx.stylize.HoneyCombTiles`, `Lib.image.fx.stylize.LightRaysFx`, `Lib.image.fx.stylize.MosiacTiling`, `Lib.image.fx.stylize.Pixelate`, `Lib.image.fx.stylize.StarGlowStreaks`, `Lib.image.fx.stylize.Steps`, `Lib.image.fx.stylize.VoronoiCells`, `Lib.image.generate.MunchingSquares2`, `Lib.image.generate._obsolete._BlobOld`, `Lib.image.generate._obsolete._FractalNoiseOld`, `Lib.image.generate.basic.Blob`, `Lib.image.generate.basic.BoxGradient`, `Lib.image.generate.basic.CheckerBoard`, `Lib.image.generate.basic.LinearGradient`, `Lib.image.generate.basic.NGon`, `Lib.image.generate.basic.NGonGradient`, `Lib.image.generate.basic.RadialGradient`, `Lib.image.generate.basic.RenderTarget`, `Lib.image.generate.basic.RoundedRect`, `Lib.image.generate.fractal.MandelbrotFractal`, `Lib.image.generate.misc.JumpFloodFill`, `Lib.image.generate.misc.SlidingHistory`, `Lib.image.generate.noise.FractalNoise`, `Lib.image.generate.noise.Grain`, `Lib.image.generate.noise.ShardNoise`, `Lib.image.generate.noise.TileableNoise`, `Lib.image.generate.noise.WorleyNoise`, `Lib.image.generate.pattern.FraserGrid`, `Lib.image.generate.pattern.NumberPattern`, `Lib.image.generate.pattern.Raster`, `Lib.image.generate.pattern.Rings`, `Lib.image.generate.pattern.RyojiPattern1`, `Lib.image.generate.pattern.RyojiPattern2`, `Lib.image.generate.pattern.SinForm`, `Lib.image.generate.pattern.ValueRaster`, `Lib.image.generate.pattern.ZollnerPattern`, `Lib.image.transform.Crop`, `Lib.image.transform.ImageFFT`, `Lib.image.transform.MirrorRepeat`, `Lib.image.transform.TransformImage`, `Lib.image.use.Blend`, `Lib.image.use.BlendWithMask`, `Lib.image.use.Combine3Images`, `Lib.image.use.CombineMaterialChannels`, `Lib.image.use.CombineMaterialChannels2`, `Lib.image.use.CustomPixelShader`, `Lib.image.use.DepthBufferAsGrayScale`, `Lib.image.use.Fxaa`, `Lib.image.use.NormalMap`, `Lib.image.use.RenderWithMotionBlur`
- DX11 / app-specific first-pass D nodes: 20 nodes: `Lib.image.fx._._ExecuteBloomPasses`, `Lib.image.fx._._ExecuteFastBlurPasses`, `Lib.image.fx._obsolete.TriangleGridTransition`, `Lib.image.fx.feedback.FluidFeedback`, `Lib.image.fx.feedback.SimpleLiquid`, `Lib.image.fx.feedback.SimpleLiquid2`, `Lib.image.fx.glitch.SortPixelGlitch`, `Lib.image.fx.stylize.ColorPhysarum`, `Lib.image.generate._obsolete._BlobOld`, `Lib.image.generate._obsolete._FractalNoiseOld`, `Lib.image.generate.basic.RenderTarget`, `Lib.image.generate.misc.JumpFloodFill`, `Lib.image.generate.misc.SlidingHistory`, `Lib.image.transform.ImageFFT`, `Lib.image.use.CustomPixelShader`, `Lib.image.use.DepthBufferAsGrayScale`, `Lib.image.use.KeepInTextureArray`, `Lib.image.use.RenderWithMotionBlur`, `Lib.image.use.UseTextureReference`, `Lib.image.use._KeepPreviousFrame_Old1`
- VuoImage + built-in Vuo image node candidates: 77 nodes: `Lib.image.analyze.CompareImages` -> vuo.image.blend + crop/mask (partial), `Lib.image.analyze.GetImageBrightness` -> vuo.image.sample.color / custom reduction (partial), `Lib.image.color.AdjustColors` -> vuo.image.color.adjust (partial), `Lib.image.color.ChannelMixer` -> vuo.image.color.combine/split rgb (partial), `Lib.image.color.ColorGrade` -> vuo.image.color.adjust (partial), `Lib.image.color.ConvertColors` -> vuo.image.color.map / split/combine (partial), `Lib.image.color.ConvertFormat` -> VuoImage color depth/format support (partial), `Lib.image.color.HSE` -> vuo.image.color.adjust (partial), `Lib.image.color.RemapColor` -> vuo.image.color.map (partial), `Lib.image.color.Tint` -> vuo.image.color.adjust / sepia (partial), `Lib.image.fx.blur.Blur` -> vuo.image.blur, `Lib.image.fx.blur.DirectionalBlur` -> vuo.image.blur.directional, `Lib.image.fx.blur.FastBlur` -> vuo.image.blur (partial), `Lib.image.fx.blur.Sharpen` -> vuo.image.sharpen, `Lib.image.fx.distort.BubbleZoom` -> vuo.image.bulge2 (partial), `Lib.image.fx.distort.ChromaticDistortion` -> vuo.image.color.offset.rgb / analogDistortion (partial), `Lib.image.fx.distort.EdgeRepeat` -> vuo.image.tile / wrapMode (partial), `Lib.image.fx.distort.KochKaleidoskope` -> vuo.image.kaleidoscope (partial), `Lib.image.fx.feedback.AdvancedFeedback` -> vuo.image.feedback (partial), `Lib.image.fx.feedback.AdvancedFeedback2` -> vuo.image.feedback (partial), `Lib.image.fx.feedback.AfterGlow` -> vuo.image.feedback (partial), `Lib.image.fx.feedback.AfterGlow2` -> vuo.image.feedback (partial), `Lib.image.fx.glitch.RgbTV` -> vuo.image.analogDistortion / color.offset.rgb (partial), `Lib.image.fx.glitch.SubdivisionStretch` -> vuo.image.scramble / tile (partial), `Lib.image.fx.stylize.ChromaticAbberation` -> vuo.image.color.offset.rgb (partial), `Lib.image.fx.stylize.DetectEdges` -> vuo.image.outline (partial), `Lib.image.fx.stylize.Dither` -> vuo.image.color.palette (partial), `Lib.image.fx.stylize.HoneyCombTiles` -> vuo.image.pixellate details/stainedGlass (partial), `Lib.image.fx.stylize.LightRaysFx` -> vuo.image.streak.radial (partial), `Lib.image.fx.stylize.MosiacTiling` -> vuo.image.pixellate/details (partial), `Lib.image.fx.stylize.Pixelate` -> vuo.image.pixellate, `Lib.image.fx.stylize.ScreenCloseUp` -> vuo.image.pixellate/details (partial), `Lib.image.fx.stylize.StarGlowStreaks` -> vuo.image.streak (partial), `Lib.image.fx.stylize.Steps` -> vuo.image.posterize (partial), `Lib.image.fx.stylize.VoronoiCells` -> vuo.image.stainedGlass (partial), `Lib.image.generate.basic.Blob` -> vuo.image.make.noise / shader (partial), `Lib.image.generate.basic.BoxGradient` -> vuo.image.make.gradient.linear/radial (partial), `Lib.image.generate.basic.CheckerBoard` -> vuo.image.make.checkerboard, `Lib.image.generate.basic.LinearGradient` -> vuo.image.make.gradient.linear, `Lib.image.generate.basic.NGon` -> vuo.image.make.triangle / custom shape (partial), `Lib.image.generate.basic.NGonGradient` -> shader rewrite; partial gradient nodes, `Lib.image.generate.basic.RadialGradient` -> vuo.image.make.gradient.radial, `Lib.image.generate.fractal.MandelbrotFractal` -> vuo.image.make.shadertoy / shader rewrite, `Lib.image.generate.load.ImageSequenceClip` -> vuo.image.fetch.list + event timing (partial), `Lib.image.generate.load.LoadImage` -> vuo.image.fetch, `Lib.image.generate.load.LoadImageFromUrl` -> vuo.image.fetch, `Lib.image.generate.load.LoadSvgAsTexture2D` -> vuo.image.fetch / web render (partial), `Lib.image.generate.noise.FractalNoise` -> vuo.image.make.noise (partial), `Lib.image.generate.noise.Grain` -> vuo.image.filmGrain / make.noise (partial), `Lib.image.generate.noise.ShardNoise` -> vuo.image.make.noise (partial), `Lib.image.generate.noise.TileableNoise` -> vuo.image.make.noise Tile=true (partial), `Lib.image.generate.noise.WorleyNoise` -> vuo.image.make.noise cellular (partial), `Lib.image.generate.pattern.FraserGrid` -> shader rewrite; no direct illusion-pattern node, `Lib.image.generate.pattern.NumberPattern` -> shader rewrite; no direct number-pattern node, `Lib.image.generate.pattern.Raster` -> vuo.image.make.stripe / checkerboard (partial), `Lib.image.generate.pattern.Rings` -> vuo.image.make.gradient.radial (partial), `Lib.image.generate.pattern.RyojiPattern1` -> shader rewrite, `Lib.image.generate.pattern.RyojiPattern2` -> shader rewrite, `Lib.image.generate.pattern.SinForm` -> shader rewrite, `Lib.image.generate.pattern.ValueRaster` -> shader rewrite, `Lib.image.generate.pattern.ZollnerPattern` -> shader rewrite, `Lib.image.transform.Crop` -> vuo.image.crop / crop.pixels, `Lib.image.transform.MakeTileableImage` -> vuo.image.tileable, `Lib.image.transform.MakeTileableImageAdvanced` -> vuo.image.tileable (partial), `Lib.image.transform.MirrorRepeat` -> vuo.image.mirror / tile (partial), `Lib.image.transform.TransformImage` -> vuo.image.resize/rotate/translate/tile (partial), `Lib.image.use.Blend` -> vuo.image.blend, `Lib.image.use.BlendImages` -> no single node; list select + blend, `Lib.image.use.BlendWithMask` -> vuo.image.apply.mask + blend (partial), `Lib.image.use.Combine3Images` -> compose vuo.image.blend, `Lib.image.use.CombineMaterialChannels` -> vuo.image.color.combine/split (partial), `Lib.image.use.CombineMaterialChannels2` -> vuo.image.color.combine/split (partial), `Lib.image.use.FirstValidTexture` -> Vuo selection/list logic + VuoImage, `Lib.image.use.KeepPreviousFrame` -> vuo.image.feedback / hold value (partial), `Lib.image.use.PickTexture` -> Vuo list/select logic + VuoImage, `Lib.image.use.SwapTextures` -> Vuo select/hold logic + VuoImage, `Lib.image.use.UseFallbackTexture` -> Vuo select logic + VuoImage
- Needs ISF/Metal/GLSL or Vuo-specific design: 83 nodes: `Lib.image.analyze.DetectMotion`, `Lib.image.analyze.GetImageBrightness`, `Lib.image.analyze.ImageLevels`, `Lib.image.analyze.OpticalFlow`, `Lib.image.analyze.RemoveStaticBackground`, `Lib.image.analyze.WaveForm`, `Lib.image.color.AdjustColors`, `Lib.image.color.ChannelMixer`, `Lib.image.color.ColorGrade`, `Lib.image.color.ColorGradeDepth`, `Lib.image.color.ConvertColors`, `Lib.image.color.ConvertFormat`, `Lib.image.color.HSE`, `Lib.image.color.KeyColor`, `Lib.image.color.RemapColor`, `Lib.image.color.Tint`, `Lib.image.color.ToneMapping`, `Lib.image.fx.blur.Bloom`, `Lib.image.fx.blur.Blur`, `Lib.image.fx.blur.DirectionalBlur`, `Lib.image.fx.blur.FastBlur`, `Lib.image.fx.blur.Sharpen`, `Lib.image.fx.distort.BubbleZoom`, `Lib.image.fx.distort.ChromaticDistortion`, `Lib.image.fx.distort.Displace`, `Lib.image.fx.distort.DistortAndShade`, `Lib.image.fx.distort.EdgeRepeat`, `Lib.image.fx.distort.FieldToImage`, `Lib.image.fx.distort.KochKaleidoskope`, `Lib.image.fx.distort.PolarCoordinates`, `Lib.image.fx.distort.TimeDisplace`, `Lib.image.fx.feedback.AdvancedFeedback2`, `Lib.image.fx.glitch.GlitchDisplace`, `Lib.image.fx.glitch.RgbTV`, `Lib.image.fx.glitch.SubdivisionStretch`, `Lib.image.fx.stylize.AsciiRender`, `Lib.image.fx.stylize.ChromaticAbberation`, `Lib.image.fx.stylize.DetectEdges`, `Lib.image.fx.stylize.Dither`, `Lib.image.fx.stylize.FakeLight`, `Lib.image.fx.stylize.Glow`, `Lib.image.fx.stylize.HoneyCombTiles`, `Lib.image.fx.stylize.LightRaysFx`, `Lib.image.fx.stylize.MosiacTiling`, `Lib.image.fx.stylize.Pixelate`, `Lib.image.fx.stylize.StarGlowStreaks`, `Lib.image.fx.stylize.Steps`, `Lib.image.fx.stylize.VoronoiCells`, `Lib.image.generate.MunchingSquares2`, `Lib.image.generate.basic.Blob`, `Lib.image.generate.basic.BoxGradient`, `Lib.image.generate.basic.CheckerBoard`, `Lib.image.generate.basic.LinearGradient`, `Lib.image.generate.basic.NGon`, `Lib.image.generate.basic.NGonGradient`, `Lib.image.generate.basic.RadialGradient`, `Lib.image.generate.basic.RoundedRect`, `Lib.image.generate.fractal.MandelbrotFractal`, `Lib.image.generate.misc.Sketch`, `Lib.image.generate.noise.FractalNoise`, `Lib.image.generate.noise.Grain`, `Lib.image.generate.noise.ShardNoise`, `Lib.image.generate.noise.TileableNoise`, `Lib.image.generate.noise.WorleyNoise`, `Lib.image.generate.pattern.FraserGrid`, `Lib.image.generate.pattern.NumberPattern`, `Lib.image.generate.pattern.Raster`, `Lib.image.generate.pattern.Rings`, `Lib.image.generate.pattern.RyojiPattern1`, `Lib.image.generate.pattern.RyojiPattern2`, `Lib.image.generate.pattern.SinForm`, `Lib.image.generate.pattern.ValueRaster`, `Lib.image.generate.pattern.ZollnerPattern`, `Lib.image.transform.Crop`, `Lib.image.transform.MirrorRepeat`, `Lib.image.transform.TransformImage`, `Lib.image.use.Blend`, `Lib.image.use.BlendWithMask`, `Lib.image.use.Combine3Images`, `Lib.image.use.CombineMaterialChannels`, `Lib.image.use.CombineMaterialChannels2`, `Lib.image.use.Fxaa`, `Lib.image.use.NormalMap`

## Compact Rows For All Image Nodes

### Lib.image.analyze

| full_path | purpose | input/output summary | source evidence | grade | reason |
|---|---|---|---|---|---|
| `Lib.image.analyze.CompareImages` | A simple helper to verify an image effect before and after. | in: Center:Vector2={'X': 0.0, 'Y': 0.0}, IntensityRange:float=0.06, Rotate:float=0.0, Texture2d:Texture2D, Texture2d2:Texture2D; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/analyze/CompareImages.cs`; .t3: `Operators/Lib/image/analyze/CompareImages.t3`; docs: `.help/docs/operators/lib/image/analyze/CompareImages.md` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.analyze.DetectMotion` | Uses motion history or difference to detect changes in an image. | in: BackgroundFade:float=0.05, Method:int=0, RemapColor:bool=False, RemapGradient:Gradient={'Gradient': {'Interpolation': 'Linear', 'Steps': [{'Id': '7b08ec6d-2c86-48ea-b546-26a8fc2f1f51', 'NormalizedPosition': 0.0, 'Color': {'R': 0.0, 'G': 0.0, 'B': 0.0, 'A': 1.0}}, {'Id': 'cc5f92aa-e487-4171-b772-494d392b3239', 'NormalizedPosition': 1.0, 'Color': {'R': 1.0, 'G': 1.0, 'B': 1.0, 'A': 1.0}}]}}, VideoFrameIndex:int=0, VideoTexture:Texture2D; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/analyze/DetectMotion.cs`; .t3: `Operators/Lib/image/analyze/DetectMotion.t3`; docs: `.help/docs/operators/lib/image/analyze/DetectMotion.md` | C | image/shader node; likely needs ISF/Metal/GLSL or Vuo-specific renderer design |
| `Lib.image.analyze.GetImageBrightness` | Analyzes the brightness of a 2D input and outputs the brightness as a float value. | in: Texture2d:Texture2D; out: Brightness:float, Update:Command | C#: `Operators/Lib/image/analyze/GetImageBrightness.cs`; .t3: `Operators/Lib/image/analyze/GetImageBrightness.t3`; docs: `.help/docs/operators/lib/image/analyze/GetImageBrightness.md`; shaders: `Operators/Lib/Assets/shaders/img/analyze/cs-GetImageBrightness.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.analyze.ImageLevels` | Visualizes the image brightness distribution of an image. | in: Center:Vector2={'X': 0.25, 'Y': 0.0}, Range:Vector2={'X': 0.0, 'Y': 1.0}, Rotation:float=0.0, ShowOriginal:float=1.0, Texture2d:Texture2D, Width:float=0.2; out: Output:Texture2D | C#: `Operators/Lib/image/analyze/ImageLevels.cs`; .t3: `Operators/Lib/image/analyze/ImageLevels.t3`; docs: `.help/docs/operators/lib/image/analyze/ImageLevels.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/ImageLevels.hlsl` | C | image/shader node; likely needs ISF/Metal/GLSL or Vuo-specific renderer design |
| `Lib.image.analyze.OpticalFlow` | Implements an optical flow effect that generates a motion vector field, which can be used to drive particle effects. | in: Amount:float=1.0, ClampRange:Vector2={'X': 0.0, 'Y': 10.0}, Image:Texture2D, Image2:Texture2D, Lod:float=0.0, OutputMethod:int=0, VisualizationScale:float=0.0, VisualizeResult:bool=False; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/analyze/OpticalFlow.cs`; .t3: `Operators/Lib/image/analyze/OpticalFlow.t3`; docs: `.help/docs/operators/lib/image/analyze/OpticalFlow.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/OpticalFlowKanade.hlsl`, `Operators/Lib/Assets/shaders/img/fx/OpticalFlowSticks.hlsl` | C | image/shader node; likely needs ISF/Metal/GLSL or Vuo-specific renderer design |
| `Lib.image.analyze.RemoveStaticBackground` | Uses statistical filtering to remove static background from an image. | in: BackgroundGateHi:float=3.0, BackgroundGateLo:float=1.5, BrightSuppression:float=1.0, ChromaWeight:float=0.25, DensityRange:Vector2={'X': 0.5, 'Y': 1.0}, EnableChroma:bool=True, IsTraining:bool=False, KeepOriginal:float=0.0, +14 more; out: Output:Texture2D | C#: `Operators/Lib/image/analyze/RemoveStaticBackground.cs`; .t3: `Operators/Lib/image/analyze/RemoveStaticBackground.t3`; docs: `.help/docs/operators/lib/image/analyze/RemoveStaticBackground.md`; shaders: `Operators/Lib/Assets/shaders/img/analyze/remove-static-background-cs1-learning.hlsl`, `Operators/Lib/Assets/shaders/img/analyze/remove-static-background-cs2-refine.hlsl`, `Operators/Lib/Assets/shaders/img/analyze/remove-static-background-cs3-output.hlsl` | C | image/shader node; likely needs ISF/Metal/GLSL or Vuo-specific renderer design |
| `Lib.image.analyze.WaveForm` | Visualizes the color and intensity distribution within an image by overlaying a Waveform and a Vectorscope. | in: ColorIntensity:float=0.15, DimBackground:float=0.5, EffectTexture:Texture2D, EnlargeVectorScopeCenter:float=0.5, Height:float=0.5, Opacity:float=0.14, OverrideSize:Vector.Int2={'X': -1, 'Y': 0}, ShowVectorscope:bool=False, +1 more; out: ImgOutput:Texture2D | C#: `Operators/Lib/image/analyze/WaveForm.cs`; .t3: `Operators/Lib/image/analyze/WaveForm.t3`; docs: `.help/docs/operators/lib/image/analyze/WaveForm.md`; shaders: `Operators/Lib/Assets/shaders/img/analyze/sample-vectorscope-points-cs.hlsl`, `Operators/Lib/Assets/shaders/img/analyze/waveform-cs.hlsl` | C | image/shader node; likely needs ISF/Metal/GLSL or Vuo-specific renderer design |

### Lib.image.color

| full_path | purpose | input/output summary | source evidence | grade | reason |
|---|---|---|---|---|---|
| `Lib.image.color.AdjustColors` | Adjusts various color properties of the incoming image and adds a slight vignette. | in: Background:Vector4={'X': 1e-06, 'Y': 9.9999e-07, 'Z': 9.9999e-07, 'W': 1.0}, Brightness:float=0.0, Colorize:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 0.0}, Contrast:float=0.0, Exposure:float=1.0, Hue:float=0.0, OrangeTeal:float=0.0, PreventClamping:Vector2={'X': 0.0, 'Y': 5.0}, +3 more; out: Output:Texture2D | C#: `Operators/Lib/image/color/AdjustColors.cs`; .t3: `Operators/Lib/image/color/AdjustColors.t3`; docs: `.help/docs/operators/lib/image/color/AdjustColors.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/AdjustColors.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.color.ChannelMixer` | Adjusts the color of an incoming image. | in: Add:Vector4, ClampResult:bool=True, GenerateMipmaps:bool=False, MultiplyA:Vector4, MultiplyB:Vector4={'X': 0.0, 'Y': 0.0, 'Z': 1.0, 'W': 0.0}, MultiplyG:Vector4={'X': 0.0, 'Y': 1.0, 'Z': 0.0, 'W': 0.0}, MultiplyR:Vector4={'X': 1.0, 'Y': 0.0, 'Z': 0.0, 'W': 0.0}, Texture2d:Texture2D; out: Output:Texture2D | C#: `Operators/Lib/image/color/ChannelMixer.cs`; .t3: `Operators/Lib/image/color/ChannelMixer.t3`; docs: `.help/docs/operators/lib/image/color/ChannelMixer.md`; shaders: `Operators/Lib/Assets/shaders/img/MixChannels.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.color.ColorGrade` | Adjusts the color grading of the incoming image for the Gain (highlights), Gamma (mid-tones), and Lift (shadows). | in: ClampResult:bool=False, Gain:Vector4={'X': 0.5, 'Y': 0.5, 'Z': 0.5, 'W': 0.506}, Gamma:Vector4={'X': 0.5, 'Y': 0.5, 'Z': 0.5, 'W': 0.506}, GenerateMipmaps:bool=False, Lift:Vector4={'X': 0.5, 'Y': 0.5, 'Z': 0.5, 'W': 0.25}, PreSaturate:float=1.0, Texture2d:Texture2D, VignetteCenter:Vector2={'X': 0.0, 'Y': 0.0}, +3 more; out: Output:Texture2D | C#: `Operators/Lib/image/color/ColorGrade.cs`; .t3: `Operators/Lib/image/color/ColorGrade.t3`; docs: `.help/docs/operators/lib/image/color/ColorGrade.md`; shaders: `Operators/Lib/Assets/shaders/img/ColorGrade.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.color.ColorGradeDepth` | An advanced color grade that uses a depth buffer-based gradient look for additional effects. | in: CamNearFarClip:Vector2={'X': 0.01, 'Y': 1000.0}, DepthBuffer:Texture2D, Gain:Vector4={'X': 0.5, 'Y': 0.5, 'Z': 0.5, 'W': 0.506}, Gamma:Vector4={'X': 0.5, 'Y': 0.5, 'Z': 0.5, 'W': 0.506}, Gradient:Gradient={'Gradient': {'Interpolation': 'Linear', 'Steps': [{'Id': '4bffebf9-26e4-4c35-8845-dcec4762fd8c', 'NormalizedPosition': 0.0, 'Color': {'R': 0.5019608, 'G': 0.5019608, 'B': 0.5019608, 'A': 0.5}}, {'Id': '3400f8c0-8fc0-4f4e-84c6-b10d6e0c0a00', 'NormalizedPosition': 0.47330096, 'Color': {'R': 0.5019608, 'G': 0.5019608, 'B': 0.5019608, 'A': 0.5}}, {'Id': '72e980ca-f1b0-4bf9-a8d4-c098888c1ed7', 'NormalizedPosition': 1.0, 'Color': {'R': 0.5019608, 'G': 0.5019608, 'B': 0.5019608, 'A': 0.5}}]}}, GradientDepthRange:Vector2={'X': 0.1, 'Y': 100.0}, Lift:Vector4={'X': 0.5, 'Y': 0.5, 'Z': 0.5, 'W': 0.25}, PreSaturate:float=1.0, +5 more; out: Output:Texture2D | C#: `Operators/Lib/image/color/ColorGradeDepth.cs`; .t3: `Operators/Lib/image/color/ColorGradeDepth.t3`; docs: `.help/docs/operators/lib/image/color/ColorGradeDepth.md`; shaders: `Operators/Lib/Assets/shaders/img/adjust/ColorGradeWithDepth.hlsl`, `Operators/Lib/Assets/shaders/img/fx/Default2-vs.hlsl` | C | image/shader node; likely needs ISF/Metal/GLSL or Vuo-specific renderer design |
| `Lib.image.color.ConvertColors` | Converts between different color spaces. | in: GenerateMipmaps:bool=False, Mode:int=0, OutputFormat:Format=R32G32B32A32_Float, Texture2d:Texture2D; out: Output:Texture2D | C#: `Operators/Lib/image/color/ConvertColors.cs`; .t3: `Operators/Lib/image/color/ConvertColors.t3`; docs: `.help/docs/operators/lib/image/color/ConvertColors.md`; shaders: `Operators/Lib/Assets/shaders/img/adjust/img-fx-ConvertColors.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.color.ConvertFormat` | Converts a texture into another format. | in: Enable:bool=True, Format:DXGI.Format=R8G8B8A8_UNorm, GenerateMipMaps:bool=False, ScaleFactor:float=1.0, Texture2d:Texture2D; out: Output:Texture2D | C#: `Operators/Lib/image/color/ConvertFormat.cs`; .t3: `Operators/Lib/image/color/ConvertFormat.t3`; docs: `.help/docs/operators/lib/image/color/ConvertFormat.md`; shaders: `Operators/Lib/Assets/shaders/img/ConvertFormat-cs.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.color.HSE` | Allows to quickly adjust the Hue, Saturation and Exposure of a texture 2D. | in: Exposure:float=1.0, FxTexture:Texture2D, Hue:float=0.0, Saturation:float=1.0, Texture2d:Texture2D; out: Output:Texture2D | C#: `Operators/Lib/image/color/HSE.cs`; .t3: `Operators/Lib/image/color/HSE.t3`; docs: `.help/docs/operators/lib/image/color/HSE.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/HueShift.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.color.KeyColor` | A simple color keyer. | in: Amplify:float=0.0, Background:Vector4={'X': 1e-06, 'Y': 9.9999e-07, 'Z': 9.9999e-07, 'W': 0.0}, Choke:float=0.0, Exposure:float=1.0, Key:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}, Return:int=0, Texture2d:Texture2D, WeightBrightness:float=10.0, +2 more; out: Output:Texture2D | C#: `Operators/Lib/image/color/KeyColor.cs`; .t3: `Operators/Lib/image/color/KeyColor.t3`; docs: `.help/docs/operators/lib/image/color/KeyColor.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/ChromaKey.hlsl` | C | image/shader node; likely needs ISF/Metal/GLSL or Vuo-specific renderer design |
| `Lib.image.color.RemapColor` | Replaces the colors of an image with a gradient based on its brightness. | in: Cycle:float=0.0, DontColorAlpha:bool=False, Exposure:float=1.0, GainAndBias:Vector2={'X': 0.5, 'Y': 0.5}, Gradient:Gradient={'Gradient': {'Interpolation': 'Linear', 'Steps': [{'Id': 'cf89ad61-23e5-46d1-9d13-e2bae35721ba', 'NormalizedPosition': 0.0, 'Color': {'R': 0.0, 'G': 1.2159347e-11, 'B': 1e-06, 'A': 1.0}}, {'Id': '752c4515-16e0-4b31-94b7-47ae200b55d8', 'NormalizedPosition': 1.0, 'Color': {'R': 1.0, 'G': 0.99999, 'B': 1.0, 'A': 1.0}}]}}, GradientSteps:int=256, Image:Texture2D, Mode:int=1, +3 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/color/RemapColor.cs`; .t3: `Operators/Lib/image/color/RemapColor.t3`; docs: `.help/docs/operators/lib/image/color/RemapColor.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/ColorRemap.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.color.Tint` | Tints the bright and dark colors of an image. | in: Amount:float=1.0, ChannelWeights:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 0.0}, Exposure:float=1.0, GainAndBias:Vector2={'X': 0.5, 'Y': 0.5}, MapBlackTo:Vector4={'X': 1e-06, 'Y': 9.9999e-07, 'Z': 9.9999e-07, 'W': 1.0}, MapWhiteTo:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}, Texture2d:Texture2D; out: Output:Texture2D | C#: `Operators/Lib/image/color/Tint.cs`; .t3: `Operators/Lib/image/color/Tint.t3`; docs: `.help/docs/operators/lib/image/color/Tint.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/Tint.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.color.ToneMapping` | Tone mapping is the process of converting high dynamic range (HDR) imagery to a displayable format, adjusting the contrast and brightness to make it visually appealing while preserving details in both highlights and shad... | in: CorrectGamma:bool=False, Exposure:float=1.0, Gamma:float=2.2, Mode:int=4, Texture2d:Texture2D; out: Output:Texture2D | C#: `Operators/Lib/image/color/ToneMapping.cs`; .t3: `Operators/Lib/image/color/ToneMapping.t3`; docs: `.help/docs/operators/lib/image/color/ToneMapping.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/ToneMap.hlsl` | C | image/shader node; likely needs ISF/Metal/GLSL or Vuo-specific renderer design |

### Lib.image.fx._

| full_path | purpose | input/output summary | source evidence | grade | reason |
|---|---|---|---|---|---|
| `Lib.image.fx._._ExecuteBloomPasses` | Unknown. | in: BlurGradient:Gradient, BlurOffset:float, BrightPassPS:PixelShader, Clamp:bool, ColorWeights:Vector4, CopyPS:PixelShader, DownsamplePS:PixelShader, FullscreenVS:VertexShader, +9 more; out: SourceTexture:Texture2D | C#: `Operators/Lib/image/fx/_/_ExecuteBloomPasses.cs`; .t3: `Operators/Lib/image/fx/_/_ExecuteBloomPasses.t3`; docs: `Unknown` | D | internal/obsolete/DX11-heavy first-pass document-only node; DX11/app terms: Direct3D11, ShaderResourceView, PixelShader, VertexShader, SamplerState |
| `Lib.image.fx._._ExecuteFastBlurPasses` | Unknown. | in: DownsampleBlurPS:PixelShader, FullscreenVS:VertexShader, LinearSampler:SamplerState, SourceTextureSrv:ShaderResourceView, Steps:int, UpsampleBlurPS:PixelShader; out: SourceTexture:Texture2D | C#: `Operators/Lib/image/fx/_/_ExecuteFastBlurPasses.cs`; .t3: `Operators/Lib/image/fx/_/_ExecuteFastBlurPasses.t3`; docs: `Unknown` | D | internal/obsolete/DX11-heavy first-pass document-only node; DX11/app terms: ShaderResourceView, PixelShader, VertexShader, SamplerState |

### Lib.image.fx._obsolete

| full_path | purpose | input/output summary | source evidence | grade | reason |
|---|---|---|---|---|---|
| `Lib.image.fx._obsolete.TriangleGridTransition` | Unknown. | in: Background:Vector4={'X': 0.0, 'Y': 0.0, 'Z': 0.0, 'W': 0.0}, Center:Vector2={'X': 0.0, 'Y': 0.0}, Divisions:float=20.0, EffectCenter:Vector2={'X': 0.0, 'Y': 0.0}, EffectFalloff:float=0.25, EffectRotation:float=45.0, Fill:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}, Image:Texture2D, +5 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/fx/_obsolete/TriangleGridTransition.cs`; .t3: `Operators/Lib/image/fx/_obsolete/TriangleGridTransition.t3`; docs: `Unknown`; shaders: `Operators/Lib/Assets/shaders/img/fx/TriangleGridTransition.hlsl` | D | internal/obsolete/DX11-heavy first-pass document-only node; shader/compute dependency |

### Lib.image.fx.blur

| full_path | purpose | input/output summary | source evidence | grade | reason |
|---|---|---|---|---|---|
| `Lib.image.fx.blur.Bloom` | A more versatile and faster version of [Glow]. | in: Blur:float=1.0, Clamp:bool=False, ColorWeights:Vector4={'X': 0.299, 'Y': 0.587, 'Z': 0.114, 'W': 1.0}, GainAndBias:Vector2={'X': 0.5, 'Y': 0.5}, GlowGradient:Gradient={'Gradient': {'Interpolation': 'Linear', 'Steps': [{'Id': '9bb24404-e993-4c5f-bda6-db2cdea0e00b', 'NormalizedPosition': 0.0, 'Color': {'R': 1.0, 'G': 1.0, 'B': 1.0, 'A': 1.0}}]}}, Image:Texture2D, Intensity:float=6.0, MaxLevels:int=10, +1 more; out: Result:Texture2D | C#: `Operators/Lib/image/fx/blur/Bloom.cs`; .t3: `Operators/Lib/image/fx/blur/Bloom.t3`; docs: `.help/docs/operators/lib/image/fx/blur/Bloom.md`; shaders: `Operators/Lib/Assets/shaders/img/blur/Bloom-BrightpassPS.hlsl`, `Operators/Lib/Assets/shaders/img/blur/Bloom-CopyPS.hlsl`, `Operators/Lib/Assets/shaders/img/blur/Bloom-DownsamplePS.hlsl`, +3 | C | image/shader node; likely needs ISF/Metal/GLSL or Vuo-specific renderer design |
| `Lib.image.fx.blur.Blur` | Blurs the incoming image Useful Ops for a PostFX Pipeline: [MotionBlur] [DepthOfField] [ChromaticAbberation] [Glow] [Grain] [Blur] | in: Image:Texture2D, Offset:float=0.0, Opacity:float=1.0, Resolution:Int2={'X': 0, 'Y': 0}, Samples:float=8.0, Size:float=1.0, Wrap:TextureAddressMode=MirrorOnce; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/fx/blur/Blur.cs`; .t3: `Operators/Lib/image/fx/blur/Blur.t3`; docs: `.help/docs/operators/lib/image/fx/blur/Blur.md`; shaders: `Blur.hlsl`, `Operators/Lib/Assets/shaders/img/fx/Blur.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.fx.blur.DirectionalBlur` | Blurs the incoming image along a directional angle. | in: Angle:float=0.0, FxAngleFactor:float=1.0, FxSizeFactor:float=0.0, FxTextures:Texture2D, Image:Texture2D, RefinementPass:bool=False, RefinementSamples:int=6, RefineSizeFactor:float=0.0, +4 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/fx/blur/DirectionalBlur.cs`; .t3: `Operators/Lib/image/fx/blur/DirectionalBlur.t3`; docs: `.help/docs/operators/lib/image/fx/blur/DirectionalBlur.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/DirectionalBlur.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.fx.blur.FastBlur` | Provides better quality and much faster speed that the [Blur] but does only allow radius control. | in: Image:Texture2D, MaxLevels:int=5; out: Result:Texture2D | C#: `Operators/Lib/image/fx/blur/FastBlur.cs`; .t3: `Operators/Lib/image/fx/blur/FastBlur.t3`; docs: `.help/docs/operators/lib/image/fx/blur/FastBlur.md`; shaders: `Operators/Lib/Assets/shaders/img/blur/FastBlur-DownsamplePS.hlsl`, `Operators/Lib/Assets/shaders/img/blur/FastBlur-FullscreenVS.hlsl`, `Operators/Lib/Assets/shaders/img/blur/FastBlur-UpsampleAcculuatePS.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.fx.blur.Sharpen` | Sharpens the incoming image. | in: Clamping:bool=False, Image:Texture2D, SampleRadius:float=1.0, Strength:float=1.0; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/fx/blur/Sharpen.cs`; .t3: `Operators/Lib/image/fx/blur/Sharpen.t3`; docs: `.help/docs/operators/lib/image/fx/blur/Sharpen.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/Sharpen.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |

### Lib.image.fx.distort

| full_path | purpose | input/output summary | source evidence | grade | reason |
|---|---|---|---|---|---|
| `Lib.image.fx.distort.BubbleZoom` | An image effect that enlarges the inner circular region with a smooth edge. | in: Bias:float=0.0, Center:Vector2={'X': 0.0, 'Y': 0.0}, Feather:float=1.0, FeatherGradient:Gradient={'Gradient': {'Interpolation': 'Linear', 'Steps': [{'Id': '57bfcad7-d494-40c2-a1c5-0eb9bdf0cd3d', 'NormalizedPosition': 0.0, 'Color': {'R': 1.0, 'G': 0.99999, 'B': 1.0, 'A': 0.0}}, {'Id': '349fe2d5-1257-4141-8f17-842ee3d33833', 'NormalizedPosition': 1.0, 'Color': {'R': 0.0, 'G': 1.2159347e-11, 'B': 1e-06, 'A': 0.030000024}}]}}, FlipEffect:float=0.0, GainAndBias:Vector2={'X': 0.5, 'Y': 0.5}, Image:Texture2D, Magnify:float=1.25, +2 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/fx/distort/BubbleZoom.cs`; .t3: `Operators/Lib/image/fx/distort/BubbleZoom.t3`; docs: `.help/docs/operators/lib/image/fx/distort/BubbleZoom.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/BubbleZoom.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.fx.distort.ChromaticDistortion` | Simulates an imaging error of optical camera lenses that manifests itself as blurring or discoloration at the outer edges. | in: Center:Vector2={'X': 0.0, 'Y': 0.0}, Colorize:float=0.1, Distort:float=0.1, DistortOffset:float=0.5, SampleCount:int=16, ScaleImage:float=1.0, Size:float=0.05, Texture2d:Texture2D; out: Output:Texture2D | C#: `Operators/Lib/image/fx/distort/ChromaticDistortion.cs`; .t3: `Operators/Lib/image/fx/distort/ChromaticDistortion.t3`; docs: `.help/docs/operators/lib/image/fx/distort/ChromaticDistortion.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/ChromaticDistortion.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.fx.distort.Displace` | *No description yet. | in: DisplaceMap:Texture2D, DisplaceMapOffset:Vector2={'X': 0.0, 'Y': 0.0}, Displacement:float=0.0, DisplacementOffset:float=0.0, DisplaceMode:int=0, GenerateMips:bool=False, Image:Texture2D, RGSS_4xAA:bool=False, +5 more; out: Output:Texture2D | C#: `Operators/Lib/image/fx/distort/Displace.cs`; .t3: `Operators/Lib/image/fx/distort/Displace.t3`; docs: `.help/docs/operators/lib/image/fx/distort/Displace.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/Displace.hlsl` | C | image/shader node; likely needs ISF/Metal/GLSL or Vuo-specific renderer design |
| `Lib.image.fx.distort.DistortAndShade` | Uses two images to distort (create a bevel / emboss effect) that can look like textured glass Also see [Displace] For similar effects see: [Steps] [DetectEdges] [FakeLight] | in: Center:Vector2={'X': 0.5, 'Y': 0.5}, Displacement:float=0.5, ImageA:Texture2D, ImageB:Texture2D, ShadeColor:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}, Shading:float=0.0; out: Output:Texture2D | C#: `Operators/Lib/image/fx/distort/DistortAndShade.cs`; .t3: `Operators/Lib/image/fx/distort/DistortAndShade.t3`; docs: `.help/docs/operators/lib/image/fx/distort/DistortAndShade.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/DistortAndShade.hlsl` | C | image/shader node; likely needs ISF/Metal/GLSL or Vuo-specific renderer design |
| `Lib.image.fx.distort.EdgeRepeat` | Repeats (stretches) pixels of the input image along a line. | in: Background:Vector4={'X': 1.0, 'Y': 0.99999, 'Z': 0.99999, 'W': 0.804}, Center:Vector2={'X': 0.0, 'Y': 0.0}, Fill:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}, Image:Texture2D, LineColor:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}, LineThickness:float=0.0, Resolution:Int2={'X': 0, 'Y': 0}, Rotation:float=45.0, +1 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/fx/distort/EdgeRepeat.cs`; .t3: `Operators/Lib/image/fx/distort/EdgeRepeat.t3`; docs: `.help/docs/operators/lib/image/fx/distort/EdgeRepeat.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/EdgeRepeat.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.fx.distort.FieldToImage` | *No description yet. | in: Center:Vector2={'X': 0.0, 'Y': 0.0}, Field:ShaderGraphNode, GainAndBias:Vector2={'X': 0.5, 'Y': 0.5}, GenerateMips:bool=False, Gradient:Gradient={'Gradient': {'Interpolation': 'Linear', 'Steps': [{'Id': '668b6b32-ed3b-412e-aa23-b28d81a12e0e', 'NormalizedPosition': 0.0, 'Color': {'R': 0.0, 'G': 0.0, 'B': 0.0, 'A': 1.0}}, {'Id': '565e4d7f-b8b0-40df-ba54-294acc8c1e60', 'NormalizedPosition': 1.0, 'Color': {'R': 1.0, 'G': 1.0, 'B': 1.0, 'A': 1.0}}]}}, Mode:int=0, OutputFormat:DXGI.Format=R16G16B16A16_Float, PingPong:bool=False, +6 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/fx/distort/FieldToImage.cs`; .t3: `Operators/Lib/image/fx/distort/FieldToImage.t3`; docs: `.help/docs/operators/lib/image/fx/distort/FieldToImage.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/Default2-vs.hlsl`, `Operators/Lib/Assets/shaders/img/generate/FieldToImageTemplate.hlsl` | C | image/shader node; likely needs ISF/Metal/GLSL or Vuo-specific renderer design |
| `Lib.image.fx.distort.KochKaleidoskope` | This effect is based on Koch snowflake fractal curve. | in: Angle:float=0.0, Center:Vector2={'X': 0.5, 'Y': 0.5}, Image:Texture2D, Offset:Vector2={'X': 0.0, 'Y': 0.0}, Resolution:Vector.Int2={'X': 0, 'Y': 0}, Rotate:float=0.0, Scale:float=3.0, ShadeFolds:float=0.0, +2 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/fx/distort/KochKaleidoskope.cs`; .t3: `Operators/Lib/image/fx/distort/KochKaleidoskope.t3`; docs: `.help/docs/operators/lib/image/fx/distort/KochKaleidoskope.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/KochKaleidoscope.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.fx.distort.PolarCoordinates` | Applies a polar coordinate transformation that converts between circular and rectangular coordinates. | in: Center:Vector2={'X': 0.0, 'Y': 0.0}, Image:Texture2D, Mode:int=0, RadialBias:float=1.0, RadialOffset:float=0.0, Radius:float=1.0, Resolution:Int2={'X': 0, 'Y': 0}, Stretch:Vector2={'X': 1.0, 'Y': 1.0}, +1 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/fx/distort/PolarCoordinates.cs`; .t3: `Operators/Lib/image/fx/distort/PolarCoordinates.t3`; docs: `.help/docs/operators/lib/image/fx/distort/PolarCoordinates.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/PolarCoordinates.hlsl` | C | image/shader node; likely needs ISF/Metal/GLSL or Vuo-specific renderer design |
| `Lib.image.fx.distort.TimeDisplace` | Uses a texture array history buffer to displace in time. | in: DisplaceMap:Texture2D, DisplaceMapOffset:Vector2={'X': 0.0, 'Y': 0.0}, Displacement:float=0.0, DisplacementOffset:float=0.0, DisplaceMode:int=0, GenerateMips:bool=False, Image:Texture2D, RGSS_4xAA:bool=False, +5 more; out: Output:Texture2D | C#: `Operators/Lib/image/fx/distort/TimeDisplace.cs`; .t3: `Operators/Lib/image/fx/distort/TimeDisplace.t3`; docs: `.help/docs/operators/lib/image/fx/distort/TimeDisplace.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/TimeDisplace.hlsl` | C | image/shader node; likely needs ISF/Metal/GLSL or Vuo-specific renderer design |

### Lib.image.fx.feedback

| full_path | purpose | input/output summary | source evidence | grade | reason |
|---|---|---|---|---|---|
| `Lib.image.fx.feedback.AdvancedFeedback` | An advanced version of the [FluidFeedback] effect is much more versatile but harder to control. | in: AmplifyEdges:float=0.141, BlurRadius:float=4.0, Command:Command, Displacement:float=15.0, DisplaceOffset:float=0.0, IsEnabled:bool=True, LimitBrights:float=1.0, Offset:Vector2={'X': 0.0, 'Y': 0.0}, +10 more; out: ColorBuffer:Texture2D | C#: `Operators/Lib/image/fx/feedback/AdvancedFeedback.cs`; .t3: `Operators/Lib/image/fx/feedback/AdvancedFeedback.t3`; docs: `.help/docs/operators/lib/image/fx/feedback/AdvancedFeedback.md` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.fx.feedback.AdvancedFeedback2` | An improved feedback effect based on OKLab space. | in: AddBlurred:float=0.0, AmplifyEdges:float=0.141, BlurRadius:float=4.0, ChromaRange:Vector2={'X': 2.0, 'Y': 0.5}, Command:Command, Displacement:float=20.0, DisplaceOffset:float=0.0, IsEnabled:bool=True, +14 more; out: ColorBuffer:Texture2D | C#: `Operators/Lib/image/fx/feedback/AdvancedFeedback2.cs`; .t3: `Operators/Lib/image/fx/feedback/AdvancedFeedback2.t3`; docs: `.help/docs/operators/lib/image/fx/feedback/AdvancedFeedback2.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/Default2-vs.hlsl`, `Operators/Lib/Assets/shaders/img/fx/FeedbackOkLab.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.fx.feedback.AfterGlow` | Creates an afterglow effect for moving images just like the newer version [AfterGlow2] Other Useful Ops for a PostFX Pipeline: [MotionBlur] [DepthOfField] [ChromaticAberration] [Glow] [Grain] [Blur] All Feedback Ops: [Fl... | in: BlurAmount:float=0.1, Color:Vector4={'X': 0.5928854, 'Y': 0.5928794, 'Z': 0.5928794, 'W': 1.0}, ContrastOffset2:float=-0.76, DecayRate:float=0.015666667, GlowImpact:float=0.7, Image:Texture2D, Resolution:Int2={'X': 0, 'Y': 0}; out: CombinedLayers:Command, Output:Texture2D | C#: `Operators/Lib/image/fx/feedback/AfterGlow.cs`; .t3: `Operators/Lib/image/fx/feedback/AfterGlow.t3`; docs: `.help/docs/operators/lib/image/fx/feedback/AfterGlow.md` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.fx.feedback.AfterGlow2` | Creates an afterglow effect for moving images Other Useful Ops for a PostFX Pipeline: [MotionBlur] [DepthOfField] [ChromaticAberration] [Glow] [Grain] [Blur] All Feedback Ops: [FluidFeedback] [AdvancedFeedback] [Advanced... | in: BlurAmount:float=2.0, Color:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}, DecayRate:float=0.015666667, GlowImpact:float=0.7, Image:Texture2D, OrgColor:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}; out: CombinedLayers:Command, Output:Texture2D | C#: `Operators/Lib/image/fx/feedback/AfterGlow2.cs`; .t3: `Operators/Lib/image/fx/feedback/AfterGlow2.t3`; docs: `.help/docs/operators/lib/image/fx/feedback/AfterGlow2.md` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.fx.feedback.FluidFeedback` | An example of how feedback combined with displacement can lead to a variety of effects. | in: BlurRadius:float=4.0, Command:Command, Displacement:float=15.0, DisplaceOffset:float=0.06, IsEnabled:bool=True, Offset:Vector2={'X': 0.0, 'Y': 0.0}, Reset:bool=False, Rotate:float=0.0, +4 more; out: ColorBuffer:Texture2D | C#: `Operators/Lib/image/fx/feedback/FluidFeedback.cs`; .t3: `Operators/Lib/image/fx/feedback/FluidFeedback.t3`; docs: `.help/docs/operators/lib/image/fx/feedback/FluidFeedback.md` | D | internal/obsolete/DX11-heavy first-pass document-only node; DX11/app terms: RenderTarget, Command |
| `Lib.image.fx.feedback.SimpleLiquid` | A port of 'simple detailed fluid' from 'https://www.shadertoy.com/view/sl3Szs' by Lomateron. | in: ApplyFxTexture:Vector4={'X': 0.0, 'Y': 0.0, 'Z': 0.0, 'W': 1.0}, BorderStrength:float=1.0, Brightness:float=1.0, Damping:float=0.81, FxTexture:Texture2D, Gravity:Vector2={'X': 0.0, 'Y': 0.0}, MassAttraction:float=0.8, ShouldReset:float=0.0, +2 more; out: ColorBuffer:Texture2D | C#: `Operators/Lib/image/fx/feedback/SimpleLiquid.cs`; .t3: `Operators/Lib/image/fx/feedback/SimpleLiquid.t3`; docs: `.help/docs/operators/lib/image/fx/feedback/SimpleLiquid.md`; shaders: `Operators/Lib/Assets/shaders/img/fluid-fx/SimpleLiquid-cs.hlsl` | D | internal/obsolete/DX11-heavy first-pass document-only node; shader/compute dependency |
| `Lib.image.fx.feedback.SimpleLiquid2` | This Simple Detailed Fluid effect is a port of 'https://www.shadertoy.com/view/sl3Szs' by Lomateron. | in: ApplyFxTexture:float=0.0, BorderStrength:float=0.1, FX_B_AddRemoveMass:float=0.5, FX_RG_Velocity:float=0.0, FxTexture:Texture2D, Gravity:Vector2={'X': 0.0, 'Y': 0.01}, Iterations:int=1, MassAttraction:float=1.0, +6 more; out: ColorBuffer:Texture2D | C#: `Operators/Lib/image/fx/feedback/SimpleLiquid2.cs`; .t3: `Operators/Lib/image/fx/feedback/SimpleLiquid2.t3`; docs: `.help/docs/operators/lib/image/fx/feedback/SimpleLiquid2.md`; shaders: `Operators/Lib/Assets/shaders/img/fluid-fx/SimpleLiquid2-cs.hlsl` | D | internal/obsolete/DX11-heavy first-pass document-only node; shader/compute dependency |

### Lib.image.fx.glitch

| full_path | purpose | input/output summary | source evidence | grade | reason |
|---|---|---|---|---|---|
| `Lib.image.fx.glitch.GlitchDisplace` | Takes the incoming image and applies an image effect that glitches and displaces parts of the image to mimic lossy signals, broken video files, codec glitches etc. | in: Amount:float=1.0, Colorize:Vector4={'X': 1.0, 'Y': 0.0, 'Z': 0.8917217, 'W': 1.0}, ColorRatio:float=0.1, Columns:int=25, Image:Texture2D, Mode:int=3, Offset:Vector2={'X': 0.5, 'Y': 0.0}, OverridePoints:BufferWithViews, +8 more; out: Output2:Texture2D | C#: `Operators/Lib/image/fx/glitch/GlitchDisplace.cs`; .t3: `Operators/Lib/image/fx/glitch/GlitchDisplace.t3`; docs: `.help/docs/operators/lib/image/fx/glitch/GlitchDisplace.md`; shaders: `Operators/Lib/Assets/shaders/points/draw/GlitchDisplace.hlsl` | C | image/shader node; likely needs ISF/Metal/GLSL or Vuo-specific renderer design |
| `Lib.image.fx.glitch.RgbTV` | Creates a vintage TV-glitch effect. | in: BlackLevel:float=-0.100000024, BlurImage:float=0.0, Buldge:float=0.15, Gaps:float=0.03, GlitchAmount:float=1.0, GlitchDistort:float=1.0, GlitchFlicker:float=0.0, GlitchTimeOverride:float=0.0, +18 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/fx/glitch/RgbTV.cs`; .t3: `Operators/Lib/image/fx/glitch/RgbTV.t3`; docs: `.help/docs/operators/lib/image/fx/glitch/RgbTV.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/Default2-vs.hlsl`, `Operators/Lib/Assets/shaders/img/fx/RgbTV.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.fx.glitch.SortPixelGlitch` | An interesting image glitch effect. | in: AddGrain:float=0.0, BackgroundColor:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}, Extend:float=0.0, FadeStreaks:float=0.0, GradientBias:float=0.75, LumaBias:float=0.0, MaxSteps:float=2000.0, Offset:float=0.0, +7 more; out: Output:Texture2D | C#: `Operators/Lib/image/fx/glitch/SortPixelGlitch.cs`; .t3: `Operators/Lib/image/fx/glitch/SortPixelGlitch.t3`; docs: `.help/docs/operators/lib/image/fx/glitch/SortPixelGlitch.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/Default2-vs.hlsl`, `Operators/Lib/Assets/shaders/img/fx/PixelSortGlitch-convert-buffer-to-texture2d-ps.hlsl`, `Operators/Lib/Assets/shaders/img/fx/SortPixelsGlitch-cs.hlsl` | D | internal/obsolete/DX11-heavy first-pass document-only node; DX11/app terms: RenderTarget; shader/compute dependency |
| `Lib.image.fx.glitch.SubdivisionStretch` | A powerful image generation effect that splits an image into smaller and smaller fragments. | in: ColorMode:int, DirectionBias:float=0.0, Feather:float=0.0005, GapColor:Vector4={'X': 0.0, 'Y': 0.0, 'Z': 0.0, 'W': 1.0}, GapWidth:float=0.001, Gradient:Gradient={'Gradient': {'Interpolation': 'Linear', 'Steps': [{'Id': 'd0af64cd-ac5b-42f2-9bf4-3fe8be6abd69', 'NormalizedPosition': 0.0, 'Color': {'R': 1.0, 'G': 1.0, 'B': 1.0, 'A': 1.0}}, {'Id': 'eed8d65c-2663-440a-98a1-0dc2bfdd9f86', 'NormalizedPosition': 0.94606256, 'Color': {'R': 0.0, 'G': 0.0, 'B': 0.0, 'A': 1.0}}]}}, GradientMode:int=0, Image:Texture2D, +11 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/fx/glitch/SubdivisionStretch.cs`; .t3: `Operators/Lib/image/fx/glitch/SubdivisionStretch.t3`; docs: `.help/docs/operators/lib/image/fx/glitch/SubdivisionStretch.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/StretchSubdivide.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |

### Lib.image.fx.stylize

| full_path | purpose | input/output summary | source evidence | grade | reason |
|---|---|---|---|---|---|
| `Lib.image.fx.stylize.AsciiRender` | Draws the incoming image as shaded ASCII characters, similar to 'The Matrix'. | in: Background:Vector4={'X': 1e-06, 'Y': 9.9999e-07, 'Z': 9.9999e-07, 'W': 1.0}, Fill:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}, FilterCharacters:string=, FontCharSize:Vector.Int2={'X': 6, 'Y': 6}, FontFilePath:string=Lib:images/font-6x6px.png, GainAndBias:Vector2={'X': 0.5, 'Y': 0.5}, GenerateMips:bool=False, ImageA:Texture2D, +5 more; out: Output:Texture2D | C#: `Operators/Lib/image/fx/stylize/AsciiRender.cs`; .t3: `Operators/Lib/image/fx/stylize/AsciiRender.t3`; docs: `.help/docs/operators/lib/image/fx/stylize/AsciiRender.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/AsciiRender.hlsl`, `Operators/Lib/Assets/shaders/img/fx/Default2-vs.hlsl` | C | image/shader node; likely needs ISF/Metal/GLSL or Vuo-specific renderer design |
| `Lib.image.fx.stylize.ChromaticAbberation` | Simulates an imaging error of optical camera lenses that manifests itself as blurring or discoloration at the outer edges. | in: Distort:float=-0.1, Image:Texture2D, SampleCount:int=3, Size:float=5.0, Strength:float=1.0; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/fx/stylize/ChromaticAbberation.cs`; .t3: `Operators/Lib/image/fx/stylize/ChromaticAbberation.t3`; docs: `.help/docs/operators/lib/image/fx/stylize/ChromaticAbberation.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/ChromaticAbberation.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.fx.stylize.ColorPhysarum` | Work in progress for a new edge detect like effect. | in: AgentCount:int=0, AngleLockFactor:float=0.0, AngleLockSteps:float=6.0, BaseMovement:float=0.0, BaseRotation:float=0.0, ColorPickUp:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}, ComfortZones:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}, ComputeSteps:int=1, +13 more; out: ImgOutput:Texture2D | C#: `Operators/Lib/image/fx/stylize/ColorPhysarum.cs`; .t3: `Operators/Lib/image/fx/stylize/ColorPhysarum.t3`; docs: `.help/docs/operators/lib/image/fx/stylize/ColorPhysarum.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/stylize/color-physarum-cs.hlsl`, `Operators/Lib/Assets/shaders/img/fx/stylize/color-physarum-diffuse-cs.hlsl` | D | internal/obsolete/DX11-heavy first-pass document-only node; DX11/app terms: RenderTarget; shader/compute dependency |
| `Lib.image.fx.stylize.DetectEdges` | Detects edges in the incoming image. | in: Color:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}, Contrast:float=0.0, Image:Texture2D, MixOriginal:float=0.0, OutputAsTransparent:bool=False, SampleRadius:float=1.0, Strength:float=1.0; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/fx/stylize/DetectEdges.cs`; .t3: `Operators/Lib/image/fx/stylize/DetectEdges.t3`; docs: `.help/docs/operators/lib/image/fx/stylize/DetectEdges.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/DetectEdges.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.fx.stylize.Dither` | Applies Floyd-Steinberg dithering to an image to convert it to black and white colors. | in: BlendMethod:int=0, GainAndBias:Vector2={'X': 0.5, 'Y': 0.5}, GrayScaleWeights:Vector4={'X': 0.2126, 'Y': 0.7152, 'Z': 0.0722, 'W': 0.0}, HighlightColor:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}, Image:Texture2D, Method:int=0, Offset:Vector2={'X': 0.0, 'Y': 0.0}, Scale:float=4.0, +1 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/fx/stylize/Dither.cs`; .t3: `Operators/Lib/image/fx/stylize/Dither.t3`; docs: `.help/docs/operators/lib/image/fx/stylize/Dither.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/Dither.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.fx.stylize.FakeLight` | Applies fake shading to the image. | in: Amount:float=1.0, BlurRadius:float=1.0, Direction:Vector2={'X': 0.003, 'Y': 0.015999999}, HeightMap:Texture2D, HighlightColor:Vector4={'X': 2.0, 'Y': 1.7309682, 'Z': 0.99563324, 'W': 1.0}, LightMap:Texture2D, MidColor:Vector4={'X': 0.50144064, 'Y': 0.44633016, 'Z': 0.6157205, 'W': 0.0}, Resolution:Int2={'X': 0, 'Y': 0}, +4 more; out: Output:Texture2D | C#: `Operators/Lib/image/fx/stylize/FakeLight.cs`; .t3: `Operators/Lib/image/fx/stylize/FakeLight.t3`; docs: `.help/docs/operators/lib/image/fx/stylize/FakeLight.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/FakeLight.hlsl` | C | image/shader node; likely needs ISF/Metal/GLSL or Vuo-specific renderer design |
| `Lib.image.fx.stylize.Glow` | Adds a glow effect to the incoming image. | in: AmplifyCore:float=0.0, BlendMode:int=1, Color:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}, Intensity:float=0.5, Radius:float=0.5, Samples:float=12.0, Texture:Texture2D, Threshold:float=0.0; out: ImgOutput:Texture2D, Output:Command | C#: `Operators/Lib/image/fx/stylize/Glow.cs`; .t3: `Operators/Lib/image/fx/stylize/Glow.t3`; docs: `.help/docs/operators/lib/image/fx/stylize/Glow.md` | C | image/shader node; likely needs ISF/Metal/GLSL or Vuo-specific renderer design |
| `Lib.image.fx.stylize.HoneyCombTiles` | Creates a hexagonal pattern based on an incoming image. | in: Background:Vector4={'X': 1.0, 'Y': 0.99999, 'Z': 0.99999, 'W': 0.804}, Center:Vector2={'X': 0.0, 'Y': 0.0}, Divisions:float=20.0, Fill:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}, Image:Texture2D, LineThickness:float=0.0, MixOriginal:float=0.0, Resolution:Int2={'X': 0, 'Y': 0}, +1 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/fx/stylize/HoneyCombTiles.cs`; .t3: `Operators/Lib/image/fx/stylize/HoneyCombTiles.t3`; docs: `.help/docs/operators/lib/image/fx/stylize/HoneyCombTiles.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/HexGridDisplace.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.fx.stylize.LightRaysFx` | Based on "Simpler God Rays" by akufishi Pimped by Newemka | in: Amount:float=5.0, ApplyFXToBackground:float=1.0, Decay:float=0.9, Direction:Vector2={'X': 0.0, 'Y': 0.0}, Image:Texture2D, Length:float=0.4, RayColor:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}, Samples:int=100, +1 more; out: Output:Texture2D | C#: `Operators/Lib/image/fx/stylize/LightRaysFx.cs`; .t3: `Operators/Lib/image/fx/stylize/LightRaysFx.t3`; docs: `.help/docs/operators/lib/image/fx/stylize/LightRaysFx.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/Default2-vs.hlsl`, `Operators/Lib/Assets/shaders/img/fx/LightRayFx.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.fx.stylize.MosiacTiling` | Subdivides the incoming image into recursive tiles. | in: Center:Vector2={'X': 0.0, 'Y': 0.0}, Feather:float=0.0, FxTextures:Texture2D, GapColor:Vector4={'X': 0.0, 'Y': 0.0, 'Z': 0.0, 'W': 1.0}, Image:Texture2D, MaxSubdivisions:int=4, MixOriginal:float=1.0, Padding:float=0.0, +4 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/fx/stylize/MosiacTiling.cs`; .t3: `Operators/Lib/image/fx/stylize/MosiacTiling.t3`; docs: `.help/docs/operators/lib/image/fx/stylize/MosiacTiling.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/MosiacTiling.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.fx.stylize.Pixelate` | TilesAmount parameter works only if Divisor = 0. | in: Color:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}, Divisor:int=0, Image:Texture2D, Shape:Texture2D, TileAmount:Int2={'X': 160, 'Y': 90}; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/fx/stylize/Pixelate.cs`; .t3: `Operators/Lib/image/fx/stylize/Pixelate.t3`; docs: `.help/docs/operators/lib/image/fx/stylize/Pixelate.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/Pixelate.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.fx.stylize.ScreenCloseUp` | Uses the incoming 2D image and puts it on a virtual LCD/screen in a 3D scene that looks as if it is being filmed by a real camera (including depth of field etc) Works great with [PlayVideo] or any [RenderTarget] or [Scre... | in: BackgroundColor:Vector4={'X': 0.0, 'Y': 0.0, 'Z': 0.0, 'W': 1.0}, DepthOfField:float=0.3, FogStrength:float=0.2, Glossy:float=0.2, Target:Vector2={'X': 0.0, 'Y': 0.0}, Texture2d:Texture2D, Tilt:Vector2={'X': 0.0, 'Y': 0.0}, Zoom:float=0.0; out: Output:Texture2D | C#: `Operators/Lib/image/fx/stylize/ScreenCloseUp.cs`; .t3: `Operators/Lib/image/fx/stylize/ScreenCloseUp.t3`; docs: `.help/docs/operators/lib/image/fx/stylize/ScreenCloseUp.md` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.fx.stylize.StarGlowStreaks` | Renders streaks onto the incoming image. | in: BlendMode:int=8, Brightness:float=3.0, Color:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}, GlareModes:int=0, Image:Texture2D, OriginalColor:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}, Quality:int=0, Range:float=0.1, +1 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/fx/stylize/StarGlowStreaks.cs`; .t3: `Operators/Lib/image/fx/stylize/StarGlowStreaks.t3`; docs: `.help/docs/operators/lib/image/fx/stylize/StarGlowStreaks.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/StarGlowStreaks.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.fx.stylize.Steps` | A versatile effect that uses the value of the input image to draw 'steps'. | in: Bias:float=0.5, Count:float=4.0, Edge:Gradient={'Gradient': {'Interpolation': 'Linear', 'Steps': [{'Id': '90afb6ee-c945-4378-8028-48b4c1fb8913', 'NormalizedPosition': 0.8745387, 'Color': {'R': 0.12546128, 'G': 0.12546003, 'B': 0.12546216, 'A': 0.0}}, {'Id': '733aed87-0739-4ebb-9bd0-14226f26a505', 'NormalizedPosition': 1.0, 'Color': {'R': 0.0, 'G': 1.168251e-11, 'B': 1e-06, 'A': 0.16981131}}]}}, Highlight:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 0.0}, HighlightIndex:int=0, Image:Texture2D, Offset:float=0.0, Ramp:Gradient={'Gradient': {'Interpolation': 'Linear', 'Steps': [{'Id': '940ecad4-01c9-4c8b-9e81-515649a7864d', 'NormalizedPosition': 0.0, 'Color': {'R': 9.9999e-07, 'G': 9.9999e-07, 'B': 1e-06, 'A': 1.0}}, {'Id': '388e8fca-fc13-4b79-bba5-d8c7fdebbc86', 'NormalizedPosition': 1.0, 'Color': {'R': 1.0, 'G': 0.99999, 'B': 1.0, 'A': 1.0}}]}}, +4 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/fx/stylize/Steps.cs`; .t3: `Operators/Lib/image/fx/stylize/Steps.t3`; docs: `.help/docs/operators/lib/image/fx/stylize/Steps.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/Default2-vs.hlsl`, `Operators/Lib/Assets/shaders/img/fx/Steps.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.fx.stylize.VoronoiCells` | Creates a Voronoi cell pattern based on an incoming image. | in: Background:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}, EdgeColor:Vector4={'X': 0.0, 'Y': 0.0, 'Z': 0.0, 'W': 1.0}, EdgeWidth:float=0.68, Image:Texture2D, Phase:float=0.0, Resolution:Int2={'X': 0, 'Y': 0}, Scale:float=10.0; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/fx/stylize/VoronoiCells.cs`; .t3: `Operators/Lib/image/fx/stylize/VoronoiCells.t3`; docs: `.help/docs/operators/lib/image/fx/stylize/VoronoiCells.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/VoronoiCells.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |

### Lib.image.generate

| full_path | purpose | input/output summary | source evidence | grade | reason |
|---|---|---|---|---|---|
| `Lib.image.generate.MunchingSquares2` | *No description yet. | in: BlendMethod:int=0, GainAndBias:Vector2={'X': 0.5, 'Y': 0.5}, GrayScaleWeights:Vector4={'X': 0.2126, 'Y': 0.7152, 'Z': 0.0722, 'W': 0.0}, HighlightColor:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}, Image:Texture2D, IterationFx:float=0.0, Iterations:int=10, Method:int=0, +4 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/generate/MunchingSquares2.cs`; .t3: `Operators/Lib/image/generate/MunchingSquares2.t3`; docs: `.help/docs/operators/lib/image/generate/MunchingSquares2.md`; shaders: `Operators/Lib/Assets/shaders/img/generate/MunchingSquares.hlsl` | C | image/shader node; likely needs ISF/Metal/GLSL or Vuo-specific renderer design |

### Lib.image.generate._obsolete

| full_path | purpose | input/output summary | source evidence | grade | reason |
|---|---|---|---|---|---|
| `Lib.image.generate._obsolete._BlobOld` | Unknown. | in: Background:Vector4={'X': 0.0, 'Y': 0.0, 'Z': 0.0, 'W': 0.0}, BlendMode:int=0, Feather:float=1.0, FeatherBias:float=0.0, Fill:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}, GenerateMips:bool=False, Image:Texture2D, Position:Vector2={'X': 0.0, 'Y': 0.0}, +4 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/generate/_obsolete/_BlobOld.cs`; .t3: `Operators/Lib/image/generate/_obsolete/_BlobOld.t3`; docs: `Unknown`; shaders: `Operators/Lib/Assets/shaders/img/generate/BlobOld.hlsl` | D | internal/obsolete/DX11-heavy first-pass document-only node; shader/compute dependency |
| `Lib.image.generate._obsolete._FractalNoiseOld` | Unknown. | in: Bias:float=0.0, ColorA:Vector4={'X': 1e-06, 'Y': 9.9999e-07, 'Z': 9.9999e-07, 'W': 1.0}, ColorB:Vector4={'X': 1.0, 'Y': 0.99999, 'Z': 0.99999, 'W': 1.0}, GenerateMips:bool=False, Iterations:int=2, Method:int=0, Offset:Vector2={'X': 0.0, 'Y': 0.0}, Phase:float=5.0, +4 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/generate/_obsolete/_FractalNoiseOld.cs`; .t3: `Operators/Lib/image/generate/_obsolete/_FractalNoiseOld.t3`; docs: `Unknown`; shaders: `Operators/Lib/Assets/shaders/img/generate/FractalNoiseOld.hlsl` | D | internal/obsolete/DX11-heavy first-pass document-only node; shader/compute dependency |

### Lib.image.generate.basic

| full_path | purpose | input/output summary | source evidence | grade | reason |
|---|---|---|---|---|---|
| `Lib.image.generate.basic.Blob` | Generates ellipses, circles, blobs, vignettes, and similar shapes. | in: Background:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 0.0}, BlendMode:int=0, Color:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}, Feather:float=1.0, FeatherBias:float=0.0, GenerateMips:bool=False, Image:Texture2D, Position:Vector2={'X': 0.0, 'Y': 0.0}, +5 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/generate/basic/Blob.cs`; .t3: `Operators/Lib/image/generate/basic/Blob.t3`; docs: `.help/docs/operators/lib/image/generate/basic/Blob.md`; shaders: `Operators/Lib/Assets/shaders/img/generate/Blob.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.generate.basic.BoxGradient` | A box gradient using the signed distance field "Box - Exact" described in the "2D distance functions" article on Inigo Quilez's website. | in: BlendMode:int=0, Center:Vector2={'X': 0.0, 'Y': 0.0}, CornersRadius:Vector4={'X': 0.0, 'Y': 0.0, 'Z': 0.0, 'W': 0.0}, GainAndBias:Vector2={'X': 0.5, 'Y': 0.5}, Gradient:Gradient={'Gradient': {'Interpolation': 'Linear', 'Steps': [{'Id': '8e4f8a75-f14c-47f7-8328-883bfb1b3cfa', 'NormalizedPosition': 0.0, 'Color': {'R': 1.0, 'G': 0.99999, 'B': 1.0, 'A': 1.0}}, {'Id': 'd2a66e2d-8d83-4e2a-9e6c-838669725ac9', 'NormalizedPosition': 0.5, 'Color': {'R': 0.0, 'G': 1.2159347e-11, 'B': 1e-06, 'A': 1.0}}]}}, GradientWidth:float=1.0, Image:Texture2D, Offset:float=0.0, +6 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/generate/basic/BoxGradient.cs`; .t3: `Operators/Lib/image/generate/basic/BoxGradient.t3`; docs: `.help/docs/operators/lib/image/generate/basic/BoxGradient.md`; shaders: `Operators/Lib/Assets/shaders/img/generate/BoxGradient.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.generate.basic.CheckerBoard` | Generates a modular chessboard pattern Other interesting patterns can be generated with [SinForm] [ZollnerPattern] [FraserGrid] [Raster] [CheckerBoard] [RyojiPattern2] [RyojiPattern1] | in: ColorA:Vector4={'X': 0.20212764, 'Y': 0.20212561, 'Z': 0.20212561, 'W': 1.0}, ColorB:Vector4={'X': 0.12056738, 'Y': 0.120566174, 'Z': 0.120566174, 'W': 1.0}, GenerateMips:bool=False, Offset:Vector2={'X': 0.0, 'Y': 0.0}, Resolution:Int2={'X': 0, 'Y': 0}, Scale:float=1.0, Stretch:Vector2={'X': 1.0, 'Y': 1.0}, UseAspectRatio:bool=True; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/generate/basic/CheckerBoard.cs`; .t3: `Operators/Lib/image/generate/basic/CheckerBoard.t3`; docs: `.help/docs/operators/lib/image/generate/basic/CheckerBoard.md`; shaders: `Operators/Lib/Assets/shaders/img/generate/CheckerBoard.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.generate.basic.LinearGradient` | Renders a linear color ramp defined by a gradient. | in: BlendMode:int=0, Center:Vector2={'X': 0.0, 'Y': 0.0}, GainAndBias:Vector2={'X': 0.5, 'Y': 0.5}, GenerateMips:bool=False, Gradient:Gradient={'Gradient': {'Interpolation': 'Linear', 'Steps': [{'Id': '034c8b5b-5c92-439f-b5a6-28e721df9492', 'NormalizedPosition': 0.0, 'Color': {'R': 0.0, 'G': 1.2159347e-11, 'B': 1e-06, 'A': 1.0}}, {'Id': '0c357289-d7c4-4a05-86ea-4cc7debde848', 'NormalizedPosition': 1.0, 'Color': {'R': 1.0, 'G': 0.99999, 'B': 1.0, 'A': 1.0}}]}}, Image:Texture2D, Offset:float=0.0, OffsetMode:int=0, +7 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/generate/basic/LinearGradient.cs`; .t3: `Operators/Lib/image/generate/basic/LinearGradient.t3`; docs: `.help/docs/operators/lib/image/generate/basic/LinearGradient.md`; shaders: `Operators/Lib/Assets/shaders/img/generate/LinearGradient.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.generate.basic.NGon` | Renders a polygon shape similar to [Blob] or [RoundedRect]. | in: Background:Vector4={'X': 0.0, 'Y': 0.0, 'Z': 0.0, 'W': 0.0}, Blades:float=0.0, BlendMode:int=0, Curvature:float=0.0, Feather:float=0.05, FeatherBias:float=0.0, Fill:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}, Image:Texture2D, +6 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/generate/basic/NGon.cs`; .t3: `Operators/Lib/image/generate/basic/NGon.t3`; docs: `.help/docs/operators/lib/image/generate/basic/NGon.md`; shaders: `Operators/Lib/Assets/shaders/img/generate/NGon.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.generate.basic.NGonGradient` | Renders a polygon shape similar to [NGon], [Blob] or [RoundedRect]. | in: BiasAndGain:Vector2={'X': 0.5, 'Y': 0.5}, Blades:float=0.0, BlendMode:int=0, Curvature:float=0.0, Gradient:Gradient={'Gradient': {'Interpolation': 'Linear', 'Steps': [{'Id': 'dfa71896-0de8-47d7-9f2d-e7e28ab7bedb', 'NormalizedPosition': 0.0, 'Color': {'R': 1.0, 'G': 1.0, 'B': 1.0, 'A': 1.0}}, {'Id': 'f6f198f5-be17-4715-88d2-550585672e4b', 'NormalizedPosition': 1.0, 'Color': {'R': 0.0, 'G': 0.0, 'B': 0.0, 'A': 1.0}}]}}, Image:Texture2D, Offset:float=0.0, PingPong:bool=False, +8 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/generate/basic/NGonGradient.cs`; .t3: `Operators/Lib/image/generate/basic/NGonGradient.t3`; docs: `.help/docs/operators/lib/image/generate/basic/NGonGradient.md`; shaders: `Operators/Lib/Assets/shaders/img/generate/NGonGradient.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.generate.basic.RadialGradient` | Generates an image with a radial gradient. | in: BiasAndGain:Vector2={'X': 0.5, 'Y': 0.5}, BlendMode:int=0, Center:Vector2={'X': 0.0, 'Y': 0.0}, GenerateMipMaps:bool=False, Gradient:Gradient={'Gradient': {'Interpolation': 'Linear', 'Steps': [{'Id': '56e4b8b6-1c97-412b-9d21-7aa12d0ba50c', 'NormalizedPosition': 0.0, 'Color': {'R': 1.0, 'G': 0.99999, 'B': 1.0, 'A': 1.0}}, {'Id': '469c9380-cdcf-4d49-99c7-8d261939c749', 'NormalizedPosition': 1.0, 'Color': {'R': 0.0, 'G': 1.2159347e-11, 'B': 1e-06, 'A': 1.0}}]}}, Image:Texture2D, Noise:float=0.0, Offset:float=0.0, +7 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/generate/basic/RadialGradient.cs`; .t3: `Operators/Lib/image/generate/basic/RadialGradient.t3`; docs: `.help/docs/operators/lib/image/generate/basic/RadialGradient.md`; shaders: `Operators/Lib/Assets/shaders/img/generate/RadialGradient.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.generate.basic.RenderTarget` | The primary method of rendering 3D data into a 2D image texture. | in: Clear:bool=True, ClearColor:Vector4={'X': 0.0, 'Y': 0.0, 'Z': 0.0, 'W': 1.0}, Command:Command, EnableUpdate:bool, GenerateMips:bool=False, Multisampling:int, Resolution:Int2={'X': 0, 'Y': 0}, TextureFormat:Format=R16G16B16A16_Float, +3 more; out: ColorBuffer:Texture2D, DepthBuffer:Texture2D, NormalBuffer:Texture2D | C#: `Operators/Lib/image/generate/basic/RenderTarget.cs`; .t3: `Operators/Lib/image/generate/basic/RenderTarget.t3`; docs: `.help/docs/operators/lib/image/generate/basic/RenderTarget.md`; shaders: `Operators/Lib/Assets/shaders/dx11/resolve-multisampled-depth-buffer-cs.hlsl` | D | internal/obsolete/DX11-heavy first-pass document-only node; DX11/app terms: TextureReference, RenderTarget, PixelShader, Command; shader/compute dependency |
| `Lib.image.generate.basic.RoundedRect` | Generates a rounded rectangle. | in: Background:Vector4={'X': 0.0, 'Y': 0.0, 'Z': 0.0, 'W': 0.0}, Color:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}, Feather:float=0.0, FeatherBias:float=-0.001, GenerateMips:bool=False, Image:Texture2D, Position:Vector2={'X': 0.0, 'Y': 0.0}, Resolution:Int2={'X': 0, 'Y': 0}, +6 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/generate/basic/RoundedRect.cs`; .t3: `Operators/Lib/image/generate/basic/RoundedRect.t3`; docs: `.help/docs/operators/lib/image/generate/basic/RoundedRect.md`; shaders: `Operators/Lib/Assets/shaders/img/generate/RoundedRect.hlsl` | C | image/shader node; likely needs ISF/Metal/GLSL or Vuo-specific renderer design |

### Lib.image.generate.fractal

| full_path | purpose | input/output summary | source evidence | grade | reason |
|---|---|---|---|---|---|
| `Lib.image.generate.fractal.MandelbrotFractal` | *No description yet. | in: ColorScale:float=10.0, Gradient:Gradient={'Gradient': {'Interpolation': 'Linear', 'Steps': [{'Id': '1a4482cd-7deb-4fa2-af9e-7f4d5daf8032', 'NormalizedPosition': 0.0, 'Color': {'R': 0.0, 'G': 0.0, 'B': 0.0, 'A': 1.0}}, {'Id': '33f48194-c763-4094-9b4a-32587a9765f6', 'NormalizedPosition': 1.0, 'Color': {'R': 1.0, 'G': 1.0, 'B': 1.0, 'A': 1.0}}]}}, Offset:Vector2={'X': 0.251, 'Y': 0.0}, Phase:float=0.0, Scale:float=-0.5; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/generate/fractal/MandelbrotFractal.cs`; .t3: `Operators/Lib/image/generate/fractal/MandelbrotFractal.t3`; docs: `.help/docs/operators/lib/image/generate/fractal/MandelbrotFractal.md`; shaders: `Operators/Lib/Assets/shaders/img/generate/MandelbrotFractal.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |

### Lib.image.generate.load

| full_path | purpose | input/output summary | source evidence | grade | reason |
|---|---|---|---|---|---|
| `Lib.image.generate.load.ImageSequenceClip` | Loads an image sequence from a folder and adds it as a time clip bar within the DopeView of the Timeline, similar to how video editing apps show clips. | in: Color:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}, FilePath:string, FrameRate:float=24.0, TriggerUpdate:bool=False; out: Output:Command | C#: `Operators/Lib/image/generate/load/ImageSequenceClip.cs`; .t3: `Operators/Lib/image/generate/load/ImageSequenceClip.t3`; docs: `.help/docs/operators/lib/image/generate/load/ImageSequenceClip.md` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.generate.load.LoadImage` | Loads an image file as a Texture2D. | in: CacheResources:bool, Path:string; out: Texture:Texture2D | C#: `Operators/Lib/image/generate/load/LoadImage.cs`; .t3: `Operators/Lib/image/generate/load/LoadImage.t3`; docs: `.help/docs/operators/lib/image/generate/load/LoadImage.md` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.generate.load.LoadImageFromUrl` | Loads an image file with the specified URL. | in: TriggerUpdate:bool, Url:string=Lib:https://cataas.com/cat; out: ShaderResourceView:ShaderResourceView, Texture:Texture2D | C#: `Operators/Lib/image/generate/load/LoadImageFromUrl.cs`; .t3: `Operators/Lib/image/generate/load/LoadImageFromUrl.t3`; docs: `.help/docs/operators/lib/image/generate/load/LoadImageFromUrl.md` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.generate.load.LoadSvgAsTexture2D` | Loads a SVG file as a Texture2D. | in: Path:string=Examples:test-footage/svg/t3logo.svg, Resolution:Int2, Scale:float, SelectLayerRange:Int2, SplitToLayers:bool, UseViewBox:bool; out: Texture:Texture2D | C#: `Operators/Lib/image/generate/load/LoadSvgAsTexture2D.cs`; .t3: `Operators/Lib/image/generate/load/LoadSvgAsTexture2D.t3`; docs: `.help/docs/operators/lib/image/generate/load/LoadSvgAsTexture2D.md` | C | image/shader node; Vuo built-in mapping exists or is partial |

### Lib.image.generate.misc

| full_path | purpose | input/output summary | source evidence | grade | reason |
|---|---|---|---|---|---|
| `Lib.image.generate.misc.JumpFloodFill` | Generates a distance field based on an image's Alpha channel | in: Image:Texture2D, MaxIterationCount:int=20; out: ImageOutput:Texture2D | C#: `Operators/Lib/image/generate/misc/JumpFloodFill.cs`; .t3: `Operators/Lib/image/generate/misc/JumpFloodFill.t3`; docs: `.help/docs/operators/lib/image/generate/misc/JumpFloodFill.md`; shaders: `Operators/Lib/Assets/shaders/img/generate/img-generate-JumpFloodFill.hlsl` | D | internal/obsolete/DX11-heavy first-pass document-only node; DX11/app terms: RenderTarget; shader/compute dependency |
| `Lib.image.generate.misc.Sketch` | Allows adding sketches as annotations or storyboards. | in: Background:Vector4={'X': 0.0, 'Y': 0.0, 'Z': 0.0, 'W': 0.25}, ColorMode:int=0, Filename:string={package}:Sketches/sketch_{id}.json, InputImage:Texture2D, OnionSkinRange:float=0.0, OverridePageIndex:int=-1, Resolution:Vector.Int2={'X': 0, 'Y': 0}, Scene:Command, +4 more; out: ColorBuffer:Texture2D, Points:BufferWithViews | C#: `Operators/Lib/image/generate/misc/Sketch.cs`; .t3: `Operators/Lib/image/generate/misc/Sketch.t3`; docs: `.help/docs/operators/lib/image/generate/misc/Sketch.md` | C | image/shader node; likely needs ISF/Metal/GLSL or Vuo-specific renderer design |
| `Lib.image.generate.misc.SlidingHistory` | Generates a waveform image by feeding in a horizontal texture line at the bottom of the texture. | in: Direction:int=0, HistoryLength:int=1024, IsEnabled:bool=True, ResetTrigger:bool=False, SourceSlice:float=0.0, Texture2d:Texture2D; out: Output:Texture2D | C#: `Operators/Lib/image/generate/misc/SlidingHistory.cs`; .t3: `Operators/Lib/image/generate/misc/SlidingHistory.t3`; docs: `.help/docs/operators/lib/image/generate/misc/SlidingHistory.md`; shaders: `Operators/Lib/Assets/shaders/img/generate/SlidingHistory.hlsl` | D | internal/obsolete/DX11-heavy first-pass document-only node; shader/compute dependency |

### Lib.image.generate.noise

| full_path | purpose | input/output summary | source evidence | grade | reason |
|---|---|---|---|---|---|
| `Lib.image.generate.noise.FractalNoise` | Generates a procedural fractal noise image effect also known as pink noise or fractional noise that can be used to create visual patterns that look like cloud, smoke, dust and scratches on film and similar Available Nois... | in: ColorA:Vector4={'X': 1e-06, 'Y': 9.9999e-07, 'Z': 9.9999e-07, 'W': 1.0}, ColorB:Vector4={'X': 1.0, 'Y': 0.99999, 'Z': 0.99999, 'W': 1.0}, GainAndBias:Vector2={'X': 0.5, 'Y': 0.5}, GenerateMips:bool=False, Iterations:int=2, Offset:Vector2={'X': 0.0, 'Y': 0.0}, OutputFormat:DXGI.Format=R16G16B16A16_Float, RandomPhase:float=5.0, +5 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/generate/noise/FractalNoise.cs`; .t3: `Operators/Lib/image/generate/noise/FractalNoise.t3`; docs: `.help/docs/operators/lib/image/generate/noise/FractalNoise.md`; shaders: `Operators/Lib/Assets/shaders/img/generate/FractalNoise.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.generate.noise.Grain` | Adds animated image pixel noise similar to that of an analog TV or film grain. | in: Amount:float=0.05, Animate:float=5.0, Brightness:float=0.0, Color:float=0.0, Exponent:float=1.0, GenerateMipmaps:bool=False, Image:Texture2D, RandomPhase:float=0.0, +2 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/generate/noise/Grain.cs`; .t3: `Operators/Lib/image/generate/noise/Grain.t3`; docs: `.help/docs/operators/lib/image/generate/noise/Grain.md`; shaders: `Operators/Lib/Assets/shaders/img/generate/Grain.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.generate.noise.ShardNoise` | A port of @ENDESGA's SHARD NOISE shader It can be used for clouds/fog/metal/crystal textures and more Check the link below. | in: ColorA:Vector4={'X': 1e-06, 'Y': 9.9999e-07, 'Z': 9.9999e-07, 'W': 1.0}, ColorB:Vector4={'X': 1.0, 'Y': 0.99999, 'Z': 0.99999, 'W': 1.0}, Direction:Vector2={'X': 0.0, 'Y': 0.0}, GainAndBias:Vector2={'X': 0.5, 'Y': 0.5}, GenerateMips:bool=False, Method:int=0, Offset:Vector2={'X': 0.0, 'Y': 0.0}, Phase:float=0.0, +5 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/generate/noise/ShardNoise.cs`; .t3: `Operators/Lib/image/generate/noise/ShardNoise.t3`; docs: `.help/docs/operators/lib/image/generate/noise/ShardNoise.md`; shaders: `Operators/Lib/Assets/shaders/img/generate/ShardNoise.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.generate.noise.TileableNoise` | Generates a procedural fractal noise image effect also known as pink noise or fractional noise that can be used to create visual patterns that look like cloud, smoke, dust and scratches on film and similar Available Nois... | in: ColorA:Vector4={'X': 1e-06, 'Y': 9.9999e-07, 'Z': 9.9999e-07, 'W': 1.0}, ColorB:Vector4={'X': 1.0, 'Y': 0.99999, 'Z': 0.99999, 'W': 1.0}, Contrast:float=1.7, Detail:int=1, Gain:float=0.5, GainAndBias:Vector2={'X': 0.5, 'Y': 0.5}, GenerateMips:bool=False, Lacunarity:float=2.0, +6 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/generate/noise/TileableNoise.cs`; .t3: `Operators/Lib/image/generate/noise/TileableNoise.t3`; docs: `.help/docs/operators/lib/image/generate/noise/TileableNoise.md`; shaders: `Operators/Lib/Assets/shaders/img/generate/PerlinNoise2d.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.generate.noise.WorleyNoise` | Also called Voronoi noise and cellular noise. | in: Clamping:Vector2={'X': 0.0, 'Y': 1.0}, ColorA:Vector4={'X': 1.0, 'Y': 0.9999899, 'Z': 0.9999899, 'W': 1.0}, ColorB:Vector4={'X': 0.0, 'Y': 0.0, 'Z': 0.0, 'W': 1.0}, GainAndBias:Vector2={'X': 0.5, 'Y': 0.5}, GenerateMips:bool=False, Method:int=0, Offset:Vector2={'X': 0.0, 'Y': 0.0}, Phase:float=5.0, +6 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/generate/noise/WorleyNoise.cs`; .t3: `Operators/Lib/image/generate/noise/WorleyNoise.t3`; docs: `.help/docs/operators/lib/image/generate/noise/WorleyNoise.md`; shaders: `Operators/Lib/Assets/shaders/img/generate/WorleyNoise.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |

### Lib.image.generate.pattern

| full_path | purpose | input/output summary | source evidence | grade | reason |
|---|---|---|---|---|---|
| `Lib.image.generate.pattern.FraserGrid` | The basis for the Fräser spiral illusion. | in: Background:Vector4={'X': 0.67475104, 'Y': 0.67498636, 'Z': 0.67569184, 'W': 1.0}, BAffects_LineRatio:float=0.0, BarWidth:float=0.035, BorderWidth:float=0.06, Feather:float=0.015, Fill:Vector4={'X': 0.0, 'Y': 0.0, 'Z': 0.0, 'W': 1.0}, FillB:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}, GAffects_ShapeSize:float=0.0, +10 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/generate/pattern/FraserGrid.cs`; .t3: `Operators/Lib/image/generate/pattern/FraserGrid.t3`; docs: `.help/docs/operators/lib/image/generate/pattern/FraserGrid.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/FraserGrid.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.generate.pattern.NumberPattern` | Renders columns for values that can be driven by an input texture. | in: CellRange:Vector2={'X': 1.0, 'Y': 1.0}, CellSize:Vector2={'X': 200.0, 'Y': 8.0}, Highlight:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}, HighlightThreshold:float=0.0, LineColor:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}, Offset:float=100.0, OriginalImage:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}, Position:Vector2={'X': 0.0, 'Y': 0.0}, +4 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/generate/pattern/NumberPattern.cs`; .t3: `Operators/Lib/image/generate/pattern/NumberPattern.t3`; docs: `.help/docs/operators/lib/image/generate/pattern/NumberPattern.md`; shaders: `Operators/Lib/Assets/shaders/img/generate/NumberPattern.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.generate.pattern.Raster` | Generates a wide range of patterns of dots and lines. | in: Background:Vector4={'X': 0.0, 'Y': 0.0, 'Z': 0.0, 'W': 0.0}, BlueToLineRatio:float=0.0, Color:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}, DotSize:float=0.05333333, Feather:float=0.02, GreenToLineWidth:float=0.0, Image:Texture2D, LineRatio:float=0.75, +8 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/generate/pattern/Raster.cs`; .t3: `Operators/Lib/image/generate/pattern/Raster.t3`; docs: `.help/docs/operators/lib/image/generate/pattern/Raster.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/Raster.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.generate.pattern.Rings` | Generates a procedural rings texture that can produce a wide variety of radial patterns. | in: _FillRatio:float=1.0, _HighlightRatio:float=0.0, _Ratio:Vector2={'X': 1.05, 'Y': 0.0}, _Segments:Vector2={'X': 20.0, 'Y': 0.0}, _Thickness:Vector2={'X': 0.5, 'Y': 0.0}, _Twist:Vector2={'X': 0.0, 'Y': 0.0}, Background:Vector4={'X': 0.0, 'Y': 0.0, 'Z': 0.0, 'W': 0.0}, BlendMode:int=0, +14 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/generate/pattern/Rings.cs`; .t3: `Operators/Lib/image/generate/pattern/Rings.t3`; docs: `.help/docs/operators/lib/image/generate/pattern/Rings.md`; shaders: `Operators/Lib/Assets/shaders/img/generate/Rings.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.generate.pattern.RyojiPattern1` | Generates animated patterns inspired by Ryoji Ikeda. | in: Background:Vector4={'X': 0.0, 'Y': 0.0, 'Z': 0.0, 'W': 1.0}, Contrast:float=0.75, Foreground:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}, ForgroundRatio:float=0.5, GenerateMipmaps:bool=False, Highlight:Vector4={'X': 1.0, 'Y': 0.0, 'Z': 0.0, 'W': 1.0}, HighlightProbability:float=0.01, HighlightSeed:float=0.0, +10 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/generate/pattern/RyojiPattern1.cs`; .t3: `Operators/Lib/image/generate/pattern/RyojiPattern1.t3`; docs: `.help/docs/operators/lib/image/generate/pattern/RyojiPattern1.md`; shaders: `Operators/Lib/Assets/shaders/img/generate/RyojiPattern1.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.generate.pattern.RyojiPattern2` | A pattern generator inspired by the work of Ryoji Ikeda. | in: Background:Vector4={'X': 1e-06, 'Y': 9.9999e-07, 'Z': 9.9999e-07, 'W': 0.0}, Contrast:float=0.5, Foreground:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}, ForgroundRatio:float=0.50333333, Highlight:Vector4={'X': 1.0, 'Y': 0.0, 'Z': 0.0, 'W': 1.0}, HighlightProbability:float=0.01, HighlightSeed:int=0, Image:Texture2D, +11 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/generate/pattern/RyojiPattern2.cs`; .t3: `Operators/Lib/image/generate/pattern/RyojiPattern2.t3`; docs: `.help/docs/operators/lib/image/generate/pattern/RyojiPattern2.md`; shaders: `Operators/Lib/Assets/shaders/img/generate/RyojiPattern2.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.generate.pattern.SinForm` | Generates a sine curve with various parameters The presets show various application possibilities for different patterns Other interesting patterns can be generated with [SinForm] [ZollnerPattern] [FraserGrid] [Raster] [... | in: Background:Vector4={'X': 0.0, 'Y': 0.0, 'Z': 0.0, 'W': 0.0}, Copies:float=0.0, Fade:float=1.0, Fill:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}, Image:Texture2D, LineWidth:float=0.04333334, Offset:Vector2={'X': 0.0, 'Y': 0.0}, OffsetCopies:Vector2={'X': 0.0, 'Y': 0.05}, +4 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/generate/pattern/SinForm.cs`; .t3: `Operators/Lib/image/generate/pattern/SinForm.t3`; docs: `.help/docs/operators/lib/image/generate/pattern/SinForm.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/SinForm.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.generate.pattern.ValueRaster` | *No description yet. | in: Background:Vector4={'X': 0.0, 'Y': 0.0, 'Z': 0.0, 'W': 0.0}, Color:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 0.695}, Density:Vector2={'X': 1000.0, 'Y': 1000.0}, Image:Texture2D, MajorLineWidth:float=1.0, MinorLineWidth:float=0.25, MixOriginal:float=1.0, RangeX:Vector2={'X': 0.0, 'Y': 1.0}, +2 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/generate/pattern/ValueRaster.cs`; .t3: `Operators/Lib/image/generate/pattern/ValueRaster.t3`; docs: `.help/docs/operators/lib/image/generate/pattern/ValueRaster.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/ValueRaster.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.generate.pattern.ZollnerPattern` | Creates an image for an optical illusion. | in: AmplifyIllusion:float=0.0, Background:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}, BAffects_HookRotation:float=0.0, BarWidth:float=0.2, Feather:float=0.02, Fill:Vector4={'X': 0.0, 'Y': 0.0, 'Z': 0.0, 'W': 1.0}, GAffects_HookLength:float=0.0, HookLength:float=0.7, +10 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/generate/pattern/ZollnerPattern.cs`; .t3: `Operators/Lib/image/generate/pattern/ZollnerPattern.t3`; docs: `.help/docs/operators/lib/image/generate/pattern/ZollnerPattern.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/ZollnerGrid.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |

### Lib.image.transform

| full_path | purpose | input/output summary | source evidence | grade | reason |
|---|---|---|---|---|---|
| `Lib.image.transform.Crop` | Crops an image or adds a frame. | in: LeftRight:Int2={'X': 0, 'Y': 0}, PaddingColor:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 0.0}, Texture2d:Texture2D, TopBottom:Int2={'X': 0, 'Y': 0}; out: Output:Texture2D | C#: `Operators/Lib/image/transform/Crop.cs`; .t3: `Operators/Lib/image/transform/Crop.t3`; docs: `.help/docs/operators/lib/image/transform/Crop.md`; shaders: `Operators/Lib/Assets/shaders/img/CropImage-cs.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.transform.ImageFFT` | Discrete Fourier Transform on 2d image. | in: Direction:int=0, Image:Texture2D, Inverse:bool=False, Normalization:int=0; out: Output:Texture2D | C#: `Operators/Lib/image/transform/ImageFFT.cs`; .t3: `Operators/Lib/image/transform/ImageFFT.t3`; docs: `.help/docs/operators/lib/image/transform/ImageFFT.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/ImageFFT.hlsl` | D | internal/obsolete/DX11-heavy first-pass document-only node; DX11/app terms: RenderTarget, PixelShader; shader/compute dependency |
| `Lib.image.transform.MakeTileableImage` | Makes an incoming image tileable based on linear edge Falloff. | in: EdgeFallOff:float=0.2, ImageA:Texture2D, IsEnabled:bool=True, TilingMode:int=3; out: Selected:Texture2D | C#: `Operators/Lib/image/transform/MakeTileableImage.cs`; .t3: `Operators/Lib/image/transform/MakeTileableImage.t3`; docs: `.help/docs/operators/lib/image/transform/MakeTileableImage.md` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.transform.MakeTileableImageAdvanced` | Makes an incoming image tileable based on linear edge falloff combined with a tweakable noise pattern that can also be animated. | in: AddNoiseToTransition:bool=True, EdgeFallOff:float=0.5, FadeOut:float=0.2, ImageA:Texture2D, Phase:float=0.0, Scale:float=0.2, TilingMode:int=3; out: Selected:Texture2D | C#: `Operators/Lib/image/transform/MakeTileableImageAdvanced.cs`; .t3: `Operators/Lib/image/transform/MakeTileableImageAdvanced.t3`; docs: `.help/docs/operators/lib/image/transform/MakeTileableImageAdvanced.md` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.transform.MirrorRepeat` | Shifts, slices or mirrors the incoming image to create endless textures or kaleidoscopic patterns when combined with itself A more sophisticated version: [KochKaleidoskope] For simpler transformations: [TransformImage] S... | in: Image:Texture2D, Offset:float=0.0, OffsetEdge:float=0.0, Offsetimage:Vector2={'X': 0.0, 'Y': 0.0}, Resolution:Int2={'X': -1, 'Y': -1}, RotateImage:float=0.0, RotateMirror:float=0.0, ShadeAmount:float=0.0, +2 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/transform/MirrorRepeat.cs`; .t3: `Operators/Lib/image/transform/MirrorRepeat.t3`; docs: `.help/docs/operators/lib/image/transform/MirrorRepeat.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/MirrorRepeat.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.transform.TransformImage` | Rotates, offsets, and scales the incoming image. | in: Filter:Filter=MinMagMipLinear, GenerateMips:bool=False, Image:Texture2D, Offset:Vector2={'X': 0.0, 'Y': 0.0}, Resolution:Int2={'X': 0, 'Y': 0}, ResolutionFactor:Vector2={'X': 1.0, 'Y': 1.0}, Rotation:float=0.0, Scale:float=1.0, +2 more; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/transform/TransformImage.cs`; .t3: `Operators/Lib/image/transform/TransformImage.t3`; docs: `.help/docs/operators/lib/image/transform/TransformImage.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/TransformImage.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |

### Lib.image.use

| full_path | purpose | input/output summary | source evidence | grade | reason |
|---|---|---|---|---|---|
| `Lib.image.use.Blend` | Blends two images. | in: AlphaMode:int=0, BlendMode:int=0, ColorA:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}, ColorB:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}, GenerateMips:bool=False, ImageA:Texture2D, ImageB:Texture2D, NormalForUpperHalf:bool=False, +2 more; out: Output:Texture2D | C#: `Operators/Lib/image/use/Blend.cs`; .t3: `Operators/Lib/image/use/Blend.t3`; docs: `.help/docs/operators/lib/image/use/Blend.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/Blend.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.use.BlendImages` | Blends the connected input images with cross-fading and using a float index. | in: BlendFraction:float=0.0, Input:Texture2D, Resolution:Int2={'X': 0, 'Y': 0}; out: OutputImage:Texture2D | C#: `Operators/Lib/image/use/BlendImages.cs`; .t3: `Operators/Lib/image/use/BlendImages.t3`; docs: `.help/docs/operators/lib/image/use/BlendImages.md` | B | simple VuoImage selection/list/control behavior; no shader ref found in C#/.t3 |
| `Lib.image.use.BlendWithMask` | Blends two images by the brightness of a 3rd mask image. | in: ColorA:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}, ColorB:Vector4={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}, ImageA:Texture2D, ImageB:Texture2D, Mask:Texture2D, Resolution:Int2={'X': 0, 'Y': 0}; out: Output:Texture2D | C#: `Operators/Lib/image/use/BlendWithMask.cs`; .t3: `Operators/Lib/image/use/BlendWithMask.t3`; docs: `.help/docs/operators/lib/image/use/BlendWithMask.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/BlendWithMask.hlsl`, `Operators/Lib/Assets/shaders/img/fx/Default2-vs.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.use.Combine3Images` | A node to combine 3 input images into the RGBA channels of a new one. | in: ColorA:Vector4={'X': 1.0, 'Y': 0.0, 'Z': 0.0, 'W': 1.0}, ColorB:Vector4={'X': 0.0, 'Y': 1.0, 'Z': 0.0, 'W': 1.0}, ColorC:Vector4={'X': 0.0, 'Y': 0.0, 'Z': 1.0, 'W': 1.0}, GenerateMips:bool=False, ImageA:Texture2D, ImageB:Texture2D, ImageC:Texture2D, SelectAlphaChannel:int=4, +3 more; out: Output:Texture2D | C#: `Operators/Lib/image/use/Combine3Images.cs`; .t3: `Operators/Lib/image/use/Combine3Images.t3`; docs: `.help/docs/operators/lib/image/use/Combine3Images.md`; shaders: `Operators/Lib/Assets/shaders/img/use/img-combine-3.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.use.CombineMaterialChannels` | Combines roughness, metallic, and ambient occlusion texture maps that are loaded with [LoadImage] into a single texture for [SetMaterial] to create PBR materials. | in: GenerateMips:bool=True, Metallic:Texture2D, Occlusion:Texture2D, RemapRoughness:Curve={'Curve': {'PreCurve': 'Constant', 'PostCurve': 'Constant', 'Keys': [{'Time': 0.0, 'Value': 0.0, 'InType': 'Linear', 'OutType': 'Linear', 'InEditMode': 'Linear', 'OutEditMode': 'Linear', 'InTangentAngle': 0.7853981633974483, 'OutTangentAngle': 3.9269908169872414}, {'Time': 1.0, 'Value': 1.0, 'InType': 'Linear', 'OutType': 'Linear', 'InEditMode': 'Linear', 'OutEditMode': 'Linear', 'InTangentAngle': 0.7853981633974483, 'OutTangentAngle': -2.356194490192345}]}}, Resolution:Int2={'X': 0, 'Y': 0}, Roughness:Texture2D; out: Output:Texture2D | C#: `Operators/Lib/image/use/CombineMaterialChannels.cs`; .t3: `Operators/Lib/image/use/CombineMaterialChannels.t3`; docs: `.help/docs/operators/lib/image/use/CombineMaterialChannels.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/Default2-vs.hlsl`, `Operators/Lib/Assets/shaders/img/use/CombineMaterialChannels.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.use.CombineMaterialChannels2` | Combines roughness, metallic, and ambient occlusion texture maps into a single texture for [SetMaterial]. | in: ColorA:Vector4={'X': 1.0, 'Y': 0.0, 'Z': 0.0, 'W': 1.0}, ColorB:Vector4={'X': 0.0, 'Y': 1.0, 'Z': 0.0, 'W': 1.0}, ColorC:Vector4={'X': 0.0, 'Y': 0.0, 'Z': 1.0, 'W': 1.0}, GenerateMips:bool=False, ImageA:Texture2D, ImageB:Texture2D, ImageC:Texture2D, SelectAlphaChannel:int, +3 more; out: Output:Texture2D | C#: `Operators/Lib/image/use/CombineMaterialChannels2.cs`; .t3: `Operators/Lib/image/use/CombineMaterialChannels2.t3`; docs: `.help/docs/operators/lib/image/use/CombineMaterialChannels2.md`; shaders: `Operators/Lib/Assets/shaders/img/use/img-combine-3.hlsl` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.use.CustomPixelShader` | Creates a custom shader from a source parameter. | in: A:float=0.0, AdditionalCode:string=, B:float=0.0, C:float=0.0, Clear:bool=True, ConstantBuffers:Direct3D11.Buffer, CustomSampler:Direct3D11.SamplerState, D:float=0.0, +11 more; out: ShaderCode_:string, TextureOutput:Texture2D | C#: `Operators/Lib/image/use/CustomPixelShader.cs`; .t3: `Operators/Lib/image/use/CustomPixelShader.t3`; docs: `.help/docs/operators/lib/image/use/CustomPixelShader.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/Default2-vs.hlsl`, `Operators/Lib/Assets/shaders/img/use/CustomImageShader-template.hlsl` | D | internal/obsolete/DX11-heavy first-pass document-only node; DX11/app terms: Direct3D11, ShaderResourceView, RenderTarget, PixelShader, VertexShader; shader/compute dependency |
| `Lib.image.use.DepthBufferAsGrayScale` | Converts the provided depth buffer into a grayscale texture. | in: ClampOutput:bool=False, Mode:int=0, NearFarRange:Vector2={'X': 0.01, 'Y': 1000.0}, OutputRange:Vector2={'X': 0.0, 'Y': 5.0}, Texture2d:Texture2D; out: Output:Texture2D | C#: `Operators/Lib/image/use/DepthBufferAsGrayScale.cs`; .t3: `Operators/Lib/image/use/DepthBufferAsGrayScale.t3`; docs: `.help/docs/operators/lib/image/use/DepthBufferAsGrayScale.md`; shaders: `Operators/Lib/Assets/shaders/img/post-fx/depth-to-linear.hlsl` | D | internal/obsolete/DX11-heavy first-pass document-only node; DX11/app terms: RenderTarget; shader/compute dependency |
| `Lib.image.use.FirstValidTexture` | *No description yet. | in: Input:Texture2D; out: Output:Texture2D | C#: `Operators/Lib/image/use/FirstValidTexture.cs`; .t3: `Operators/Lib/image/use/FirstValidTexture.t3`; docs: `.help/docs/operators/lib/image/use/FirstValidTexture.md` | B | simple VuoImage selection/list/control behavior; no shader ref found in C#/.t3 |
| `Lib.image.use.Fxaa` | Fast approXimate Anti-Aliasing is a post-FX, use it to improve SDF / RayMarching. | in: Image:Texture2D, KeepAlpha:bool=False, Preset:int=0; out: TextureOutput:Texture2D | C#: `Operators/Lib/image/use/Fxaa.cs`; .t3: `Operators/Lib/image/use/Fxaa.t3`; docs: `.help/docs/operators/lib/image/use/Fxaa.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/Default2-vs.hlsl`, `Operators/Lib/Assets/shaders/img/use/FXAA.hlsl` | C | image/shader node; likely needs ISF/Metal/GLSL or Vuo-specific renderer design |
| `Lib.image.use.KeepInTextureArray` | Pick a single "slice" from a TextureArray | in: ReadIndex:int=0, SourceTexture:Texture2D, TriggerWrite:bool, WriteIndex:int; out: ArraySize:int | C#: `Operators/Lib/image/use/KeepInTextureArray.cs`; .t3: `Operators/Lib/image/use/KeepInTextureArray.t3`; docs: `.help/docs/operators/lib/image/use/KeepInTextureArray.md` | D | internal/obsolete/DX11-heavy first-pass document-only node; DX11/app terms: TextureArray |
| `Lib.image.use.KeepPreviousFrame` | *No description yet. | in: ImageA:Texture2D, Keep:bool; out: CurrentFrame:Texture2D, PreviousFrame:Texture2D | C#: `Operators/Lib/image/use/KeepPreviousFrame.cs`; .t3: `Operators/Lib/image/use/KeepPreviousFrame.t3`; docs: `.help/docs/operators/lib/image/use/KeepPreviousFrame.md` | C | image/shader node; Vuo built-in mapping exists or is partial |
| `Lib.image.use.NormalMap` | Converts the brightness of an image into a normal map that can be used with [SetMaterial]. | in: Impact:float=1.0, LightMap:Texture2D, Mode:int=0, OutputFormat:Format=R16G16B16A16_Float, Resolution:Int2={'X': 0, 'Y': 0}, SampleRadius:float=2.0, TextureRepeat:Direct3D11.TextureAddressMode=MirrorOnce, Twist:float=180.0; out: Output:Texture2D | C#: `Operators/Lib/image/use/NormalMap.cs`; .t3: `Operators/Lib/image/use/NormalMap.t3`; docs: `.help/docs/operators/lib/image/use/NormalMap.md`; shaders: `Operators/Lib/Assets/shaders/img/fx/NormalMap.hlsl` | C | image/shader node; likely needs ISF/Metal/GLSL or Vuo-specific renderer design |
| `Lib.image.use.PickTexture` | Picks one of the connected textures. | in: Index:int=0, Input:Texture2D; out: Selected:Texture2D | C#: `Operators/Lib/image/use/PickTexture.cs`; .t3: `Operators/Lib/image/use/PickTexture.t3`; docs: `.help/docs/operators/lib/image/use/PickTexture.md` | B | simple VuoImage selection/list/control behavior; no shader ref found in C#/.t3 |
| `Lib.image.use.RenderWithMotionBlur` | This will render multiple instances of the incoming op each pass with slightly offset local time. | in: Passes:int=5, Strength:float=1.0, Texture:Texture2D; out: Output:Texture2D | C#: `Operators/Lib/image/use/RenderWithMotionBlur.cs`; .t3: `Operators/Lib/image/use/RenderWithMotionBlur.t3`; docs: `.help/docs/operators/lib/image/use/RenderWithMotionBlur.md`; shaders: `Operators/Lib/Assets/shaders/img/analyze/vs-draw-viewport-quad.hlsl` | D | internal/obsolete/DX11-heavy first-pass document-only node; DX11/app terms: RenderTarget; shader/compute dependency |
| `Lib.image.use.SwapTextures` | A helper that swaps texture buffers. | in: EnableSwap:bool=False, TextureAInput:Texture2D, TextureBInput:Texture2D; out: TextureA:Texture2D, TextureB:Texture2D | C#: `Operators/Lib/image/use/SwapTextures.cs`; .t3: `Operators/Lib/image/use/SwapTextures.t3`; docs: `.help/docs/operators/lib/image/use/SwapTextures.md` | B | simple VuoImage selection/list/control behavior; no shader ref found in C#/.t3 |
| `Lib.image.use.UseFallbackTexture` | Automatically replaces a non-loadable texture with a predefined backup. | in: Fallback:Texture2D, TextureA:Texture2D; out: Output:Texture2D | C#: `Operators/Lib/image/use/UseFallbackTexture.cs`; .t3: `Operators/Lib/image/use/UseFallbackTexture.t3`; docs: `.help/docs/operators/lib/image/use/UseFallbackTexture.md` | B | simple VuoImage selection/list/control behavior; no shader ref found in C#/.t3 |
| `Lib.image.use.UseTextureReference` | Uses a reference to a [RenderTarget] to implement feedback effects. | in: None; out: DepthTexture:Texture2D, Reference:RenderTargetReference, Texture:Texture2D | C#: `Operators/Lib/image/use/UseTextureReference.cs`; .t3: `Operators/Lib/image/use/UseTextureReference.t3`; docs: `.help/docs/operators/lib/image/use/UseTextureReference.md` | D | internal/obsolete/DX11-heavy first-pass document-only node; DX11/app terms: TextureReference, RenderTarget |
| `Lib.image.use._KeepPreviousFrame_Old1` | Unknown. | in: Enable:bool=True, HasFrameChanged:bool=False, Image:Texture2D; out: TextureA:Texture2D, TextureB:Texture2D | C#: `Operators/Lib/image/use/_KeepPreviousFrame_Old1.cs`; .t3: `Operators/Lib/image/use/_KeepPreviousFrame_Old1.t3`; docs: `Unknown` | D | internal/obsolete/DX11-heavy first-pass document-only node |

## Full Node Cards

## ImageLevels

- TiXL full path: `Lib.image.analyze.ImageLevels`
- Namespace: `Lib.image.analyze`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/analyze/ImageLevels.cs`
  - .t3 defaults: `Operators/Lib/image/analyze/ImageLevels.t3`
  - docs: `.help/docs/operators/lib/image/analyze/ImageLevels.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/fx/ImageLevels.hlsl`
- Purpose: Visualizes the image brightness distribution of an image.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `Center`: Vector2, default {'X': 0.25, 'Y': 0.0}; semantic role Unknown
  - `Range`: Vector2, default {'X': 0.0, 'Y': 1.0}; semantic role Unknown
  - `Rotation`: float, default 0.0; semantic role Unknown
  - `ShowOriginal`: float, default 1.0; semantic role Unknown
  - `Texture2d`: Texture2D, default Unknown; semantic role Unknown
  - `Width`: float, default 0.2; semantic role Unknown
- Outputs:
  - `Output`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - DX11/app-specific evidence: RenderTarget
  - important edge cases: Unknown beyond documented ports
- Observed graph usage:
  - common incoming nodes: Lib.image.generate.basic.LinearGradient (8), Lib.image.analyze.CompareImages (7), Lib.image.generate.basic.RenderTarget (2), Add (1), Lib.render._dx11.fxsetup._ImageFxShaderSetup2 (1)
  - common outgoing nodes: Lib.image.analyze.ImageLevels (8), Lib.image.analyze.ImageLevels (4), Lib.image.analyze.ImageLevels (2), Lib.image.analyze.ImageLevels (2), Lib.image.analyze.ImageLevels (1)
- Vuo mapping:
  - Vuo input types: Center -> VuoPoint2d or integer pair; Range -> VuoPoint2d or integer pair; Rotation -> VuoReal; ShowOriginal -> VuoReal; Texture2d -> VuoImage; Width -> VuoReal
  - Vuo output types: Output -> VuoImage
  - direct built-in Vuo equivalent, if any: no direct Vuo node; shader rewrite
  - missing Vuo support: direct built-in equivalent not found; HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; likely needs ISF/Metal/GLSL or Vuo-specific renderer design
- First implementation recommendation: Write an ISF/Metal/GLSL parity shader after freezing TiXL defaults and a fixture image.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## AdjustColors

- TiXL full path: `Lib.image.color.AdjustColors`
- Namespace: `Lib.image.color`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/color/AdjustColors.cs`
  - .t3 defaults: `Operators/Lib/image/color/AdjustColors.t3`
  - docs: `.help/docs/operators/lib/image/color/AdjustColors.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/fx/AdjustColors.hlsl`
- Purpose: Adjusts various color properties of the incoming image and adds a slight vignette.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `Background`: Vector4, default {'X': 1e-06, 'Y': 9.9999e-07, 'Z': 9.9999e-07, 'W': 1.0}; semantic role Unknown
  - `Brightness`: float, default 0.0; semantic role Unknown
  - `Colorize`: Vector4, default {'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 0.0}; semantic role Unknown
  - `Contrast`: float, default 0.0; semantic role Unknown
  - `Exposure`: float, default 1.0; semantic role Unknown
  - `Hue`: float, default 0.0; semantic role Unknown
  - `OrangeTeal`: float, default 0.0; semantic role Unknown
  - `PreventClamping`: Vector2, default {'X': 0.0, 'Y': 5.0}; semantic role Unknown
  - `Saturation`: float, default 1.0; semantic role Unknown
  - `Texture2d`: Texture2D, default Unknown; semantic role Unknown
  - `Vignette`: float, default 0.0; semantic role Unknown
- Outputs:
  - `Output`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - DX11/app-specific evidence: TextureReference, RenderTarget, PixelShader
  - important edge cases: Unknown beyond documented ports
- Observed graph usage:
  - common incoming nodes: Lib.image.generate.load.LoadImage (11), Lib.image.generate.basic.RenderTarget (8), Lib.image.transform.MakeTileableImage (5), Lib.image.use.Blend (4), Lib.image.fx.stylize.Glow (3)
  - common outgoing nodes: Lib.image.color.AdjustColors (11), Lib.image.color.AdjustColors (9), Lib.image.color.AdjustColors (9), Lib.image.color.AdjustColors (5), Lib.image.color.AdjustColors (4)
- Vuo mapping:
  - Vuo input types: Background -> VuoColor or VuoPoint4d; Brightness -> VuoReal; Colorize -> VuoColor or VuoPoint4d; Contrast -> VuoReal; Exposure -> VuoReal; Hue -> VuoReal; OrangeTeal -> VuoReal; PreventClamping -> VuoPoint2d or integer pair; Saturation -> VuoReal; Texture2d -> VuoImage
  - Vuo output types: Output -> VuoImage
  - direct built-in Vuo equivalent, if any: vuo.image.color.adjust (partial)
  - missing Vuo support: HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## ColorGrade

- TiXL full path: `Lib.image.color.ColorGrade`
- Namespace: `Lib.image.color`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/color/ColorGrade.cs`
  - .t3 defaults: `Operators/Lib/image/color/ColorGrade.t3`
  - docs: `.help/docs/operators/lib/image/color/ColorGrade.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/ColorGrade.hlsl`
- Purpose: Adjusts the color grading of the incoming image for the Gain (highlights), Gamma (mid-tones), and Lift (shadows).
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `ClampResult`: bool, default False; semantic role Unknown
  - `Gain`: Vector4, default {'X': 0.5, 'Y': 0.5, 'Z': 0.5, 'W': 0.506}; semantic role Unknown
  - `Gamma`: Vector4, default {'X': 0.5, 'Y': 0.5, 'Z': 0.5, 'W': 0.506}; semantic role Unknown
  - `GenerateMipmaps`: bool, default False; semantic role Unknown
  - `Lift`: Vector4, default {'X': 0.5, 'Y': 0.5, 'Z': 0.5, 'W': 0.25}; semantic role Unknown
  - `PreSaturate`: float, default 1.0; semantic role Unknown
  - `Texture2d`: Texture2D, default Unknown; semantic role Unknown
  - `VignetteCenter`: Vector2, default {'X': 0.0, 'Y': 0.0}; semantic role Unknown
  - `VignetteColor`: Vector4, default {'X': 0.49999997, 'Y': 0.49999997, 'Z': 0.49999997, 'W': 0.0}; semantic role Unknown
  - `VignetteFeather`: float, default 1.0; semantic role Unknown
  - `VignetteRadius`: float, default 1.0; semantic role Unknown
- Outputs:
  - `Output`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - DX11/app-specific evidence: RenderTarget
  - important edge cases: Unknown beyond documented ports
- Observed graph usage:
  - common incoming nodes: Lib.image.fx.stylize.Glow (15), Lib.image.generate.noise.Grain (10), Lib.numbers.color.SampleGradient (6), Lib.image.use.Blend (5), Lib.image.generate.basic.RenderTarget (4)
  - common outgoing nodes: Lib.image.color.ColorGrade (11), Lib.image.color.ColorGrade (10), Lib.image.color.ColorGrade (5), Lib.image.color.ColorGrade (5), Lib.image.color.ColorGrade (5)
- Vuo mapping:
  - Vuo input types: ClampResult -> VuoBoolean; Gain -> VuoColor or VuoPoint4d; Gamma -> VuoColor or VuoPoint4d; GenerateMipmaps -> VuoBoolean; Lift -> VuoColor or VuoPoint4d; PreSaturate -> VuoReal; Texture2d -> VuoImage; VignetteCenter -> VuoPoint2d or integer pair; VignetteColor -> VuoColor or VuoPoint4d; VignetteFeather -> VuoReal
  - Vuo output types: Output -> VuoImage
  - direct built-in Vuo equivalent, if any: vuo.image.color.adjust (partial)
  - missing Vuo support: HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## RemapColor

- TiXL full path: `Lib.image.color.RemapColor`
- Namespace: `Lib.image.color`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/color/RemapColor.cs`
  - .t3 defaults: `Operators/Lib/image/color/RemapColor.t3`
  - docs: `.help/docs/operators/lib/image/color/RemapColor.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/fx/ColorRemap.hlsl`
- Purpose: Replaces the colors of an image with a gradient based on its brightness.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor. TiXL Gradient has no direct Vuo type found in source scan; model as color/position lists or custom gradient helper.
- Inputs:
  - `Cycle`: float, default 0.0; semantic role Unknown
  - `DontColorAlpha`: bool, default False; semantic role Unknown
  - `Exposure`: float, default 1.0; semantic role Unknown
  - `GainAndBias`: Vector2, default {'X': 0.5, 'Y': 0.5}; semantic role Unknown
  - `Gradient`: Gradient, default {'Gradient': {'Interpolation': 'Linear', 'Steps': [{'Id': 'cf89ad61-23e5-46d1-9d13-e2bae35721ba', 'NormalizedPosition': 0.0, 'Color': {'R': 0.0, 'G': 1.2159347e-11, 'B': 1e-06, 'A': 1.0}}, {'Id': '752c4515-16e0-4b31-94b7-47ae200b55d8', 'NormalizedPosition': 1.0, 'Color': {'R': 1.0, 'G': 0.99999, 'B': 1.0, 'A': 1.0}}]}}; semantic role Unknown
  - `GradientSteps`: int, default 256; semantic role Unknown
  - `Image`: Texture2D, default Unknown; semantic role Unknown
  - `Mode`: int, default 1, enum: UseGrayScale, IndividualChannels; semantic role Unknown
  - `Repeat`: float, default 1.0; semantic role Unknown
  - `Resolution`: Int2, default {'X': 0, 'Y': 0}; semantic role Unknown
  - `WrapMode`: TextureAddressMode, default Clamp; semantic role Unknown
- Outputs:
  - `TextureOutput`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: Lib.image.generate.basic.RadialGradient (4), Lib.image.fx.blur.Blur (2), Lib.image.generate.noise.WorleyNoise (2), Lib.image.generate.noise.TileableNoise (2), Lib.image.use.Blend (1)
  - common outgoing nodes: Lib.image.color.RemapColor (6), Lib.image.color.RemapColor (4), Lib.image.color.RemapColor (3), Lib.image.color.RemapColor (2), Lib.image.color.RemapColor (2)
- Vuo mapping:
  - Vuo input types: Cycle -> VuoReal; DontColorAlpha -> VuoBoolean; Exposure -> VuoReal; GainAndBias -> VuoPoint2d or integer pair; Gradient -> VuoList<VuoColor> + positions; GradientSteps -> VuoInteger / enum; Image -> VuoImage; Mode -> VuoInteger / enum; Repeat -> VuoReal; Resolution -> VuoPoint2d or integer pair
  - Vuo output types: TextureOutput -> VuoImage
  - direct built-in Vuo equivalent, if any: vuo.image.color.map (partial)
  - missing Vuo support: HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## ToneMapping

- TiXL full path: `Lib.image.color.ToneMapping`
- Namespace: `Lib.image.color`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/color/ToneMapping.cs`
  - .t3 defaults: `Operators/Lib/image/color/ToneMapping.t3`
  - docs: `.help/docs/operators/lib/image/color/ToneMapping.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/fx/ToneMap.hlsl`
- Purpose: Tone mapping is the process of converting high dynamic range (HDR) imagery to a displayable format, adjusting the contrast and brightness to make it visually appealing while preserving details in both highlights and shad...
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `CorrectGamma`: bool, default False; semantic role Unknown
  - `Exposure`: float, default 1.0; semantic role Unknown
  - `Gamma`: float, default 2.2; semantic role Unknown
  - `Mode`: int, default 4, enum: Aces, Reinhard, Filmic, Uncharted2, AgX, AgX_Punchy, None; semantic role Unknown
  - `Texture2d`: Texture2D, default Unknown; semantic role Unknown
- Outputs:
  - `Output`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - DX11/app-specific evidence: RenderTarget
  - important edge cases: Unknown beyond documented ports
- Observed graph usage:
  - common incoming nodes: Lib.image.fx.blur.Bloom (6), Lib.image.color.ColorGrade (2), Lib.image.fx.stylize.Glow (2), Lib.image.generate.basic.RenderTarget (2), Lib.render._dx11.fxsetup._ImageFxShaderSetupStatic (1)
  - common outgoing nodes: Lib.image.color.ToneMapping (3), Lib.image.color.ToneMapping (1), Lib.image.color.ToneMapping (1), Lib.image.color.ToneMapping (1), Lib.image.color.ToneMapping (1)
- Vuo mapping:
  - Vuo input types: CorrectGamma -> VuoBoolean; Exposure -> VuoReal; Gamma -> VuoReal; Mode -> VuoInteger / enum; Texture2d -> VuoImage
  - Vuo output types: Output -> VuoImage
  - direct built-in Vuo equivalent, if any: no direct tone-map node found
  - missing Vuo support: direct built-in equivalent not found; HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; likely needs ISF/Metal/GLSL or Vuo-specific renderer design
- First implementation recommendation: Write an ISF/Metal/GLSL parity shader after freezing TiXL defaults and a fixture image.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## Bloom

- TiXL full path: `Lib.image.fx.blur.Bloom`
- Namespace: `Lib.image.fx.blur`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/fx/blur/Bloom.cs`
  - .t3 defaults: `Operators/Lib/image/fx/blur/Bloom.t3`
  - docs: `.help/docs/operators/lib/image/fx/blur/Bloom.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/blur/Bloom-BrightpassPS.hlsl`, `Operators/Lib/Assets/shaders/img/blur/Bloom-CopyPS.hlsl`, `Operators/Lib/Assets/shaders/img/blur/Bloom-DownsamplePS.hlsl`, `Operators/Lib/Assets/shaders/img/blur/Bloom-FullscreenVS.hlsl`, `Operators/Lib/Assets/shaders/img/blur/Bloom-SeparableBlurPS.hlsl`, `Operators/Lib/Assets/shaders/img/blur/Bloom-UpsamplePS.hlsl`
- Purpose: A more versatile and faster version of [Glow].
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor. TiXL Gradient has no direct Vuo type found in source scan; model as color/position lists or custom gradient helper.
- Inputs:
  - `Blur`: float, default 1.0; Offsets the blur amount. This might be useful to craft looks (e.g. to limit the glow spread).
But it will cause noticeable artifacts.
  - `Clamp`: bool, default False; Clamp the blur kernels before combining.
This will give a slightly different look. It will _NOT_ clamp the results. Use [ToneMap] for that.
  - `ColorWeights`: Vector4, default {'X': 0.299, 'Y': 0.587, 'Z': 0.114, 'W': 1.0}; Colors to calculate the initial luminance used for the glow. This can be useful for limiting the effect on certain colors.
The default values are the NTSC perception of color channels.
Reducing the alpha channel blends these colors to gray.
  - `GainAndBias`: Vector2, default {'X': 0.5, 'Y': 0.5}; This can be used to shape the distribution of the blur kernels
Lower curves focus on the core, higher curves on the blurred parts.
Many settings cause artifacts, but when used subtly can be very useful for crafting a look.
  - `GlowGradient`: Gradient, default {'Gradient': {'Interpolation': 'Linear', 'Steps': [{'Id': '9bb24404-e993-4c5f-bda6-db2cdea0e00b', 'NormalizedPosition': 0.0, 'Color': {'R': 1.0, 'G': 1.0, 'B': 1.0, 'A': 1.0}}]}}; Can be used to colorize or shape the glow.
It's multiplied onto each blur kernel with the more blurred levels on the right.
TIP:
- You can also adjust the brightness above 1 (hold CTRL while dragging the brightness slider) to amplify levels like the core.
  - `Image`: Texture2D, default Unknown; semantic role Unknown
  - `Intensity`: float, default 6.0; The overall intensity values below 1 are hardly noticeable.
  - `MaxLevels`: int, default 10; The number of blur levels applied. The maximum is 12 (which should be enough for most resolutions).
In most scenarios you wouldn't adjust this, but in edge scenarios, it might help to optimize performance or craft special looks.
  - `Threshold`: float, default 0.25; Limit the glow to brighter areas.
- Outputs:
  - `Result`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - DX11/app-specific evidence: RenderTarget
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: Lib.image.generate.basic.RenderTarget (15), Lib.render.postfx.DepthOfField (3), Lib.image.fx._._ExecuteBloomPasses (1), Lib.render.postfx.SSAO (1), Lib.image.color.ConvertFormat (1)
  - common outgoing nodes: Lib.image.fx.blur.Bloom (9), Lib.image.fx.blur.Bloom (6), Lib.image.fx.blur.Bloom (3), Lib.image.fx.blur.Bloom (1), Lib.image.fx.blur.Bloom (1)
- Vuo mapping:
  - Vuo input types: Blur -> VuoReal; Clamp -> VuoBoolean; ColorWeights -> VuoColor or VuoPoint4d; GainAndBias -> VuoPoint2d or integer pair; GlowGradient -> VuoList<VuoColor> + positions; Image -> VuoImage; Intensity -> VuoReal; MaxLevels -> VuoInteger / enum; Threshold -> VuoReal
  - Vuo output types: Result -> VuoImage
  - direct built-in Vuo equivalent, if any: no direct bloom; compose blur/blend or shader
  - missing Vuo support: direct built-in equivalent not found; HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; likely needs ISF/Metal/GLSL or Vuo-specific renderer design
- First implementation recommendation: Write an ISF/Metal/GLSL parity shader after freezing TiXL defaults and a fixture image.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## Blur

- TiXL full path: `Lib.image.fx.blur.Blur`
- Namespace: `Lib.image.fx.blur`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/fx/blur/Blur.cs`
  - .t3 defaults: `Operators/Lib/image/fx/blur/Blur.t3`
  - docs: `.help/docs/operators/lib/image/fx/blur/Blur.md`
  - related shader / helper source: `Blur.hlsl`, `Operators/Lib/Assets/shaders/img/fx/Blur.hlsl`
- Purpose: Blurs the incoming image Useful Ops for a PostFX Pipeline: [MotionBlur] [DepthOfField] [ChromaticAbberation] [Glow] [Grain] [Blur]
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `Image`: Texture2D, default Unknown; semantic role Unknown
  - `Offset`: float, default 0.0; Offsets the brightness of the image
  - `Opacity`: float, default 1.0; Changes the opacity/gamma of the image
  - `Resolution`: Int2, default {'X': 0, 'Y': 0}; Overwrites/redefines the resolution of the incoming image
  - `Samples`: float, default 8.0; Defines the number of iterations with which the image is blurred.
Higher values produce higher quality at the cost of computing speed.
  - `Size`: float, default 1.0; Defines the strength/radius of the blur
  - `Wrap`: TextureAddressMode, default MirrorOnce; Defines what is displayed at the edge of the image or how it is repeated should it be cut off/offset over its borders.
- Outputs:
  - `TextureOutput`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - DX11/app-specific evidence: TextureReference, RenderTarget
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: Lib.image.generate.basic.RenderTarget (11), Multiply (8), Lib.image.fx.stylize.Glow (7), Lib.image.use.UseTextureReference (5), Lib.image.fx.blur.Blur (5)
  - common outgoing nodes: Lib.image.fx.blur.Blur (13), Lib.image.fx.blur.Blur (9), Lib.image.fx.blur.Blur (5), Lib.image.fx.blur.Blur (3), Lib.image.fx.blur.Blur (3)
- Vuo mapping:
  - Vuo input types: Image -> VuoImage; Offset -> VuoReal; Opacity -> VuoReal; Resolution -> VuoPoint2d or integer pair; Samples -> VuoReal; Size -> VuoReal; Wrap -> Unknown / custom
  - Vuo output types: TextureOutput -> VuoImage
  - direct built-in Vuo equivalent, if any: vuo.image.blur
  - missing Vuo support: HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## DirectionalBlur

- TiXL full path: `Lib.image.fx.blur.DirectionalBlur`
- Namespace: `Lib.image.fx.blur`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/fx/blur/DirectionalBlur.cs`
  - .t3 defaults: `Operators/Lib/image/fx/blur/DirectionalBlur.t3`
  - docs: `.help/docs/operators/lib/image/fx/blur/DirectionalBlur.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/fx/DirectionalBlur.hlsl`
- Purpose: Blurs the incoming image along a directional angle.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `Angle`: float, default 0.0; semantic role Unknown
  - `FxAngleFactor`: float, default 1.0; semantic role Unknown
  - `FxSizeFactor`: float, default 0.0; semantic role Unknown
  - `FxTextures`: Texture2D, default Unknown; semantic role Unknown
  - `Image`: Texture2D, default Unknown; semantic role Unknown
  - `RefinementPass`: bool, default False; semantic role Unknown
  - `RefinementSamples`: int, default 6; semantic role Unknown
  - `RefineSizeFactor`: float, default 0.0; semantic role Unknown
  - `Resolution`: Vector.Int2, default {'X': 0, 'Y': 0}; semantic role Unknown
  - `Samples`: float, default 16.0; semantic role Unknown
  - `Size`: float, default 0.1; semantic role Unknown
  - `Wrap`: Direct3D11.TextureAddressMode, default Clamp; semantic role Unknown
- Outputs:
  - `TextureOutput`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - DX11/app-specific evidence: Direct3D11, RenderTarget, SharpDX
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: Lib.image.generate.load.LoadImage (2), Lib.image.use.PickTexture (1), Rotate (1), Lib.image.color.RemapColor (1), _OldTvGlitch (1)
  - common outgoing nodes: Lib.image.fx.blur.DirectionalBlur (13), Lib.image.fx.blur.DirectionalBlur (3), Lib.image.fx.blur.DirectionalBlur (2), Lib.image.fx.blur.DirectionalBlur (2), Lib.image.fx.blur.DirectionalBlur (2)
- Vuo mapping:
  - Vuo input types: Angle -> VuoReal; FxAngleFactor -> VuoReal; FxSizeFactor -> VuoReal; FxTextures -> VuoImage; Image -> VuoImage; RefinementPass -> VuoBoolean; RefinementSamples -> VuoInteger / enum; RefineSizeFactor -> VuoReal; Resolution -> VuoPoint2d or integer pair; Samples -> VuoReal
  - Vuo output types: TextureOutput -> VuoImage
  - direct built-in Vuo equivalent, if any: vuo.image.blur.directional
  - missing Vuo support: HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## FastBlur

- TiXL full path: `Lib.image.fx.blur.FastBlur`
- Namespace: `Lib.image.fx.blur`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/fx/blur/FastBlur.cs`
  - .t3 defaults: `Operators/Lib/image/fx/blur/FastBlur.t3`
  - docs: `.help/docs/operators/lib/image/fx/blur/FastBlur.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/blur/FastBlur-DownsamplePS.hlsl`, `Operators/Lib/Assets/shaders/img/blur/FastBlur-FullscreenVS.hlsl`, `Operators/Lib/Assets/shaders/img/blur/FastBlur-UpsampleAcculuatePS.hlsl`
- Purpose: Provides better quality and much faster speed that the [Blur] but does only allow radius control.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `Image`: Texture2D, default Unknown; semantic role Unknown
  - `MaxLevels`: int, default 5; The number of blur levels applied. The maximum is 12 (which should be enough for most resolutions).
In most scenarios you wouldn't adjust this, but in edge scenarios, it might help to optimize performance or craft special looks.
- Outputs:
  - `Result`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - important edge cases: Unknown beyond documented ports
- Observed graph usage:
  - common incoming nodes: Lib.image.fx._._ExecuteFastBlurPasses (1)
  - common outgoing nodes: Lib.image.fx.blur.FastBlur (2), Lib.image.fx.blur.FastBlur (1)
- Vuo mapping:
  - Vuo input types: Image -> VuoImage; MaxLevels -> VuoInteger / enum
  - Vuo output types: Result -> VuoImage
  - direct built-in Vuo equivalent, if any: vuo.image.blur (partial)
  - missing Vuo support: HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## Sharpen

- TiXL full path: `Lib.image.fx.blur.Sharpen`
- Namespace: `Lib.image.fx.blur`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/fx/blur/Sharpen.cs`
  - .t3 defaults: `Operators/Lib/image/fx/blur/Sharpen.t3`
  - docs: `.help/docs/operators/lib/image/fx/blur/Sharpen.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/fx/Sharpen.hlsl`
- Purpose: Sharpens the incoming image.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `Clamping`: bool, default False; Helps to get a smooth render for reaction diffusion
  - `Image`: Texture2D, default Unknown; semantic role Unknown
  - `SampleRadius`: float, default 1.0; Defines the radius of the sharpen effect.
For normal image sharpening, small values are recommended (~1-3), larger radii are useful for more creative applications and image stylization.
  - `Strength`: float, default 1.0; Defines how strongly the effect is applied.
- Outputs:
  - `TextureOutput`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - DX11/app-specific evidence: PixelShader
  - important edge cases: Unknown beyond documented ports
- Observed graph usage:
  - common incoming nodes: Lib.image.fx.blur.Blur (3), Lib.render._dx11.fxsetup._ImageFxShaderSetupStatic (1), Lib.image.generate.load.LoadImage (1), bb4e1d6e (1), Ease (1)
  - common outgoing nodes: Lib.image.fx.blur.Sharpen (3), Lib.image.fx.blur.Sharpen (2), Lib.image.fx.blur.Sharpen (1), Lib.image.fx.blur.Sharpen (1), Lib.image.fx.blur.Sharpen (1)
- Vuo mapping:
  - Vuo input types: Clamping -> VuoBoolean; Image -> VuoImage; SampleRadius -> VuoReal; Strength -> VuoReal
  - Vuo output types: TextureOutput -> VuoImage
  - direct built-in Vuo equivalent, if any: vuo.image.sharpen
  - missing Vuo support: HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## BubbleZoom

- TiXL full path: `Lib.image.fx.distort.BubbleZoom`
- Namespace: `Lib.image.fx.distort`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/fx/distort/BubbleZoom.cs`
  - .t3 defaults: `Operators/Lib/image/fx/distort/BubbleZoom.t3`
  - docs: `.help/docs/operators/lib/image/fx/distort/BubbleZoom.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/fx/BubbleZoom.hlsl`
- Purpose: An image effect that enlarges the inner circular region with a smooth edge.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor. TiXL Gradient has no direct Vuo type found in source scan; model as color/position lists or custom gradient helper.
- Inputs:
  - `Bias`: float, default 0.0; semantic role Unknown
  - `Center`: Vector2, default {'X': 0.0, 'Y': 0.0}; semantic role Unknown
  - `Feather`: float, default 1.0; semantic role Unknown
  - `FeatherGradient`: Gradient, default {'Gradient': {'Interpolation': 'Linear', 'Steps': [{'Id': '57bfcad7-d494-40c2-a1c5-0eb9bdf0cd3d', 'NormalizedPosition': 0.0, 'Color': {'R': 1.0, 'G': 0.99999, 'B': 1.0, 'A': 0.0}}, {'Id': '349fe2d5-1257-4141-8f17-842ee3d33833', 'NormalizedPosition': 1.0, 'Color': {'R': 0.0, 'G': 1.2159347e-11, 'B': 1e-06, 'A': 0.030000024}}]}}; semantic role Unknown
  - `FlipEffect`: float, default 0.0; semantic role Unknown
  - `GainAndBias`: Vector2, default {'X': 0.5, 'Y': 0.5}; semantic role Unknown
  - `Image`: Texture2D, default Unknown; semantic role Unknown
  - `Magnify`: float, default 1.25; semantic role Unknown
  - `Radius`: float, default 0.5; semantic role Unknown
  - `Resolution`: Vector.Int2, default {'X': 0, 'Y': 0}; semantic role Unknown
- Outputs:
  - `TextureOutput`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - DX11/app-specific evidence: RenderTarget
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: _multiImageFxSetupStatic (1), Lib.image.generate.load.LoadImage (1), Lib.numbers.anim.animators.OscillateVec2 (1), Lib.image.generate.basic.RenderTarget (1)
  - common outgoing nodes: Lib.image.fx.distort.BubbleZoom (6), Lib.image.fx.distort.BubbleZoom (2), Lib.image.fx.distort.BubbleZoom (1), Lib.image.fx.distort.BubbleZoom (1)
- Vuo mapping:
  - Vuo input types: Bias -> VuoReal; Center -> VuoPoint2d or integer pair; Feather -> VuoReal; FeatherGradient -> VuoList<VuoColor> + positions; FlipEffect -> VuoReal; GainAndBias -> VuoPoint2d or integer pair; Image -> VuoImage; Magnify -> VuoReal; Radius -> VuoReal; Resolution -> VuoPoint2d or integer pair
  - Vuo output types: TextureOutput -> VuoImage
  - direct built-in Vuo equivalent, if any: vuo.image.bulge2 (partial)
  - missing Vuo support: HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## ChromaticDistortion

- TiXL full path: `Lib.image.fx.distort.ChromaticDistortion`
- Namespace: `Lib.image.fx.distort`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/fx/distort/ChromaticDistortion.cs`
  - .t3 defaults: `Operators/Lib/image/fx/distort/ChromaticDistortion.t3`
  - docs: `.help/docs/operators/lib/image/fx/distort/ChromaticDistortion.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/fx/ChromaticDistortion.hlsl`
- Purpose: Simulates an imaging error of optical camera lenses that manifests itself as blurring or discoloration at the outer edges.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `Center`: Vector2, default {'X': 0.0, 'Y': 0.0}; Offsets the center of the effect.
  - `Colorize`: float, default 0.1; Tints the image yellow.
  - `Distort`: float, default 0.1; Controls intensity of lens distortion.
  - `DistortOffset`: float, default 0.5; Uniformly scales the image to fix unwanted effects at the edges when using "distort" setting.
  - `SampleCount`: int, default 16; Controls the fidelity of the effect.
Higher numbers create smoother results at the cost of rendering speed.
  - `ScaleImage`: float, default 1.0; Uniformly scales the image without cutting the edges.
  - `Size`: float, default 0.05; Controls the intensity of the effect.
  - `Texture2d`: Texture2D, default Unknown; semantic role Unknown
- Outputs:
  - `Output`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - DX11/app-specific evidence: RenderTarget
  - important edge cases: Unknown beyond documented ports
- Observed graph usage:
  - common incoming nodes: Lib.image.color.ColorGrade (2), Lib.render._dx11.fxsetup._ImageFxShaderSetup2 (1), Lib.image.generate.load.LoadImage (1), Lib.image.fx.distort.ChromaticDistortion (1), Lib.image.generate.pattern.Raster (1)
  - common outgoing nodes: Lib.image.fx.distort.ChromaticDistortion (6), Lib.image.fx.distort.ChromaticDistortion (1), Lib.image.fx.distort.ChromaticDistortion (1), Lib.image.fx.distort.ChromaticDistortion (1), Lib.image.fx.distort.ChromaticDistortion (1)
- Vuo mapping:
  - Vuo input types: Center -> VuoPoint2d or integer pair; Colorize -> VuoReal; Distort -> VuoReal; DistortOffset -> VuoReal; SampleCount -> VuoInteger / enum; ScaleImage -> VuoReal; Size -> VuoReal; Texture2d -> VuoImage
  - Vuo output types: Output -> VuoImage
  - direct built-in Vuo equivalent, if any: vuo.image.color.offset.rgb / analogDistortion (partial)
  - missing Vuo support: HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## Displace

- TiXL full path: `Lib.image.fx.distort.Displace`
- Namespace: `Lib.image.fx.distort`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/fx/distort/Displace.cs`
  - .t3 defaults: `Operators/Lib/image/fx/distort/Displace.t3`
  - docs: `.help/docs/operators/lib/image/fx/distort/Displace.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/fx/Displace.hlsl`
- Purpose: *No description yet.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `DisplaceMap`: Texture2D, default Unknown; semantic role Unknown
  - `DisplaceMapOffset`: Vector2, default {'X': 0.0, 'Y': 0.0}; semantic role Unknown
  - `Displacement`: float, default 0.0; semantic role Unknown
  - `DisplacementOffset`: float, default 0.0; semantic role Unknown
  - `DisplaceMode`: int, default 0, enum: IntensityGradient, Intensity, NormalMap, SignedNormalMap; semantic role Unknown
  - `GenerateMips`: bool, default False; semantic role Unknown
  - `Image`: Texture2D, default Unknown; semantic role Unknown
  - `RGSS_4xAA`: bool, default False; semantic role Unknown
  - `SampleRadius`: float, default 1.0; semantic role Unknown
  - `Shade`: float, default 0.0; Darkens the effect
  - `TextureFiltering`: Direct3D11.Filter, default MinPointMagLinearMipPoint; semantic role Unknown
  - `Twist`: float, default 0.0; semantic role Unknown
  - `WrapMode`: Direct3D11.TextureAddressMode, default MirrorOnce; Defines if and how the image is repeated at its edge
- Outputs:
  - `Output`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - DX11/app-specific evidence: Direct3D11, TextureReference, RenderTarget, PixelShader, SharpDX
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: Lib.image.generate.basic.LinearGradient (21), Multiply (14), Lib.image.fx.stylize.Steps (6), Lib.image.generate.noise.FractalNoise (5), Lib.image.fx.distort.Displace (5)
  - common outgoing nodes: Lib.image.fx.distort.Displace (11), Lib.image.fx.distort.Displace (10), Lib.image.fx.distort.Displace (5), Lib.image.fx.distort.Displace (5), Lib.image.fx.distort.Displace (5)
- Vuo mapping:
  - Vuo input types: DisplaceMap -> VuoImage; DisplaceMapOffset -> VuoPoint2d or integer pair; Displacement -> VuoReal; DisplacementOffset -> VuoReal; DisplaceMode -> VuoInteger / enum; GenerateMips -> VuoBoolean; Image -> VuoImage; RGSS_4xAA -> VuoBoolean; SampleRadius -> VuoReal; Shade -> VuoReal
  - Vuo output types: Output -> VuoImage
  - direct built-in Vuo equivalent, if any: no direct displacement node; shader rewrite
  - missing Vuo support: direct built-in equivalent not found; HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; likely needs ISF/Metal/GLSL or Vuo-specific renderer design
- First implementation recommendation: Write an ISF/Metal/GLSL parity shader after freezing TiXL defaults and a fixture image.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## PolarCoordinates

- TiXL full path: `Lib.image.fx.distort.PolarCoordinates`
- Namespace: `Lib.image.fx.distort`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/fx/distort/PolarCoordinates.cs`
  - .t3 defaults: `Operators/Lib/image/fx/distort/PolarCoordinates.t3`
  - docs: `.help/docs/operators/lib/image/fx/distort/PolarCoordinates.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/fx/PolarCoordinates.hlsl`
- Purpose: Applies a polar coordinate transformation that converts between circular and rectangular coordinates.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `Center`: Vector2, default {'X': 0.0, 'Y': 0.0}; semantic role Unknown
  - `Image`: Texture2D, default Unknown; semantic role Unknown
  - `Mode`: int, default 0, enum: Cartesian2Polar, Polar2Cartesian; semantic role Unknown
  - `RadialBias`: float, default 1.0; semantic role Unknown
  - `RadialOffset`: float, default 0.0; semantic role Unknown
  - `Radius`: float, default 1.0; semantic role Unknown
  - `Resolution`: Int2, default {'X': 0, 'Y': 0}; semantic role Unknown
  - `Stretch`: Vector2, default {'X': 1.0, 'Y': 1.0}; semantic role Unknown
  - `Twist`: float, default 0.0; semantic role Unknown
- Outputs:
  - `TextureOutput`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - DX11/app-specific evidence: RenderTarget
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: Lib.image.generate.load.LoadImage (5), Lib.image.generate.basic.RenderTarget (2), Lib.render._dx11.fxsetup._ImageFxShaderSetupStatic (1), Lib.image.generate.basic.LinearGradient (1), Lib.image.color.AdjustColors (1)
  - common outgoing nodes: Lib.image.fx.distort.PolarCoordinates (6), Lib.image.fx.distort.PolarCoordinates (4), Lib.image.fx.distort.PolarCoordinates (4), Lib.image.fx.distort.PolarCoordinates (2), Lib.image.fx.distort.PolarCoordinates (1)
- Vuo mapping:
  - Vuo input types: Center -> VuoPoint2d or integer pair; Image -> VuoImage; Mode -> VuoInteger / enum; RadialBias -> VuoReal; RadialOffset -> VuoReal; Radius -> VuoReal; Resolution -> VuoPoint2d or integer pair; Stretch -> VuoPoint2d or integer pair; Twist -> VuoReal
  - Vuo output types: TextureOutput -> VuoImage
  - direct built-in Vuo equivalent, if any: no direct polar remap node found
  - missing Vuo support: direct built-in equivalent not found; HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; likely needs ISF/Metal/GLSL or Vuo-specific renderer design
- First implementation recommendation: Write an ISF/Metal/GLSL parity shader after freezing TiXL defaults and a fixture image.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## AdvancedFeedback

- TiXL full path: `Lib.image.fx.feedback.AdvancedFeedback`
- Namespace: `Lib.image.fx.feedback`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/fx/feedback/AdvancedFeedback.cs`
  - .t3 defaults: `Operators/Lib/image/fx/feedback/AdvancedFeedback.t3`
  - docs: `.help/docs/operators/lib/image/fx/feedback/AdvancedFeedback.md`
  - related shader / helper source: None found in C#/.t3 scan
- Purpose: An advanced version of the [FluidFeedback] effect is much more versatile but harder to control.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `AmplifyEdges`: float, default 0.141; semantic role Unknown
  - `BlurRadius`: float, default 4.0; semantic role Unknown
  - `Command`: Command, default Unknown; semantic role Unknown
  - `Displacement`: float, default 15.0; semantic role Unknown
  - `DisplaceOffset`: float, default 0.0; semantic role Unknown
  - `IsEnabled`: bool, default True; semantic role Unknown
  - `LimitBrights`: float, default 1.0; semantic role Unknown
  - `Offset`: Vector2, default {'X': 0.0, 'Y': 0.0}; semantic role Unknown
  - `Reset`: bool, default False; semantic role Unknown
  - `Rotate`: float, default 0.0; semantic role Unknown
  - `SampleDistance`: float, default 0.2; semantic role Unknown
  - `SampleRadius`: float, default 1.263; semantic role Unknown
  - `Shade`: float, default 0.39999998; semantic role Unknown
  - `ShiftBrightness`: float, default 0.0; semantic role Unknown
  - `ShiftHue`: float, default 0.29999998; semantic role Unknown
  - `ShiftSaturation`: float, default 0.0; semantic role Unknown
  - `Twist`: float, default -15.0; semantic role Unknown
  - `Zoom`: float, default 1.0; semantic role Unknown
- Outputs:
  - `ColorBuffer`: Texture2D, output image/data role
- Runtime behavior:
  - C# file mainly declares slots; `.t3` graph/defaults should be inspected for exact runtime composition. No shader file was found by the local scan.
  - DX11/app-specific evidence: RenderTarget, Command
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: Lib.render.basic.Layer2d (4), Lib.io.midi.MidiInput (2), Lib.render.transform.Group (2), Lib.image.generate.basic.RenderTarget (1), Lib.render.camera.Camera (1)
  - common outgoing nodes: Lib.image.fx.feedback.AdvancedFeedback (6), Lib.image.fx.feedback.AdvancedFeedback (6), Lib.image.fx.feedback.AdvancedFeedback (3), Lib.image.fx.feedback.AdvancedFeedback (3), Lib.image.fx.feedback.AdvancedFeedback (2)
- Vuo mapping:
  - Vuo input types: AmplifyEdges -> VuoReal; BlurRadius -> VuoReal; Command -> Unknown / custom; Displacement -> VuoReal; DisplaceOffset -> VuoReal; IsEnabled -> VuoBoolean; LimitBrights -> VuoReal; Offset -> VuoPoint2d or integer pair; Reset -> VuoBoolean; Rotate -> VuoReal
  - Vuo output types: ColorBuffer -> VuoImage
  - direct built-in Vuo equivalent, if any: vuo.image.feedback (partial)
  - missing Vuo support: mostly type/default parity and composition details
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## AfterGlow

- TiXL full path: `Lib.image.fx.feedback.AfterGlow`
- Namespace: `Lib.image.fx.feedback`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/fx/feedback/AfterGlow.cs`
  - .t3 defaults: `Operators/Lib/image/fx/feedback/AfterGlow.t3`
  - docs: `.help/docs/operators/lib/image/fx/feedback/AfterGlow.md`
  - related shader / helper source: None found in C#/.t3 scan
- Purpose: Creates an afterglow effect for moving images just like the newer version [AfterGlow2] Other Useful Ops for a PostFX Pipeline: [MotionBlur] [DepthOfField] [ChromaticAberration] [Glow] [Grain] [Blur] All Feedback Ops: [Fl...
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `BlurAmount`: float, default 0.1; semantic role Unknown
  - `Color`: Vector4, default {'X': 0.5928854, 'Y': 0.5928794, 'Z': 0.5928794, 'W': 1.0}; semantic role Unknown
  - `ContrastOffset2`: float, default -0.76; semantic role Unknown
  - `DecayRate`: float, default 0.015666667; semantic role Unknown
  - `GlowImpact`: float, default 0.7; semantic role Unknown
  - `Image`: Texture2D, default Unknown; semantic role Unknown
  - `Resolution`: Int2, default {'X': 0, 'Y': 0}; semantic role Unknown
- Outputs:
  - `CombinedLayers`: Command, output image/data role
  - `Output`: Texture2D, output image/data role
- Runtime behavior:
  - C# file mainly declares slots; `.t3` graph/defaults should be inspected for exact runtime composition. No shader file was found by the local scan.
  - DX11/app-specific evidence: RenderTarget, Command
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: Lib.flow.Execute (1), Lib.image.generate.basic.RenderTarget (1)
  - common outgoing nodes: Lib.image.fx.feedback.AfterGlow (5), Lib.image.fx.feedback.AfterGlow (2), Lib.image.fx.feedback.AfterGlow (1), Lib.image.fx.feedback.AfterGlow (1)
- Vuo mapping:
  - Vuo input types: BlurAmount -> VuoReal; Color -> VuoColor or VuoPoint4d; ContrastOffset2 -> VuoReal; DecayRate -> VuoReal; GlowImpact -> VuoReal; Image -> VuoImage; Resolution -> VuoPoint2d or integer pair
  - Vuo output types: CombinedLayers -> Unknown / custom; Output -> VuoImage
  - direct built-in Vuo equivalent, if any: vuo.image.feedback (partial)
  - missing Vuo support: mostly type/default parity and composition details
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## GlitchDisplace

- TiXL full path: `Lib.image.fx.glitch.GlitchDisplace`
- Namespace: `Lib.image.fx.glitch`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/fx/glitch/GlitchDisplace.cs`
  - .t3 defaults: `Operators/Lib/image/fx/glitch/GlitchDisplace.t3`
  - docs: `.help/docs/operators/lib/image/fx/glitch/GlitchDisplace.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/points/draw/GlitchDisplace.hlsl`
- Purpose: Takes the incoming image and applies an image effect that glitches and displaces parts of the image to mimic lossy signals, broken video files, codec glitches etc.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `Amount`: float, default 1.0; Defines the overall intensity of the effect
  - `Colorize`: Vector4, default {'X': 1.0, 'Y': 0.0, 'Z': 0.8917217, 'W': 1.0}; Defines a color that is added to the elements
  - `ColorRatio`: float, default 0.1; Defines the color intensity
  - `Columns`: int, default 25; Defines how many glitch bars are generated horizontally
  - `Image`: Texture2D, default Unknown; semantic role Unknown
  - `Mode`: int, default 3, enum: Static, Highlights, Shadows, EdgesLeft, EdgesRight; semantic role Unknown
  - `Offset`: Vector2, default {'X': 0.5, 'Y': 0.0}; Randomly offsets the bars
X = horizontally
Y = vertically
  - `OverridePoints`: BufferWithViews, default Unknown; semantic role Unknown
  - `Rows`: int, default 300; Defines how many glitch bars are generated vertically
  - `Scatter`: Vector2, default {'X': 0.1, 'Y': 0.1}; Randomly scatters the position of the glitch elements
  - `ScatterOffset`: Vector2, default {'X': 0.0, 'Y': 0.0}; semantic role Unknown
  - `ScatterStretch`: Vector2, default {'X': 0.0, 'Y': 0.0}; Defines the maximum distance of the scattering
  - `Seed`: int, default 0; Random seed for the glitch effect
  - `Size`: float, default 1.0; Scales the size of the glitch elements
High value can make this effect take a lot of resources
  - `Stretch`: Vector2, default {'X': 3.0, 'Y': 0.5}; Scales the glitching bars
X = width
Y = height
  - `Threshold`: float, default 0.0; Defines based on the method which part of the image the effect is applied to
- Outputs:
  - `Output2`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - DX11/app-specific evidence: RenderTarget
  - important edge cases: Unknown beyond documented ports
- Observed graph usage:
  - common incoming nodes: Lib.numbers.anim.animators.TriggerAnim (2), Lib.image.generate.basic.RenderTarget (1), d9431484 (1), Lib.image.generate.load.LoadImage (1), Lib.image.fx.stylize.Glow (1)
  - common outgoing nodes: Lib.image.fx.glitch.GlitchDisplace (5), Lib.image.fx.glitch.GlitchDisplace (4), Lib.image.fx.glitch.GlitchDisplace (2), Lib.image.fx.glitch.GlitchDisplace (2), Lib.image.fx.glitch.GlitchDisplace (2)
- Vuo mapping:
  - Vuo input types: Amount -> VuoReal; Colorize -> VuoColor or VuoPoint4d; ColorRatio -> VuoReal; Columns -> VuoInteger / enum; Image -> VuoImage; Mode -> VuoInteger / enum; Offset -> VuoPoint2d or integer pair; OverridePoints -> Unknown / custom; Rows -> VuoInteger / enum; Scatter -> VuoPoint2d or integer pair
  - Vuo output types: Output2 -> VuoImage
  - direct built-in Vuo equivalent, if any: no direct Vuo node
  - missing Vuo support: direct built-in equivalent not found; HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; likely needs ISF/Metal/GLSL or Vuo-specific renderer design
- First implementation recommendation: Write an ISF/Metal/GLSL parity shader after freezing TiXL defaults and a fixture image.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## RgbTV

- TiXL full path: `Lib.image.fx.glitch.RgbTV`
- Namespace: `Lib.image.fx.glitch`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/fx/glitch/RgbTV.cs`
  - .t3 defaults: `Operators/Lib/image/fx/glitch/RgbTV.t3`
  - docs: `.help/docs/operators/lib/image/fx/glitch/RgbTV.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/fx/Default2-vs.hlsl`, `Operators/Lib/Assets/shaders/img/fx/RgbTV.hlsl`
- Purpose: Creates a vintage TV-glitch effect.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `BlackLevel`: float, default -0.100000024; semantic role Unknown
  - `BlurImage`: float, default 0.0; semantic role Unknown
  - `Buldge`: float, default 0.15; semantic role Unknown
  - `Gaps`: float, default 0.03; semantic role Unknown
  - `GlitchAmount`: float, default 1.0; semantic role Unknown
  - `GlitchDistort`: float, default 1.0; semantic role Unknown
  - `GlitchFlicker`: float, default 0.0; semantic role Unknown
  - `GlitchTimeOverride`: float, default 0.0; semantic role Unknown
  - `GlowBlur`: float, default 0.8; semantic role Unknown
  - `GlowIntensity`: float, default 0.1; semantic role Unknown
  - `Image`: Texture2D, default Unknown; semantic role Unknown
  - `ImageBrightess`: float, default 0.5; semantic role Unknown
  - `ImageContrast`: float, default 1.0; semantic role Unknown
  - `Noise`: float, default 0.1; semantic role Unknown
  - `NoiseColorize`: float, default 0.5; semantic role Unknown
  - `NoiseExponent`: float, default 1.0; semantic role Unknown
  - `NoiseForDistortion`: float, default 20.0; semantic role Unknown
  - `NoiseSpeed`: float, default 10.0; semantic role Unknown
  - `PatternAmount`: float, default 0.2; semantic role Unknown
  - `PatternBlur`: Vector2, default {'X': 0.25, 'Y': 0.25}; semantic role Unknown
  - `PatternSize`: float, default 0.025; semantic role Unknown
  - `Resolution`: Int2, default {'X': 0, 'Y': 0}; semantic role Unknown
  - `ShadeDistortion`: float, default 2.0; semantic role Unknown
  - `ShiftColumns`: float, default 0.5; semantic role Unknown
  - `Vignette`: float, default 1.0; semantic role Unknown
  - `Visibility`: float, default 1.0; semantic role Unknown
- Outputs:
  - `TextureOutput`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - DX11/app-specific evidence: RenderTarget
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: Lib.image.generate.basic.RenderTarget (2), TVIntroSceneSetup (2), TVIntroScene (1), Multiply (1)
  - common outgoing nodes: Lib.image.fx.glitch.RgbTV (20), Lib.image.fx.glitch.RgbTV (2), Lib.image.fx.glitch.RgbTV (2), Lib.image.fx.glitch.RgbTV (2), Lib.image.fx.glitch.RgbTV (1)
- Vuo mapping:
  - Vuo input types: BlackLevel -> VuoReal; BlurImage -> VuoReal; Buldge -> VuoReal; Gaps -> VuoReal; GlitchAmount -> VuoReal; GlitchDistort -> VuoReal; GlitchFlicker -> VuoReal; GlitchTimeOverride -> VuoReal; GlowBlur -> VuoReal; GlowIntensity -> VuoReal
  - Vuo output types: TextureOutput -> VuoImage
  - direct built-in Vuo equivalent, if any: vuo.image.analogDistortion / color.offset.rgb (partial)
  - missing Vuo support: HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## SortPixelGlitch

- TiXL full path: `Lib.image.fx.glitch.SortPixelGlitch`
- Namespace: `Lib.image.fx.glitch`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/fx/glitch/SortPixelGlitch.cs`
  - .t3 defaults: `Operators/Lib/image/fx/glitch/SortPixelGlitch.t3`
  - docs: `.help/docs/operators/lib/image/fx/glitch/SortPixelGlitch.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/fx/Default2-vs.hlsl`, `Operators/Lib/Assets/shaders/img/fx/PixelSortGlitch-convert-buffer-to-texture2d-ps.hlsl`, `Operators/Lib/Assets/shaders/img/fx/SortPixelsGlitch-cs.hlsl`
- Purpose: An interesting image glitch effect.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `AddGrain`: float, default 0.0; semantic role Unknown
  - `BackgroundColor`: Vector4, default {'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}; semantic role Unknown
  - `Extend`: float, default 0.0; semantic role Unknown
  - `FadeStreaks`: float, default 0.0; semantic role Unknown
  - `GradientBias`: float, default 0.75; semantic role Unknown
  - `LumaBias`: float, default 0.0; semantic role Unknown
  - `MaxSteps`: float, default 2000.0; semantic role Unknown
  - `Offset`: float, default 0.0; semantic role Unknown
  - `ScanHighlights`: bool, default False; semantic role Unknown
  - `ScatterOffset`: float, default 0.0; semantic role Unknown
  - `ScatterThreshold`: float, default 0.0; semantic role Unknown
  - `StreakColor`: Vector4, default {'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}; semantic role Unknown
  - `Texture2d`: Texture2D, default Unknown; semantic role Unknown
  - `Threshold`: float, default 0.0; semantic role Unknown
  - `Vertical`: bool, default True; semantic role Unknown
- Outputs:
  - `Output`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - DX11/app-specific evidence: RenderTarget
  - important edge cases: Unknown beyond documented ports
- Observed graph usage:
  - common incoming nodes: Lib.image.generate.basic.RenderTarget (4), Lib.numbers.anim._obsolete._AnimValueOld (2), Lib.image.color.AdjustColors (2), Lib.numbers.anim.animators.TriggerAnim (2), Lib.image.generate.load.LoadImage (1)
  - common outgoing nodes: Lib.image.fx.glitch.SortPixelGlitch (10), Lib.image.fx.glitch.SortPixelGlitch (5), Lib.image.fx.glitch.SortPixelGlitch (3), Lib.image.fx.glitch.SortPixelGlitch (2), Lib.image.fx.glitch.SortPixelGlitch (1)
- Vuo mapping:
  - Vuo input types: AddGrain -> VuoReal; BackgroundColor -> VuoColor or VuoPoint4d; Extend -> VuoReal; FadeStreaks -> VuoReal; GradientBias -> VuoReal; LumaBias -> VuoReal; MaxSteps -> VuoReal; Offset -> VuoReal; ScanHighlights -> VuoBoolean; ScatterOffset -> VuoReal
  - Vuo output types: Output -> VuoImage
  - direct built-in Vuo equivalent, if any: no direct pixel-sort node
  - missing Vuo support: direct built-in equivalent not found; HLSL must be translated or replaced; first-pass D dependency must be designed away or postponed
- Porting grade:
  - D: internal/obsolete/DX11-heavy first-pass document-only node; DX11/app terms: RenderTarget; shader/compute dependency
- First implementation recommendation: Document and postpone until renderer/state/DX11 replacement strategy is chosen.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## AsciiRender

- TiXL full path: `Lib.image.fx.stylize.AsciiRender`
- Namespace: `Lib.image.fx.stylize`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/fx/stylize/AsciiRender.cs`
  - .t3 defaults: `Operators/Lib/image/fx/stylize/AsciiRender.t3`
  - docs: `.help/docs/operators/lib/image/fx/stylize/AsciiRender.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/fx/AsciiRender.hlsl`, `Operators/Lib/Assets/shaders/img/fx/Default2-vs.hlsl`
- Purpose: Draws the incoming image as shaded ASCII characters, similar to 'The Matrix'.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `Background`: Vector4, default {'X': 1e-06, 'Y': 9.9999e-07, 'Z': 9.9999e-07, 'W': 1.0}; semantic role Unknown
  - `Fill`: Vector4, default {'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}; semantic role Unknown
  - `FilterCharacters`: string, default ; semantic role Unknown
  - `FontCharSize`: Vector.Int2, default {'X': 6, 'Y': 6}; semantic role Unknown
  - `FontFilePath`: string, default Lib:images/font-6x6px.png; semantic role Unknown
  - `GainAndBias`: Vector2, default {'X': 0.5, 'Y': 0.5}; semantic role Unknown
  - `GenerateMips`: bool, default False; semantic role Unknown
  - `ImageA`: Texture2D, default Unknown; semantic role Unknown
  - `MixInColors`: float, default 0.0; semantic role Unknown
  - `Offset`: Vector2, default {'X': 0.0, 'Y': 0.0}; semantic role Unknown
  - `Randomize`: float, default 0.0; semantic role Unknown
  - `Resolution`: Vector.Int2, default {'X': 0, 'Y': 0}; semantic role Unknown
  - `ScaleFactor`: float, default 3.0; semantic role Unknown
- Outputs:
  - `Output`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - DX11/app-specific evidence: RenderTarget
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: Lib.image.fx.blur.Blur (2), Lib.image.generate.basic.RenderTarget (1), Lib.numbers.anim.animators.OscillateVec2 (1), Lib.image.generate.load.LoadImage (1), ebfeac4e (1)
  - common outgoing nodes: Lib.image.fx.stylize.AsciiRender (3), Lib.image.fx.stylize.AsciiRender (2), Lib.image.fx.stylize.AsciiRender (2), Lib.image.fx.stylize.AsciiRender (2), Lib.image.fx.stylize.AsciiRender (2)
- Vuo mapping:
  - Vuo input types: Background -> VuoColor or VuoPoint4d; Fill -> VuoColor or VuoPoint4d; FilterCharacters -> Unknown / custom; FontCharSize -> VuoPoint2d or integer pair; FontFilePath -> Unknown / custom; GainAndBias -> VuoPoint2d or integer pair; GenerateMips -> VuoBoolean; ImageA -> VuoImage; MixInColors -> VuoReal; Offset -> VuoPoint2d or integer pair
  - Vuo output types: Output -> VuoImage
  - direct built-in Vuo equivalent, if any: no direct Vuo node
  - missing Vuo support: direct built-in equivalent not found; HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; likely needs ISF/Metal/GLSL or Vuo-specific renderer design
- First implementation recommendation: Write an ISF/Metal/GLSL parity shader after freezing TiXL defaults and a fixture image.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## DetectEdges

- TiXL full path: `Lib.image.fx.stylize.DetectEdges`
- Namespace: `Lib.image.fx.stylize`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/fx/stylize/DetectEdges.cs`
  - .t3 defaults: `Operators/Lib/image/fx/stylize/DetectEdges.t3`
  - docs: `.help/docs/operators/lib/image/fx/stylize/DetectEdges.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/fx/DetectEdges.hlsl`
- Purpose: Detects edges in the incoming image.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `Color`: Vector4, default {'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}; semantic role Unknown
  - `Contrast`: float, default 0.0; semantic role Unknown
  - `Image`: Texture2D, default Unknown; semantic role Unknown
  - `MixOriginal`: float, default 0.0; semantic role Unknown
  - `OutputAsTransparent`: bool, default False; semantic role Unknown
  - `SampleRadius`: float, default 1.0; semantic role Unknown
  - `Strength`: float, default 1.0; semantic role Unknown
- Outputs:
  - `TextureOutput`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - DX11/app-specific evidence: RenderTarget
  - important edge cases: Unknown beyond documented ports
- Observed graph usage:
  - common incoming nodes: Lib.image.generate.basic.RenderTarget (5), Lib.image.fx.feedback.AdvancedFeedback (3), Lib.image.generate.load.LoadImage (2), Lib.render._dx11.fxsetup._ImageFxShaderSetupStatic (1), Lib.image.fx.feedback.AfterGlow2 (1)
  - common outgoing nodes: Lib.image.fx.stylize.DetectEdges (5), Lib.image.fx.stylize.DetectEdges (3), Lib.image.fx.stylize.DetectEdges (2), Lib.image.fx.stylize.DetectEdges (1), Lib.image.fx.stylize.DetectEdges (1)
- Vuo mapping:
  - Vuo input types: Color -> VuoColor or VuoPoint4d; Contrast -> VuoReal; Image -> VuoImage; MixOriginal -> VuoReal; OutputAsTransparent -> VuoBoolean; SampleRadius -> VuoReal; Strength -> VuoReal
  - Vuo output types: TextureOutput -> VuoImage
  - direct built-in Vuo equivalent, if any: vuo.image.outline (partial)
  - missing Vuo support: HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## Dither

- TiXL full path: `Lib.image.fx.stylize.Dither`
- Namespace: `Lib.image.fx.stylize`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/fx/stylize/Dither.cs`
  - .t3 defaults: `Operators/Lib/image/fx/stylize/Dither.t3`
  - docs: `.help/docs/operators/lib/image/fx/stylize/Dither.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/fx/Dither.hlsl`
- Purpose: Applies Floyd-Steinberg dithering to an image to convert it to black and white colors.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `BlendMethod`: int, default 0; semantic role Unknown
  - `GainAndBias`: Vector2, default {'X': 0.5, 'Y': 0.5}; semantic role Unknown
  - `GrayScaleWeights`: Vector4, default {'X': 0.2126, 'Y': 0.7152, 'Z': 0.0722, 'W': 0.0}; Defines which color channels are used to determine the light-dark distinction.
  - `HighlightColor`: Vector4, default {'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}; Color selection for all bright pixels.
  - `Image`: Texture2D, default Unknown; semantic role Unknown
  - `Method`: int, default 0, enum: FloydSteinberg, Diffusion; Setting is currently ignored.
  - `Offset`: Vector2, default {'X': 0.0, 'Y': 0.0}; semantic role Unknown
  - `Scale`: float, default 4.0; Uniformly scales the size of the effect.
  - `ShadowColor`: Vector4, default {'X': 0.0, 'Y': 0.0, 'Z': 0.0, 'W': 1.0}; Color selection for all dark pixels.
- Outputs:
  - `TextureOutput`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - important edge cases: Unknown beyond documented ports
- Observed graph usage:
  - common incoming nodes: Lib.render._dx11.fxsetup._ImageFxShaderSetupStatic (1), Lib.image.generate.load.LoadImage (1)
  - common outgoing nodes: Lib.image.fx.stylize.Dither (3), Lib.image.fx.stylize.Dither (2), Lib.image.fx.stylize.Dither (2), Lib.image.fx.stylize.Dither (2), Lib.image.fx.stylize.Dither (1)
- Vuo mapping:
  - Vuo input types: BlendMethod -> VuoInteger / enum; GainAndBias -> VuoPoint2d or integer pair; GrayScaleWeights -> VuoColor or VuoPoint4d; HighlightColor -> VuoColor or VuoPoint4d; Image -> VuoImage; Method -> VuoInteger / enum; Offset -> VuoPoint2d or integer pair; Scale -> VuoReal; ShadowColor -> VuoColor or VuoPoint4d
  - Vuo output types: TextureOutput -> VuoImage
  - direct built-in Vuo equivalent, if any: vuo.image.color.palette (partial)
  - missing Vuo support: HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## Glow

- TiXL full path: `Lib.image.fx.stylize.Glow`
- Namespace: `Lib.image.fx.stylize`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/fx/stylize/Glow.cs`
  - .t3 defaults: `Operators/Lib/image/fx/stylize/Glow.t3`
  - docs: `.help/docs/operators/lib/image/fx/stylize/Glow.md`
  - related shader / helper source: None found in C#/.t3 scan
- Purpose: Adds a glow effect to the incoming image.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `AmplifyCore`: float, default 0.0; Further controls the intensity of the glow effect.
  - `BlendMode`: int, default 1; Selects how the effect is blended into the image.
  - `Color`: Vector4, default {'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}; Colorizes the Bloom effect.
  - `Intensity`: float, default 0.5; Controls the intensity of the bloom effect.
  - `Radius`: float, default 0.5; Controls how much the glow effect is blurred.
When heavily blurred, the effect consequently loses intensity.
  - `Samples`: float, default 12.0; Controls the fidelity of the effect.
Higher numbers create smoother results at the cost of rendering speed.
  - `Texture`: Texture2D, default Unknown; semantic role Unknown
  - `Threshold`: float, default 0.0; Controls the brightness of the image and helps to control which areas of the image are included in the effect.
- Outputs:
  - `ImgOutput`: Texture2D, output image/data role
  - `Output`: Command, output image/data role
- Runtime behavior:
  - C# file mainly declares slots; `.t3` graph/defaults should be inspected for exact runtime composition. No shader file was found by the local scan.
  - DX11/app-specific evidence: RenderTarget, Command
  - important edge cases: Unknown beyond documented ports
- Observed graph usage:
  - common incoming nodes: Lib.image.generate.basic.RenderTarget (30), Lib.render.postfx.DepthOfField (16), Lib.image.color.ColorGrade (5), Lib.image.transform.MirrorRepeat (5), Lib.render.postfx.SSAO (4)
  - common outgoing nodes: Lib.image.fx.stylize.Glow (17), Lib.image.fx.stylize.Glow (15), Lib.image.fx.stylize.Glow (7), Lib.image.fx.stylize.Glow (6), Lib.image.fx.stylize.Glow (6)
- Vuo mapping:
  - Vuo input types: AmplifyCore -> VuoReal; BlendMode -> VuoInteger / enum; Color -> VuoColor or VuoPoint4d; Intensity -> VuoReal; Radius -> VuoReal; Samples -> VuoReal; Texture -> VuoImage; Threshold -> VuoReal
  - Vuo output types: ImgOutput -> VuoImage; Output -> Unknown / custom
  - direct built-in Vuo equivalent, if any: no direct glow; compose blur/blend
  - missing Vuo support: direct built-in equivalent not found
- Porting grade:
  - C: image/shader node; likely needs ISF/Metal/GLSL or Vuo-specific renderer design
- First implementation recommendation: Write an ISF/Metal/GLSL parity shader after freezing TiXL defaults and a fixture image.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## Pixelate

- TiXL full path: `Lib.image.fx.stylize.Pixelate`
- Namespace: `Lib.image.fx.stylize`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/fx/stylize/Pixelate.cs`
  - .t3 defaults: `Operators/Lib/image/fx/stylize/Pixelate.t3`
  - docs: `.help/docs/operators/lib/image/fx/stylize/Pixelate.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/fx/Pixelate.hlsl`
- Purpose: TilesAmount parameter works only if Divisor = 0.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `Color`: Vector4, default {'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}; Multiplier color applied to the final output
  - `Divisor`: int, default 0; Sets the size of the tiles according to the source resolution
  - `Image`: Texture2D, default Unknown; semantic role Unknown
  - `Shape`: Texture2D, default Unknown; Customize the tile, could be used to
  - `TileAmount`: Int2, default {'X': 160, 'Y': 90}; Set X and Y resolution (ignored if Divisor is greater than 0)
- Outputs:
  - `TextureOutput`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - DX11/app-specific evidence: RenderTarget
  - important edge cases: Unknown beyond documented ports
- Observed graph usage:
  - common incoming nodes: Lib.image.generate.basic.LinearGradient (2), _multiImageFxSetupStatic (1), Lib.image.generate.noise.FractalNoise (1), 43c5fedf (1), Lib.image.use.Blend (1)
  - common outgoing nodes: Lib.image.fx.stylize.Pixelate (2), Lib.image.fx.stylize.Pixelate (1), Lib.image.fx.stylize.Pixelate (1), Lib.image.fx.stylize.Pixelate (1), Lib.image.fx.stylize.Pixelate (1)
- Vuo mapping:
  - Vuo input types: Color -> VuoColor or VuoPoint4d; Divisor -> VuoInteger / enum; Image -> VuoImage; Shape -> VuoImage; TileAmount -> VuoPoint2d or integer pair
  - Vuo output types: TextureOutput -> VuoImage
  - direct built-in Vuo equivalent, if any: vuo.image.pixellate
  - missing Vuo support: HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## Blob

- TiXL full path: `Lib.image.generate.basic.Blob`
- Namespace: `Lib.image.generate.basic`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/generate/basic/Blob.cs`
  - .t3 defaults: `Operators/Lib/image/generate/basic/Blob.t3`
  - docs: `.help/docs/operators/lib/image/generate/basic/Blob.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/generate/Blob.hlsl`
- Purpose: Generates ellipses, circles, blobs, vignettes, and similar shapes.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `Background`: Vector4, default {'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 0.0}; Background color of the blob.
  - `BlendMode`: int, default 0; Blend mode between the generated blob graphic and the background image, if one is provided.
  - `Color`: Vector4, default {'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}; Fill color of the blob.
  - `Feather`: float, default 1.0; Feather edges to reduce pixel artifacts.
Can also be used to blur the blob.
Set to a negative value to create a vignette.
  - `FeatherBias`: float, default 0.0; Weights the feathering towards one edge or the other of the blurred region.
  - `GenerateMips`: bool, default False; Generate mipmaps (scaled-down versions of this image for use in situations where many small copies are shown on screen.)
Will increase memory usage.
  - `Image`: Texture2D, default Unknown; Image to use as a background for the blob.
Drawn behind the background color.
  - `Position`: Vector2, default {'X': 0.0, 'Y': 0.0}; X/Y position, in relative units.
  - `Resolution`: Vector.Int2, default {'X': 0, 'Y': 0}; Output resolution in pixels. Set to 0 for dynamic resolution.
  - `Rotate`: float, default 0.0; Rotation amount in degrees.
Rotation is applied after Stretch and Scale, but before Position.
  - `Scale`: float, default 0.5; Scales the blob evenly.
  - `Stretch`: Vector2, default {'X': 1.0, 'Y': 1.0}; Stretches the blob unevenly.
  - `TextureFormat`: DXGI.Format, default R16G16B16A16_Float; semantic role Unknown
- Outputs:
  - `TextureOutput`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - DX11/app-specific evidence: PixelShader, DXGI.Format, SharpDX
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: Lib.image.generate.basic.Blob (8), Lib.image.generate.basic.RoundedRect (6), Lib.image.generate.load.LoadImage (2), Lib.numbers.vec4.RgbaToColor (2), Lib.render._dx11.fxsetup._ImageFxShaderSetupStatic (1)
  - common outgoing nodes: Lib.image.generate.basic.Blob (9), Lib.image.generate.basic.Blob (8), Lib.image.generate.basic.Blob (7), Lib.image.generate.basic.Blob (5), Lib.image.generate.basic.Blob (4)
- Vuo mapping:
  - Vuo input types: Background -> VuoColor or VuoPoint4d; BlendMode -> VuoInteger / enum; Color -> VuoColor or VuoPoint4d; Feather -> VuoReal; FeatherBias -> VuoReal; GenerateMips -> VuoBoolean; Image -> VuoImage; Position -> VuoPoint2d or integer pair; Resolution -> VuoPoint2d or integer pair; Rotate -> VuoReal
  - Vuo output types: TextureOutput -> VuoImage
  - direct built-in Vuo equivalent, if any: vuo.image.make.noise / shader (partial)
  - missing Vuo support: HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## BoxGradient

- TiXL full path: `Lib.image.generate.basic.BoxGradient`
- Namespace: `Lib.image.generate.basic`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/generate/basic/BoxGradient.cs`
  - .t3 defaults: `Operators/Lib/image/generate/basic/BoxGradient.t3`
  - docs: `.help/docs/operators/lib/image/generate/basic/BoxGradient.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/generate/BoxGradient.hlsl`
- Purpose: A box gradient using the signed distance field "Box - Exact" described in the "2D distance functions" article on Inigo Quilez's website.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor. TiXL Gradient has no direct Vuo type found in source scan; model as color/position lists or custom gradient helper.
- Inputs:
  - `BlendMode`: int, default 0; semantic role Unknown
  - `Center`: Vector2, default {'X': 0.0, 'Y': 0.0}; semantic role Unknown
  - `CornersRadius`: Vector4, default {'X': 0.0, 'Y': 0.0, 'Z': 0.0, 'W': 0.0}; top left, top right, bottom right, bottom left
  - `GainAndBias`: Vector2, default {'X': 0.5, 'Y': 0.5}; semantic role Unknown
  - `Gradient`: Gradient, default {'Gradient': {'Interpolation': 'Linear', 'Steps': [{'Id': '8e4f8a75-f14c-47f7-8328-883bfb1b3cfa', 'NormalizedPosition': 0.0, 'Color': {'R': 1.0, 'G': 0.99999, 'B': 1.0, 'A': 1.0}}, {'Id': 'd2a66e2d-8d83-4e2a-9e6c-838669725ac9', 'NormalizedPosition': 0.5, 'Color': {'R': 0.0, 'G': 1.2159347e-11, 'B': 1e-06, 'A': 1.0}}]}}; semantic role Unknown
  - `GradientWidth`: float, default 1.0; semantic role Unknown
  - `Image`: Texture2D, default Unknown; semantic role Unknown
  - `Offset`: float, default 0.0; semantic role Unknown
  - `PingPong`: bool, default True; semantic role Unknown
  - `Repeat`: bool, default False; semantic role Unknown
  - `Resolution`: Int2, default {'X': 0, 'Y': 0}; semantic role Unknown
  - `Rotation`: float, default 0.0; semantic role Unknown
  - `Size`: Vector2, default {'X': 0.25, 'Y': 0.25}; semantic role Unknown
  - `UniformScale`: float, default 1.0; semantic role Unknown
- Outputs:
  - `TextureOutput`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - DX11/app-specific evidence: RenderTarget
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: Lib.numbers.color.BlendGradients (2), _multiImageFxSetupStatic (1), Lib.image.generate.basic.RenderTarget (1), dd011e87 (1)
  - common outgoing nodes: Lib.image.generate.basic.BoxGradient (6), Lib.image.generate.basic.BoxGradient (4), Lib.image.generate.basic.BoxGradient (3), Lib.image.generate.basic.BoxGradient (2), Lib.image.generate.basic.BoxGradient (1)
- Vuo mapping:
  - Vuo input types: BlendMode -> VuoInteger / enum; Center -> VuoPoint2d or integer pair; CornersRadius -> VuoColor or VuoPoint4d; GainAndBias -> VuoPoint2d or integer pair; Gradient -> VuoList<VuoColor> + positions; GradientWidth -> VuoReal; Image -> VuoImage; Offset -> VuoReal; PingPong -> VuoBoolean; Repeat -> VuoBoolean
  - Vuo output types: TextureOutput -> VuoImage
  - direct built-in Vuo equivalent, if any: vuo.image.make.gradient.linear/radial (partial)
  - missing Vuo support: HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## CheckerBoard

- TiXL full path: `Lib.image.generate.basic.CheckerBoard`
- Namespace: `Lib.image.generate.basic`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/generate/basic/CheckerBoard.cs`
  - .t3 defaults: `Operators/Lib/image/generate/basic/CheckerBoard.t3`
  - docs: `.help/docs/operators/lib/image/generate/basic/CheckerBoard.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/generate/CheckerBoard.hlsl`
- Purpose: Generates a modular chessboard pattern Other interesting patterns can be generated with [SinForm] [ZollnerPattern] [FraserGrid] [Raster] [CheckerBoard] [RyojiPattern2] [RyojiPattern1]
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `ColorA`: Vector4, default {'X': 0.20212764, 'Y': 0.20212561, 'Z': 0.20212561, 'W': 1.0}; semantic role Unknown
  - `ColorB`: Vector4, default {'X': 0.12056738, 'Y': 0.120566174, 'Z': 0.120566174, 'W': 1.0}; semantic role Unknown
  - `GenerateMips`: bool, default False; semantic role Unknown
  - `Offset`: Vector2, default {'X': 0.0, 'Y': 0.0}; semantic role Unknown
  - `Resolution`: Int2, default {'X': 0, 'Y': 0}; semantic role Unknown
  - `Scale`: float, default 1.0; semantic role Unknown
  - `Stretch`: Vector2, default {'X': 1.0, 'Y': 1.0}; semantic role Unknown
  - `UseAspectRatio`: bool, default True; semantic role Unknown
- Outputs:
  - `TextureOutput`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - DX11/app-specific evidence: PixelShader
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: Lib.numbers.anim.animators.OscillateVec2 (2), 74623f0f (2), scale (2), Lib.render._dx11.fxsetup._ImageFxShaderSetupStatic (1), 2f33fda1 (1)
  - common outgoing nodes: Lib.image.generate.basic.CheckerBoard (8), Lib.image.generate.basic.CheckerBoard (7), Lib.image.generate.basic.CheckerBoard (3), Lib.image.generate.basic.CheckerBoard (2), Lib.image.generate.basic.CheckerBoard (2)
- Vuo mapping:
  - Vuo input types: ColorA -> VuoColor or VuoPoint4d; ColorB -> VuoColor or VuoPoint4d; GenerateMips -> VuoBoolean; Offset -> VuoPoint2d or integer pair; Resolution -> VuoPoint2d or integer pair; Scale -> VuoReal; Stretch -> VuoPoint2d or integer pair; UseAspectRatio -> VuoBoolean
  - Vuo output types: TextureOutput -> VuoImage
  - direct built-in Vuo equivalent, if any: vuo.image.make.checkerboard
  - missing Vuo support: HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## LinearGradient

- TiXL full path: `Lib.image.generate.basic.LinearGradient`
- Namespace: `Lib.image.generate.basic`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/generate/basic/LinearGradient.cs`
  - .t3 defaults: `Operators/Lib/image/generate/basic/LinearGradient.t3`
  - docs: `.help/docs/operators/lib/image/generate/basic/LinearGradient.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/generate/LinearGradient.hlsl`
- Purpose: Renders a linear color ramp defined by a gradient.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor. TiXL Gradient has no direct Vuo type found in source scan; model as color/position lists or custom gradient helper.
- Inputs:
  - `BlendMode`: int, default 0; semantic role Unknown
  - `Center`: Vector2, default {'X': 0.0, 'Y': 0.0}; semantic role Unknown
  - `GainAndBias`: Vector2, default {'X': 0.5, 'Y': 0.5}; semantic role Unknown
  - `GenerateMips`: bool, default False; semantic role Unknown
  - `Gradient`: Gradient, default {'Gradient': {'Interpolation': 'Linear', 'Steps': [{'Id': '034c8b5b-5c92-439f-b5a6-28e721df9492', 'NormalizedPosition': 0.0, 'Color': {'R': 0.0, 'G': 1.2159347e-11, 'B': 1e-06, 'A': 1.0}}, {'Id': '0c357289-d7c4-4a05-86ea-4cc7debde848', 'NormalizedPosition': 1.0, 'Color': {'R': 1.0, 'G': 0.99999, 'B': 1.0, 'A': 1.0}}]}}; semantic role Unknown
  - `Image`: Texture2D, default Unknown; semantic role Unknown
  - `Offset`: float, default 0.0; semantic role Unknown
  - `OffsetMode`: int, default 0, enum: RelativeToImage, RelativeToSize; Renders a linear color ramp defined by a gradient. This can be very useful in combination with [Steps].
Similar Ops: [NGon] [RoundedRect] [Rings] [RadialGradient] [LinearGradient] [Blob]
  - `PingPong`: bool, default False; semantic role Unknown
  - `Repeat`: bool, default False; semantic role Unknown
  - `Resolution`: Vector.Int2, default {'X': 0, 'Y': 0}; semantic role Unknown
  - `Rotate`: float, default 90.0; semantic role Unknown
  - `SizeMode`: int, default 0, enum: AlignToHeight, AlignToWidth; semantic role Unknown
  - `TextureFormat`: DXGI.Format, default R16G16B16A16_Float; semantic role Unknown
  - `Width`: float, default 1.0; semantic role Unknown
- Outputs:
  - `TextureOutput`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - DX11/app-specific evidence: RenderTarget, DXGI.Format, SharpDX
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: Lib.numbers.anim._obsolete.Counter (8), Lib.numbers.color.BuildGradient (7), Lib.numbers.anim._obsolete._Time_old (7), Lib.numbers.anim.time.Time (5), Lib.image.transform.MakeTileableImageAdvanced (4)
  - common outgoing nodes: Lib.image.generate.basic.LinearGradient (27), Lib.image.generate.basic.LinearGradient (21), Lib.image.generate.basic.LinearGradient (18), Lib.image.generate.basic.LinearGradient (18), Lib.image.generate.basic.LinearGradient (14)
- Vuo mapping:
  - Vuo input types: BlendMode -> VuoInteger / enum; Center -> VuoPoint2d or integer pair; GainAndBias -> VuoPoint2d or integer pair; GenerateMips -> VuoBoolean; Gradient -> VuoList<VuoColor> + positions; Image -> VuoImage; Offset -> VuoReal; OffsetMode -> VuoInteger / enum; PingPong -> VuoBoolean; Repeat -> VuoBoolean
  - Vuo output types: TextureOutput -> VuoImage
  - direct built-in Vuo equivalent, if any: vuo.image.make.gradient.linear
  - missing Vuo support: HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## NGon

- TiXL full path: `Lib.image.generate.basic.NGon`
- Namespace: `Lib.image.generate.basic`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/generate/basic/NGon.cs`
  - .t3 defaults: `Operators/Lib/image/generate/basic/NGon.t3`
  - docs: `.help/docs/operators/lib/image/generate/basic/NGon.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/generate/NGon.hlsl`
- Purpose: Renders a polygon shape similar to [Blob] or [RoundedRect].
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `Background`: Vector4, default {'X': 0.0, 'Y': 0.0, 'Z': 0.0, 'W': 0.0}; semantic role Unknown
  - `Blades`: float, default 0.0; Offsets every second edge to create sharp corners
  - `BlendMode`: int, default 0; Blend mode between the generated blob graphic and the background image, if one is provided.
  - `Curvature`: float, default 0.0; Defines if the outer line is convex or concave and can also invert the shape
  - `Feather`: float, default 0.05; Defines how sharp the outer edge of the object is rendered
  - `FeatherBias`: float, default 0.0; Defines the bias for the feather
  - `Fill`: Vector4, default {'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}; semantic role Unknown
  - `Image`: Texture2D, default Unknown; semantic role Unknown
  - `Position`: Vector2, default {'X': 0.0, 'Y': 0.0}; X/Y position, in relative units.
  - `Radius`: float, default 0.25; Defines the radius
  - `Resolution`: Int2, default {'X': 0, 'Y': 0}; Output resolution in pixels. Set to 0 for dynamic resolution.
  - `Rotate`: float, default -90.0; Rotates the object around its center
  - `Round`: float, default 0.0; Smoothes the whole shape
  - `Sides`: float, default 3.0; Defines the amount of sides
- Outputs:
  - `TextureOutput`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: Lib.render._dx11.fxsetup._ImageFxShaderSetupStatic (1)
  - common outgoing nodes: Lib.image.generate.basic.NGon (11), Lib.image.generate.basic.NGon (7), Lib.image.generate.basic.NGon (2), Lib.image.generate.basic.NGon (2), Lib.image.generate.basic.NGon (2)
- Vuo mapping:
  - Vuo input types: Background -> VuoColor or VuoPoint4d; Blades -> VuoReal; BlendMode -> VuoInteger / enum; Curvature -> VuoReal; Feather -> VuoReal; FeatherBias -> VuoReal; Fill -> VuoColor or VuoPoint4d; Image -> VuoImage; Position -> VuoPoint2d or integer pair; Radius -> VuoReal
  - Vuo output types: TextureOutput -> VuoImage
  - direct built-in Vuo equivalent, if any: vuo.image.make.triangle / custom shape (partial)
  - missing Vuo support: HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## NGonGradient

- TiXL full path: `Lib.image.generate.basic.NGonGradient`
- Namespace: `Lib.image.generate.basic`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/generate/basic/NGonGradient.cs`
  - .t3 defaults: `Operators/Lib/image/generate/basic/NGonGradient.t3`
  - docs: `.help/docs/operators/lib/image/generate/basic/NGonGradient.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/generate/NGonGradient.hlsl`
- Purpose: Renders a polygon shape similar to [NGon], [Blob] or [RoundedRect].
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor. TiXL Gradient has no direct Vuo type found in source scan; model as color/position lists or custom gradient helper.
- Inputs:
  - `BiasAndGain`: Vector2, default {'X': 0.5, 'Y': 0.5}; Bias and gain to control the gradient
  - `Blades`: float, default 0.0; Offsets every second edge to create sharp corners
  - `BlendMode`: int, default 0; Blend mode between the generated blob graphic and the background image, if one is provided.
  - `Curvature`: float, default 0.0; Defines if the outer line is convex or concave and can also invert the shape
  - `Gradient`: Gradient, default {'Gradient': {'Interpolation': 'Linear', 'Steps': [{'Id': 'dfa71896-0de8-47d7-9f2d-e7e28ab7bedb', 'NormalizedPosition': 0.0, 'Color': {'R': 1.0, 'G': 1.0, 'B': 1.0, 'A': 1.0}}, {'Id': 'f6f198f5-be17-4715-88d2-550585672e4b', 'NormalizedPosition': 1.0, 'Color': {'R': 0.0, 'G': 0.0, 'B': 0.0, 'A': 1.0}}]}}; Defines the Gradient
  - `Image`: Texture2D, default Unknown; An optional background image. If connected, allows applying different BlendingModes.
  - `Offset`: float, default 0.0; Offsets the gradient inward / outward
  - `PingPong`: bool, default False; Mirrors the gradient inward
  - `Position`: Vector2, default {'X': 0.0, 'Y': 0.0}; X/Y position, in relative units.
  - `Radius`: float, default 0.33; Defines the radius
  - `Repeat`: bool, default False; Repeats the Gradient outward
  - `Resolution`: Vector.Int2, default {'X': 0, 'Y': 0}; Output resolution in pixels. Set to 0 for dynamic resolution.
  - `Rotate`: float, default 180.0; Rotates the object around its center
  - `Roundness`: float, default 1.0; Defines how round / sharp the corners are
  - `Sides`: float, default 5.0; Defines the amount of sides
  - `Width`: float, default 0.14; Defines the size of the gradient
- Outputs:
  - `TextureOutput`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: _multiImageFxSetupStatic (1), Lib.numbers.color.DefineGradient (1)
  - common outgoing nodes: Lib.image.generate.basic.NGonGradient (10), Lib.image.generate.basic.NGonGradient (2), Lib.image.generate.basic.NGonGradient (2), Lib.image.generate.basic.NGonGradient (1), Lib.image.generate.basic.NGonGradient (1)
- Vuo mapping:
  - Vuo input types: BiasAndGain -> VuoPoint2d or integer pair; Blades -> VuoReal; BlendMode -> VuoInteger / enum; Curvature -> VuoReal; Gradient -> VuoList<VuoColor> + positions; Image -> VuoImage; Offset -> VuoReal; PingPong -> VuoBoolean; Position -> VuoPoint2d or integer pair; Radius -> VuoReal
  - Vuo output types: TextureOutput -> VuoImage
  - direct built-in Vuo equivalent, if any: shader rewrite; partial gradient nodes
  - missing Vuo support: HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## RadialGradient

- TiXL full path: `Lib.image.generate.basic.RadialGradient`
- Namespace: `Lib.image.generate.basic`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/generate/basic/RadialGradient.cs`
  - .t3 defaults: `Operators/Lib/image/generate/basic/RadialGradient.t3`
  - docs: `.help/docs/operators/lib/image/generate/basic/RadialGradient.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/generate/RadialGradient.hlsl`
- Purpose: Generates an image with a radial gradient.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor. TiXL Gradient has no direct Vuo type found in source scan; model as color/position lists or custom gradient helper.
- Inputs:
  - `BiasAndGain`: Vector2, default {'X': 0.5, 'Y': 0.5}; Applies Gain and Bias before sampling the gradiant.
  - `BlendMode`: int, default 0; Blendmode when applied onto a connected input image.
  - `Center`: Vector2, default {'X': 0.0, 'Y': 0.0}; Center of the gradient
  - `GenerateMipMaps`: bool, default False; semantic role Unknown
  - `Gradient`: Gradient, default {'Gradient': {'Interpolation': 'Linear', 'Steps': [{'Id': '56e4b8b6-1c97-412b-9d21-7aa12d0ba50c', 'NormalizedPosition': 0.0, 'Color': {'R': 1.0, 'G': 0.99999, 'B': 1.0, 'A': 1.0}}, {'Id': '469c9380-cdcf-4d49-99c7-8d261939c749', 'NormalizedPosition': 1.0, 'Color': {'R': 0.0, 'G': 1.2159347e-11, 'B': 1e-06, 'A': 1.0}}]}}; semantic role Unknown
  - `Image`: Texture2D, default Unknown; semantic role Unknown
  - `Noise`: float, default 0.0; Adding slight noise can improve color banding.
  - `Offset`: float, default 0.0; Offsets the gradient.
  - `PingPong`: bool, default False; Repeats the gradient once.
This can be useful for rings.
  - `PolarOrientation`: bool, default False; If enabled switches to a "star like" polar coordiante orienation.
  - `Repeat`: bool, default False; Repeats the gradient.
  - `Resolution`: Vector.Int2, default {'X': 0, 'Y': 0}; The resolution.
If 0,0 will use requested resuition of the input resolution if connected.
  - `Stretch`: Vector2, default {'X': 1.0, 'Y': 1.0}; An additional stretch factor
  - `TextureFormat`: DXGI.Format, default R16G16B16A16_Float; semantic role Unknown
  - `Width`: float, default 1.0; The size of the gradient.
Flips the gradient if smaller than 0.
- Outputs:
  - `TextureOutput`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - DX11/app-specific evidence: RenderTarget, DXGI.Format, SharpDX
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: Offset2 (8), Offset (6), transitionWidth (6), Lib.image.generate.load.LoadImage (4), Lib.image.generate._obsolete._FractalNoiseOld (4)
  - common outgoing nodes: Lib.image.generate.basic.RadialGradient (26), Lib.image.generate.basic.RadialGradient (14), Lib.image.generate.basic.RadialGradient (10), Lib.image.generate.basic.RadialGradient (7), Lib.image.generate.basic.RadialGradient (7)
- Vuo mapping:
  - Vuo input types: BiasAndGain -> VuoPoint2d or integer pair; BlendMode -> VuoInteger / enum; Center -> VuoPoint2d or integer pair; GenerateMipMaps -> VuoBoolean; Gradient -> VuoList<VuoColor> + positions; Image -> VuoImage; Noise -> VuoReal; Offset -> VuoReal; PingPong -> VuoBoolean; PolarOrientation -> VuoBoolean
  - Vuo output types: TextureOutput -> VuoImage
  - direct built-in Vuo equivalent, if any: vuo.image.make.gradient.radial
  - missing Vuo support: HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## RenderTarget

- TiXL full path: `Lib.image.generate.basic.RenderTarget`
- Namespace: `Lib.image.generate.basic`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/generate/basic/RenderTarget.cs`
  - .t3 defaults: `Operators/Lib/image/generate/basic/RenderTarget.t3`
  - docs: `.help/docs/operators/lib/image/generate/basic/RenderTarget.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/dx11/resolve-multisampled-depth-buffer-cs.hlsl`
- Purpose: The primary method of rendering 3D data into a 2D image texture.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `Clear`: bool, default True; semantic role Unknown
  - `ClearColor`: Vector4, default {'X': 0.0, 'Y': 0.0, 'Z': 0.0, 'W': 1.0}; Defines the RGBA value of the world background.
This setting can be transparent to composite the rendered image over another background.
  - `Command`: Command, default Unknown; semantic role Unknown
  - `EnableUpdate`: bool, default Unknown; Enables / Disables rendering. This can be very useful when connected to [Once] for caching the rendering after an initial update.
  - `GenerateMips`: bool, default False; semantic role Unknown
  - `Multisampling`: int, default Unknown, enum: No_MSAA = 1, MSAA_2Samples =2, MSAA_4Samples = 4, MSAA_8Samples = 8; Selects the quality level of the Multisample anti-aliasing (MSAA).
Higher values create smoother and less pixelated results but can have a high performance impact on slow GPUs (Graphics Cards).
For details see: https://en.wikipedia.org/wiki/Multisample_anti-aliasing
  - `Resolution`: Int2, default {'X': 0, 'Y': 0}; Overrides the default resolution or the resolution of the Output window.
If the resolution is:
- [0,0], the current resolution requested by the Output window is used.
- For image fx operators, negative resolutions will use the incoming resolution.
All other settings use custom resolutions.
The maximum resolution depends on your graphics hardware and is normally 16k.
  - `TextureFormat`: Format, default R16G16B16A16_Float; semantic role Unknown
  - `TextureReference`: RenderTargetReference, default Unknown; In combination with [UseRenderTarget] this can be used for feedback effects.
  - `WithDepthBuffer`: bool, default Unknown; You will need a depth buffer for all Z-Buffer sorting.
Combining a depth buffer with MultiSampling requires internal downsampling: This basically means doubling the GPU memory consumption.
  - `WithNormalBuffer`: bool, default Unknown; semantic role Unknown
- Outputs:
  - `ColorBuffer`: Texture2D, output image/data role
  - `DepthBuffer`: Texture2D, output image/data role
  - `NormalBuffer`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - DX11/app-specific evidence: TextureReference, RenderTarget, PixelShader, Command
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: Lib.flow.Execute (98), Lib.render.camera.Camera (76), Lib.render.transform.Group (67), Lib.render.camera.OrbitCamera (57), Lib.render.basic.Text (33)
  - common outgoing nodes: Lib.image.generate.basic.RenderTarget (44), Lib.image.generate.basic.RenderTarget (42), Lib.image.generate.basic.RenderTarget (34), Lib.image.generate.basic.RenderTarget (30), Lib.image.generate.basic.RenderTarget (28)
- Vuo mapping:
  - Vuo input types: Clear -> VuoBoolean; ClearColor -> VuoColor or VuoPoint4d; Command -> Unknown / custom; EnableUpdate -> VuoBoolean; GenerateMips -> VuoBoolean; Multisampling -> VuoInteger / enum; Resolution -> VuoPoint2d or integer pair; TextureFormat -> Unknown / custom; TextureReference -> Unknown / custom; WithDepthBuffer -> VuoBoolean
  - Vuo output types: ColorBuffer -> VuoImage; DepthBuffer -> VuoImage; NormalBuffer -> VuoImage
  - direct built-in Vuo equivalent, if any: vuo.image.render.window or offscreen render design (not direct)
  - missing Vuo support: HLSL must be translated or replaced; first-pass D dependency must be designed away or postponed
- Porting grade:
  - D: internal/obsolete/DX11-heavy first-pass document-only node; DX11/app terms: TextureReference, RenderTarget, PixelShader, Command; shader/compute dependency
- First implementation recommendation: Document and postpone until renderer/state/DX11 replacement strategy is chosen.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## RoundedRect

- TiXL full path: `Lib.image.generate.basic.RoundedRect`
- Namespace: `Lib.image.generate.basic`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/generate/basic/RoundedRect.cs`
  - .t3 defaults: `Operators/Lib/image/generate/basic/RoundedRect.t3`
  - docs: `.help/docs/operators/lib/image/generate/basic/RoundedRect.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/generate/RoundedRect.hlsl`
- Purpose: Generates a rounded rectangle.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `Background`: Vector4, default {'X': 0.0, 'Y': 0.0, 'Z': 0.0, 'W': 0.0}; Background color of the shape.
  - `Color`: Vector4, default {'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}; Fill color of the shape.
  - `Feather`: float, default 0.0; Feather edges to reduce pixel artifacts.
Can also be used to blur the shape.
Set to a negative value to create a vignette.
  - `FeatherBias`: float, default -0.001; Weights the feathering towards one edge or the other of the blurred region.
  - `GenerateMips`: bool, default False; Generate mipmaps (scaled-down versions of this image for use in situations where many small copies are shown on screen.)
Will increase memory usage.
  - `Image`: Texture2D, default Unknown; Image to use as a background for the shape.
Drawn behind the background color.
  - `Position`: Vector2, default {'X': 0.0, 'Y': 0.0}; Center position.
  - `Resolution`: Int2, default {'X': 0, 'Y': 0}; Output resolution in pixels. Set to 0 for dynamic resolution.
  - `Rotate`: float, default 0.0; Rotates the shape.
  - `Round`: float, default 0.5; Rounds corners of the shape.
A value of 0 will create sharp corners, and 1 will create a pill shape.
  - `Scale`: float, default 0.5; Scales the shape uniformly.
  - `Stretch`: Vector2, default {'X': 1.0, 'Y': 1.0}; Stretches the shape non-uniformly.
  - `Stroke`: float, default 0.0; Stroke (outline) thickness.
  - `StrokeColor`: Vector4, default {'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}; Color used by the shape stroke.
- Outputs:
  - `TextureOutput`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - DX11/app-specific evidence: RenderTarget
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: Lib.image.use.Blend (6), Lib.numbers.anim.time.Time (3), Lib.image.generate.basic.RoundedRect (3), Lib.image.generate.basic.RenderTarget (2), Multiply (2)
  - common outgoing nodes: Lib.image.generate.basic.RoundedRect (14), Lib.image.generate.basic.RoundedRect (14), Lib.image.generate.basic.RoundedRect (10), Lib.image.generate.basic.RoundedRect (10), Lib.image.generate.basic.RoundedRect (7)
- Vuo mapping:
  - Vuo input types: Background -> VuoColor or VuoPoint4d; Color -> VuoColor or VuoPoint4d; Feather -> VuoReal; FeatherBias -> VuoReal; GenerateMips -> VuoBoolean; Image -> VuoImage; Position -> VuoPoint2d or integer pair; Resolution -> VuoPoint2d or integer pair; Rotate -> VuoReal; Round -> VuoReal
  - Vuo output types: TextureOutput -> VuoImage
  - direct built-in Vuo equivalent, if any: no direct rounded-rect image node found
  - missing Vuo support: direct built-in equivalent not found; HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; likely needs ISF/Metal/GLSL or Vuo-specific renderer design
- First implementation recommendation: Write an ISF/Metal/GLSL parity shader after freezing TiXL defaults and a fixture image.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## LoadImage

- TiXL full path: `Lib.image.generate.load.LoadImage`
- Namespace: `Lib.image.generate.load`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/generate/load/LoadImage.cs`
  - .t3 defaults: `Operators/Lib/image/generate/load/LoadImage.t3`
  - docs: `.help/docs/operators/lib/image/generate/load/LoadImage.md`
  - related shader / helper source: None found in C#/.t3 scan
- Purpose: Loads an image file as a Texture2D.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `CacheResources`: bool, default Unknown; semantic role Unknown
  - `Path`: string, default Unknown; semantic role Unknown
- Outputs:
  - `Texture`: Texture2D, output image/data role
- Runtime behavior:
  - C# file mainly declares slots; `.t3` graph/defaults should be inspected for exact runtime composition. No shader file was found by the local scan.
  - DX11/app-specific evidence: PixelShader
  - important edge cases: Unknown beyond documented ports
- Observed graph usage:
  - common incoming nodes: PickStringFromList (18), SearchAndReplace (2), Lib.image.fx.stylize.AsciiRender (1), PickString (1)
  - common outgoing nodes: Lib.image.generate.load.LoadImage (31), Lib.image.generate.load.LoadImage (22), Lib.image.generate.load.LoadImage (21), Lib.image.generate.load.LoadImage (13), Lib.image.generate.load.LoadImage (11)
- Vuo mapping:
  - Vuo input types: CacheResources -> VuoBoolean; Path -> Unknown / custom
  - Vuo output types: Texture -> VuoImage
  - direct built-in Vuo equivalent, if any: vuo.image.fetch
  - missing Vuo support: mostly type/default parity and composition details
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## LoadImageFromUrl

- TiXL full path: `Lib.image.generate.load.LoadImageFromUrl`
- Namespace: `Lib.image.generate.load`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/generate/load/LoadImageFromUrl.cs`
  - .t3 defaults: `Operators/Lib/image/generate/load/LoadImageFromUrl.t3`
  - docs: `.help/docs/operators/lib/image/generate/load/LoadImageFromUrl.md`
  - related shader / helper source: None found in C#/.t3 scan
- Purpose: Loads an image file with the specified URL.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `TriggerUpdate`: bool, default Unknown; semantic role Unknown
  - `Url`: string, default Lib:https://cataas.com/cat; semantic role Unknown
- Outputs:
  - `ShaderResourceView`: ShaderResourceView, output image/data role
  - `Texture`: Texture2D, output image/data role
- Runtime behavior:
  - C# file mainly declares slots; `.t3` graph/defaults should be inspected for exact runtime composition. No shader file was found by the local scan.
  - DX11/app-specific evidence: Direct3D11, ShaderResourceView, SharpDX
  - important edge cases: Unknown beyond documented ports
- Observed graph usage:
  - common incoming nodes: Unknown
  - common outgoing nodes: Unknown
- Vuo mapping:
  - Vuo input types: TriggerUpdate -> VuoBoolean; Url -> Unknown / custom
  - Vuo output types: ShaderResourceView -> Unknown / custom; Texture -> VuoImage
  - direct built-in Vuo equivalent, if any: vuo.image.fetch
  - missing Vuo support: mostly type/default parity and composition details
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## FractalNoise

- TiXL full path: `Lib.image.generate.noise.FractalNoise`
- Namespace: `Lib.image.generate.noise`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/generate/noise/FractalNoise.cs`
  - .t3 defaults: `Operators/Lib/image/generate/noise/FractalNoise.t3`
  - docs: `.help/docs/operators/lib/image/generate/noise/FractalNoise.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/generate/FractalNoise.hlsl`
- Purpose: Generates a procedural fractal noise image effect also known as pink noise or fractional noise that can be used to create visual patterns that look like cloud, smoke, dust and scratches on film and similar Available Nois...
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `ColorA`: Vector4, default {'X': 1e-06, 'Y': 9.9999e-07, 'Z': 9.9999e-07, 'W': 1.0}; semantic role Unknown
  - `ColorB`: Vector4, default {'X': 1.0, 'Y': 0.99999, 'Z': 0.99999, 'W': 1.0}; semantic role Unknown
  - `GainAndBias`: Vector2, default {'X': 0.5, 'Y': 0.5}; semantic role Unknown
  - `GenerateMips`: bool, default False; semantic role Unknown
  - `Iterations`: int, default 2; semantic role Unknown
  - `Offset`: Vector2, default {'X': 0.0, 'Y': 0.0}; semantic role Unknown
  - `OutputFormat`: DXGI.Format, default R16G16B16A16_Float; semantic role Unknown
  - `RandomPhase`: float, default 5.0; semantic role Unknown
  - `Resolution`: Vector.Int2, default {'X': 256, 'Y': 256}; semantic role Unknown
  - `Scale`: float, default 1.0; semantic role Unknown
  - `Stretch`: Vector2, default {'X': 2.0, 'Y': 2.0}; semantic role Unknown
  - `WarpXY`: Vector2, default {'X': 0.0, 'Y': 0.0}; semantic role Unknown
  - `WarpZ`: float, default 0.0; semantic role Unknown
- Outputs:
  - `TextureOutput`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - DX11/app-specific evidence: DXGI.Format, SharpDX
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: Lib.numbers.anim._obsolete._Time_old (3), 67042c8b (3), 5e0d93c0 (2), 39428ad5 (2), f6ed3b68 (2)
  - common outgoing nodes: Lib.image.generate.noise.FractalNoise (8), Lib.image.generate.noise.FractalNoise (7), Lib.image.generate.noise.FractalNoise (6), Lib.image.generate.noise.FractalNoise (5), Lib.image.generate.noise.FractalNoise (4)
- Vuo mapping:
  - Vuo input types: ColorA -> VuoColor or VuoPoint4d; ColorB -> VuoColor or VuoPoint4d; GainAndBias -> VuoPoint2d or integer pair; GenerateMips -> VuoBoolean; Iterations -> VuoInteger / enum; Offset -> VuoPoint2d or integer pair; OutputFormat -> Unknown / custom; RandomPhase -> VuoReal; Resolution -> VuoPoint2d or integer pair; Scale -> VuoReal
  - Vuo output types: TextureOutput -> VuoImage
  - direct built-in Vuo equivalent, if any: vuo.image.make.noise (partial)
  - missing Vuo support: HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## Grain

- TiXL full path: `Lib.image.generate.noise.Grain`
- Namespace: `Lib.image.generate.noise`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/generate/noise/Grain.cs`
  - .t3 defaults: `Operators/Lib/image/generate/noise/Grain.t3`
  - docs: `.help/docs/operators/lib/image/generate/noise/Grain.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/generate/Grain.hlsl`
- Purpose: Adds animated image pixel noise similar to that of an analog TV or film grain.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `Amount`: float, default 0.05; Controls the intensity of the image noise.
  - `Animate`: float, default 5.0; Controls the speed of the animation
Low values can look like noise in liquids
High values (50 and up) can look like TV static or film grain
  - `Brightness`: float, default 0.0; Controls the image brightness
  - `Color`: float, default 0.0; Controls the saturation of the noise.
0: monochromatic noise
5: Colored Noise as in old TV Sets
  - `Exponent`: float, default 1.0; Controls the Exponent
  - `GenerateMipmaps`: bool, default False; Enables generation of mipmaps.
ProTip: This can be useful and produces better results if "Grain" is not used as a pure post effect, but is applied to a surface which is rendered in 3D space from different distances and angles. For example, a television / CCTV screen during a fly-through.
  - `Image`: Texture2D, default Unknown; semantic role Unknown
  - `RandomPhase`: float, default 0.0; semantic role Unknown
  - `Resolution`: Vector.Int2, default {'X': 0, 'Y': 0}; Can be used to override the default render size.
  - `Scale`: float, default 0.0; Changes the size of the generated noise pattern / pixels.
- Outputs:
  - `TextureOutput`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - DX11/app-specific evidence: RenderTarget, PixelShader
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: Lib.image.color.ColorGrade (10), Lib.image.fx.stylize.Glow (6), Lib.image.fx.stylize.ChromaticAbberation (5), Lib.image.transform.MirrorRepeat (2), Lib.image.generate.basic.RenderTarget (2)
  - common outgoing nodes: Lib.image.generate.noise.Grain (10), Lib.image.generate.noise.Grain (8), Lib.image.generate.noise.Grain (7), Lib.image.generate.noise.Grain (2), Lib.image.generate.noise.Grain (1)
- Vuo mapping:
  - Vuo input types: Amount -> VuoReal; Animate -> VuoReal; Brightness -> VuoReal; Color -> VuoReal; Exponent -> VuoReal; GenerateMipmaps -> VuoBoolean; Image -> VuoImage; RandomPhase -> VuoReal; Resolution -> VuoPoint2d or integer pair; Scale -> VuoReal
  - Vuo output types: TextureOutput -> VuoImage
  - direct built-in Vuo equivalent, if any: vuo.image.filmGrain / make.noise (partial)
  - missing Vuo support: HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## ShardNoise

- TiXL full path: `Lib.image.generate.noise.ShardNoise`
- Namespace: `Lib.image.generate.noise`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/generate/noise/ShardNoise.cs`
  - .t3 defaults: `Operators/Lib/image/generate/noise/ShardNoise.t3`
  - docs: `.help/docs/operators/lib/image/generate/noise/ShardNoise.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/generate/ShardNoise.hlsl`
- Purpose: A port of @ENDESGA's SHARD NOISE shader It can be used for clouds/fog/metal/crystal textures and more Check the link below.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `ColorA`: Vector4, default {'X': 1e-06, 'Y': 9.9999e-07, 'Z': 9.9999e-07, 'W': 1.0}; semantic role Unknown
  - `ColorB`: Vector4, default {'X': 1.0, 'Y': 0.99999, 'Z': 0.99999, 'W': 1.0}; semantic role Unknown
  - `Direction`: Vector2, default {'X': 0.0, 'Y': 0.0}; semantic role Unknown
  - `GainAndBias`: Vector2, default {'X': 0.5, 'Y': 0.5}; semantic role Unknown
  - `GenerateMips`: bool, default False; semantic role Unknown
  - `Method`: int, default 0, enum: Cubism, Cubism_X_Octaves, Octaves; semantic role Unknown
  - `Offset`: Vector2, default {'X': 0.0, 'Y': 0.0}; semantic role Unknown
  - `Phase`: float, default 0.0; semantic role Unknown
  - `Rate`: float, default 2.0; semantic role Unknown
  - `Resolution`: Vector.Int2, default {'X': 256, 'Y': 256}; semantic role Unknown
  - `Scale`: float, default 10.0; semantic role Unknown
  - `Sharpen`: float, default 1.0; semantic role Unknown
  - `Stretch`: Vector2, default {'X': 2.0, 'Y': 2.0}; semantic role Unknown
- Outputs:
  - `TextureOutput`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: Lib.numbers.anim.time.Time (4), Lib.render._dx11.fxsetup._ImageFxShaderSetupStatic (1), Lib.numbers.anim._obsolete._Time_old (1), 67130192 (1), 23890840 (1)
  - common outgoing nodes: Lib.image.generate.noise.ShardNoise (6), Lib.image.generate.noise.ShardNoise (4), Lib.image.generate.noise.ShardNoise (2), Lib.image.generate.noise.ShardNoise (2), Lib.image.generate.noise.ShardNoise (1)
- Vuo mapping:
  - Vuo input types: ColorA -> VuoColor or VuoPoint4d; ColorB -> VuoColor or VuoPoint4d; Direction -> VuoPoint2d or integer pair; GainAndBias -> VuoPoint2d or integer pair; GenerateMips -> VuoBoolean; Method -> VuoInteger / enum; Offset -> VuoPoint2d or integer pair; Phase -> VuoReal; Rate -> VuoReal; Resolution -> VuoPoint2d or integer pair
  - Vuo output types: TextureOutput -> VuoImage
  - direct built-in Vuo equivalent, if any: vuo.image.make.noise (partial)
  - missing Vuo support: HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## TileableNoise

- TiXL full path: `Lib.image.generate.noise.TileableNoise`
- Namespace: `Lib.image.generate.noise`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/generate/noise/TileableNoise.cs`
  - .t3 defaults: `Operators/Lib/image/generate/noise/TileableNoise.t3`
  - docs: `.help/docs/operators/lib/image/generate/noise/TileableNoise.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/generate/PerlinNoise2d.hlsl`
- Purpose: Generates a procedural fractal noise image effect also known as pink noise or fractional noise that can be used to create visual patterns that look like cloud, smoke, dust and scratches on film and similar Available Nois...
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `ColorA`: Vector4, default {'X': 1e-06, 'Y': 9.9999e-07, 'Z': 9.9999e-07, 'W': 1.0}; semantic role Unknown
  - `ColorB`: Vector4, default {'X': 1.0, 'Y': 0.99999, 'Z': 0.99999, 'W': 1.0}; semantic role Unknown
  - `Contrast`: float, default 1.7; semantic role Unknown
  - `Detail`: int, default 1; The base period of the repeated noise
  - `Gain`: float, default 0.5; The gain factor for each iteration step.
  - `GainAndBias`: Vector2, default {'X': 0.5, 'Y': 0.5}; semantic role Unknown
  - `GenerateMips`: bool, default False; semantic role Unknown
  - `Lacunarity`: float, default 2.0; The scale factor for each iteration step.
  - `Octaves`: int, default 2; The complexity of the perline noise.
I.e. the number of iterations the noise is computed.
  - `Offset`: Vector2, default {'X': 0.0, 'Y': 0.0}; semantic role Unknown
  - `OutputFormat`: DXGI.Format, default R16G16B16A16_Float; semantic role Unknown
  - `RandomPhase`: float, default 5.0; The 3rd coordinate of the 3d noise. Can be usedf for animations.
  - `Resolution`: Vector.Int2, default {'X': 1024, 'Y': 1024}; semantic role Unknown
  - `Scale`: float, default 1.0; An additional scale factor that WILL ADD SEAMS.
- Outputs:
  - `TextureOutput`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - DX11/app-specific evidence: DXGI.Format, SharpDX
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: Lib.render._dx11.fxsetup._ImageFxShaderSetupStatic (1), 0c1e9a11 (1), Lib.numbers.anim.time.Time (1)
  - common outgoing nodes: Lib.image.generate.noise.TileableNoise (9), Lib.image.generate.noise.TileableNoise (2), Lib.image.generate.noise.TileableNoise (2), Lib.image.generate.noise.TileableNoise (2), Lib.image.generate.noise.TileableNoise (2)
- Vuo mapping:
  - Vuo input types: ColorA -> VuoColor or VuoPoint4d; ColorB -> VuoColor or VuoPoint4d; Contrast -> VuoReal; Detail -> VuoInteger / enum; Gain -> VuoReal; GainAndBias -> VuoPoint2d or integer pair; GenerateMips -> VuoBoolean; Lacunarity -> VuoReal; Octaves -> VuoInteger / enum; Offset -> VuoPoint2d or integer pair
  - Vuo output types: TextureOutput -> VuoImage
  - direct built-in Vuo equivalent, if any: vuo.image.make.noise Tile=true (partial)
  - missing Vuo support: HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## WorleyNoise

- TiXL full path: `Lib.image.generate.noise.WorleyNoise`
- Namespace: `Lib.image.generate.noise`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/generate/noise/WorleyNoise.cs`
  - .t3 defaults: `Operators/Lib/image/generate/noise/WorleyNoise.t3`
  - docs: `.help/docs/operators/lib/image/generate/noise/WorleyNoise.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/generate/WorleyNoise.hlsl`
- Purpose: Also called Voronoi noise and cellular noise.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `Clamping`: Vector2, default {'X': 0.0, 'Y': 1.0}; semantic role Unknown
  - `ColorA`: Vector4, default {'X': 1.0, 'Y': 0.9999899, 'Z': 0.9999899, 'W': 1.0}; semantic role Unknown
  - `ColorB`: Vector4, default {'X': 0.0, 'Y': 0.0, 'Z': 0.0, 'W': 1.0}; semantic role Unknown
  - `GainAndBias`: Vector2, default {'X': 0.5, 'Y': 0.5}; semantic role Unknown
  - `GenerateMips`: bool, default False; semantic role Unknown
  - `Method`: int, default 0, enum: Worley_F1, Manhattan_worley_F1, Chebyshev_worley_F1, Worley_F2_F1, Manhattan_worley_F2_F1, Chebyshev_worley_F2_F1; semantic role Unknown
  - `Offset`: Vector2, default {'X': 0.0, 'Y': 0.0}; semantic role Unknown
  - `Phase`: float, default 5.0; semantic role Unknown
  - `Randomness`: float, default 12.6; semantic role Unknown
  - `Resolution`: Vector.Int2, default {'X': 512, 'Y': 512}; semantic role Unknown
  - `Scale`: float, default 5.0; semantic role Unknown
  - `Stretch`: Vector2, default {'X': 1.0, 'Y': 1.0}; semantic role Unknown
  - `Texture`: Texture2D, default Unknown; semantic role Unknown
  - `TextureBlend`: float, default 1.0; semantic role Unknown
- Outputs:
  - `TextureOutput`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: fa859fd5 (3), 5fc755ae (2), Lib.render._dx11.fxsetup._ImageFxShaderSetupStatic (1), Lib.image.generate.basic.LinearGradient (1), Lib.image.color.RemapColor (1)
  - common outgoing nodes: Lib.image.generate.noise.WorleyNoise (7), Lib.image.generate.noise.WorleyNoise (7), Lib.image.generate.noise.WorleyNoise (4), Lib.image.generate.noise.WorleyNoise (3), Lib.image.generate.noise.WorleyNoise (2)
- Vuo mapping:
  - Vuo input types: Clamping -> VuoPoint2d or integer pair; ColorA -> VuoColor or VuoPoint4d; ColorB -> VuoColor or VuoPoint4d; GainAndBias -> VuoPoint2d or integer pair; GenerateMips -> VuoBoolean; Method -> VuoInteger / enum; Offset -> VuoPoint2d or integer pair; Phase -> VuoReal; Randomness -> VuoReal; Resolution -> VuoPoint2d or integer pair
  - Vuo output types: TextureOutput -> VuoImage
  - direct built-in Vuo equivalent, if any: vuo.image.make.noise cellular (partial)
  - missing Vuo support: HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## FraserGrid

- TiXL full path: `Lib.image.generate.pattern.FraserGrid`
- Namespace: `Lib.image.generate.pattern`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/generate/pattern/FraserGrid.cs`
  - .t3 defaults: `Operators/Lib/image/generate/pattern/FraserGrid.t3`
  - docs: `.help/docs/operators/lib/image/generate/pattern/FraserGrid.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/fx/FraserGrid.hlsl`
- Purpose: The basis for the Fräser spiral illusion.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `Background`: Vector4, default {'X': 0.67475104, 'Y': 0.67498636, 'Z': 0.67569184, 'W': 1.0}; semantic role Unknown
  - `BAffects_LineRatio`: float, default 0.0; semantic role Unknown
  - `BarWidth`: float, default 0.035; semantic role Unknown
  - `BorderWidth`: float, default 0.06; semantic role Unknown
  - `Feather`: float, default 0.015; semantic role Unknown
  - `Fill`: Vector4, default {'X': 0.0, 'Y': 0.0, 'Z': 0.0, 'W': 1.0}; semantic role Unknown
  - `FillB`: Vector4, default {'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}; semantic role Unknown
  - `GAffects_ShapeSize`: float, default 0.0; semantic role Unknown
  - `Image`: Texture2D, default Unknown; semantic role Unknown
  - `Offset`: Vector2, default {'X': 0.0, 'Y': 0.0}; semantic role Unknown
  - `RAffects_BarWidth`: float, default 0.0; semantic role Unknown
  - `Resolution`: Vector.Int2, default {'X': 0, 'Y': 0}; semantic role Unknown
  - `Rotate`: float, default 0.0; semantic role Unknown
  - `RotateShapes`: float, default 45.0; semantic role Unknown
  - `RowSwift`: float, default 0.0; semantic role Unknown
  - `Scale`: float, default 4.0; semantic role Unknown
  - `ShapeSize`: float, default 0.22; semantic role Unknown
  - `Size`: Vector2, default {'X': 32.0, 'Y': 16.0}; semantic role Unknown
- Outputs:
  - `TextureOutput`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: Lib.image.generate.basic.RoundedRect (3), 15c3c5b6 (2), Lib.render._dx11.fxsetup._ImageFxShaderSetupStatic (1), Lib.numbers.anim._obsolete._Time_old (1), Lib.image.generate.basic.RadialGradient (1)
  - common outgoing nodes: Lib.image.generate.pattern.FraserGrid (14), Lib.image.generate.pattern.FraserGrid (4), Lib.image.generate.pattern.FraserGrid (3), Lib.image.generate.pattern.FraserGrid (2), Lib.image.generate.pattern.FraserGrid (1)
- Vuo mapping:
  - Vuo input types: Background -> VuoColor or VuoPoint4d; BAffects_LineRatio -> VuoReal; BarWidth -> VuoReal; BorderWidth -> VuoReal; Feather -> VuoReal; Fill -> VuoColor or VuoPoint4d; FillB -> VuoColor or VuoPoint4d; GAffects_ShapeSize -> VuoReal; Image -> VuoImage; Offset -> VuoPoint2d or integer pair
  - Vuo output types: TextureOutput -> VuoImage
  - direct built-in Vuo equivalent, if any: shader rewrite; no direct illusion-pattern node
  - missing Vuo support: HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## NumberPattern

- TiXL full path: `Lib.image.generate.pattern.NumberPattern`
- Namespace: `Lib.image.generate.pattern`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/generate/pattern/NumberPattern.cs`
  - .t3 defaults: `Operators/Lib/image/generate/pattern/NumberPattern.t3`
  - docs: `.help/docs/operators/lib/image/generate/pattern/NumberPattern.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/generate/NumberPattern.hlsl`
- Purpose: Renders columns for values that can be driven by an input texture.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `CellRange`: Vector2, default {'X': 1.0, 'Y': 1.0}; semantic role Unknown
  - `CellSize`: Vector2, default {'X': 200.0, 'Y': 8.0}; semantic role Unknown
  - `Highlight`: Vector4, default {'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}; semantic role Unknown
  - `HighlightThreshold`: float, default 0.0; semantic role Unknown
  - `LineColor`: Vector4, default {'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}; semantic role Unknown
  - `Offset`: float, default 100.0; semantic role Unknown
  - `OriginalImage`: Vector4, default {'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}; semantic role Unknown
  - `Position`: Vector2, default {'X': 0.0, 'Y': 0.0}; semantic role Unknown
  - `ScrollOffset`: float, default 0.0; semantic role Unknown
  - `ScrollSpeed`: float, default 1.0; semantic role Unknown
  - `TextColor`: Vector4, default {'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}; semantic role Unknown
  - `Texture`: Texture2D, default Unknown; semantic role Unknown
- Outputs:
  - `TextureOutput`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - important edge cases: Unknown beyond documented ports
- Observed graph usage:
  - common incoming nodes: _multiImageFxSetup (1), Lib.image.generate.pattern.Raster (1)
  - common outgoing nodes: Lib.image.generate.pattern.NumberPattern (4), Lib.image.generate.pattern.NumberPattern (3), Lib.image.generate.pattern.NumberPattern (3), Lib.image.generate.pattern.NumberPattern (1), Lib.image.generate.pattern.NumberPattern (1)
- Vuo mapping:
  - Vuo input types: CellRange -> VuoPoint2d or integer pair; CellSize -> VuoPoint2d or integer pair; Highlight -> VuoColor or VuoPoint4d; HighlightThreshold -> VuoReal; LineColor -> VuoColor or VuoPoint4d; Offset -> VuoReal; OriginalImage -> VuoColor or VuoPoint4d; Position -> VuoPoint2d or integer pair; ScrollOffset -> VuoReal; ScrollSpeed -> VuoReal
  - Vuo output types: TextureOutput -> VuoImage
  - direct built-in Vuo equivalent, if any: shader rewrite; no direct number-pattern node
  - missing Vuo support: HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## Raster

- TiXL full path: `Lib.image.generate.pattern.Raster`
- Namespace: `Lib.image.generate.pattern`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/generate/pattern/Raster.cs`
  - .t3 defaults: `Operators/Lib/image/generate/pattern/Raster.t3`
  - docs: `.help/docs/operators/lib/image/generate/pattern/Raster.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/fx/Raster.hlsl`
- Purpose: Generates a wide range of patterns of dots and lines.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `Background`: Vector4, default {'X': 0.0, 'Y': 0.0, 'Z': 0.0, 'W': 0.0}; Color of the background behind the raster pattern.
  - `BlueToLineRatio`: float, default 0.0; Strength of the input texture's blue color channel on the line ratio.
Requires a texture to be connected to the Image input.
  - `Color`: Vector4, default {'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}; Main color of the raster pattern, used by dots and lines.
  - `DotSize`: float, default 0.05333333; Size of the dots in the pattern.
  - `Feather`: float, default 0.02; Feathers (blurs) the raster pattern. Useful for smoothing out pixelated edges.
Affected by the background color, even when it's transparent!
  - `GreenToLineWidth`: float, default 0.0; Strength of the input texture's green color channel on the line width.
Requires a texture to be connected to the Image input.
  - `Image`: Texture2D, default Unknown; Adds an image to the background.
This image can also be used to control other parameters dynamically.
  - `LineRatio`: float, default 0.75; At values below 0.5, creates a gap in the middle of the lines.
At values above 0.5, creates a gap where lines intersect.
Uses a circular crop to create these gaps, so high line widths with a low ratio will cause the lines to look like circles.
  - `LineWidth`: float, default 0.053333342; Width of the lines in the pattern.
  - `MixOriginal`: float, default 1.0; Blends the image in with the background color.
Note that the image will only be visible if the background color has transparency.
  - `Offset`: Vector2, default {'X': 0.0, 'Y': 0.0}; Offsets the position of the raster texture.
An offset of 1 is equivalent to the distance between dots.
  - `RedToDotSize`: float, default 0.0; Strength of the input texture's red color channel on the dot size.
Requires a texture to be connected to the Image input.
  - `Resolution`: Vector.Int2, default {'X': 0, 'Y': 0}; Output resolution. Set to 0 for dynamic scaling.
  - `Rotate`: float, default 0.0; Rotates the pattern. Measured in degrees.
  - `Scale`: float, default 4.0; Scales the pattern evenly.
  - `Stretch`: Vector2, default {'X': 32.0, 'Y': 32.0}; Stretches the pattern unevenly.
- Outputs:
  - `TextureOutput`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - DX11/app-specific evidence: RenderTarget
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: Lib.numbers.anim._obsolete._Time_old (3), Lib.image.generate.basic.Blob (2), Lib.image.generate.basic.RenderTarget (2), Remap (2), Lib.image.fx.glitch.SubdivisionStretch (2)
  - common outgoing nodes: Lib.image.generate.pattern.Raster (13), Lib.image.generate.pattern.Raster (4), Lib.image.generate.pattern.Raster (4), Lib.image.generate.pattern.Raster (2), Lib.image.generate.pattern.Raster (2)
- Vuo mapping:
  - Vuo input types: Background -> VuoColor or VuoPoint4d; BlueToLineRatio -> VuoReal; Color -> VuoColor or VuoPoint4d; DotSize -> VuoReal; Feather -> VuoReal; GreenToLineWidth -> VuoReal; Image -> VuoImage; LineRatio -> VuoReal; LineWidth -> VuoReal; MixOriginal -> VuoReal
  - Vuo output types: TextureOutput -> VuoImage
  - direct built-in Vuo equivalent, if any: vuo.image.make.stripe / checkerboard (partial)
  - missing Vuo support: HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## Rings

- TiXL full path: `Lib.image.generate.pattern.Rings`
- Namespace: `Lib.image.generate.pattern`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/generate/pattern/Rings.cs`
  - .t3 defaults: `Operators/Lib/image/generate/pattern/Rings.t3`
  - docs: `.help/docs/operators/lib/image/generate/pattern/Rings.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/generate/Rings.hlsl`
- Purpose: Generates a procedural rings texture that can produce a wide variety of radial patterns.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `_FillRatio`: float, default 1.0; semantic role Unknown
  - `_HighlightRatio`: float, default 0.0; semantic role Unknown
  - `_Ratio`: Vector2, default {'X': 1.05, 'Y': 0.0}; semantic role Unknown
  - `_Segments`: Vector2, default {'X': 20.0, 'Y': 0.0}; semantic role Unknown
  - `_Thickness`: Vector2, default {'X': 0.5, 'Y': 0.0}; semantic role Unknown
  - `_Twist`: Vector2, default {'X': 0.0, 'Y': 0.0}; semantic role Unknown
  - `Background`: Vector4, default {'X': 0.0, 'Y': 0.0, 'Z': 0.0, 'W': 0.0}; semantic role Unknown
  - `BlendMode`: int, default 0; semantic role Unknown
  - `Constrast`: float, default 1.0; semantic role Unknown
  - `Count`: float, default 0.5; semantic role Unknown
  - `Distort`: float, default 1.0; semantic role Unknown
  - `Feather`: float, default 0.03333335; semantic role Unknown
  - `Fill`: Vector4, default {'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}; semantic role Unknown
  - `Highlight`: Vector4, default {'X': 1.0, 'Y': 0.0, 'Z': 0.0, 'W': 1.0}; semantic role Unknown
  - `HighlightSeed`: int, default 0; semantic role Unknown
  - `Image`: Texture2D, default Unknown; semantic role Unknown
  - `Offset`: float, default 0.0; semantic role Unknown
  - `Position`: Vector2, default {'X': 0.0, 'Y': 0.0}; semantic role Unknown
  - `Radius`: Vector2, default {'X': 0.0, 'Y': 0.5}; semantic role Unknown
  - `Resolution`: Int2, default {'X': 0, 'Y': 0}; semantic role Unknown
  - `Rotate`: float, default 0.0; semantic role Unknown
  - `Seed`: int, default 0; semantic role Unknown
- Outputs:
  - `TextureOutput`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: Lib.numbers.anim._obsolete._Time_old (2), Lib.io.audio.AudioReaction (2), Lib.render._dx11.fxsetup._ImageFxShaderSetupStatic (1), Lib.io.video.PlayVideo (1), Lib.image.generate._obsolete._BlobOld (1)
  - common outgoing nodes: Lib.image.generate.pattern.Rings (10), Lib.image.generate.pattern.Rings (6), Lib.image.generate.pattern.Rings (3), Lib.image.generate.pattern.Rings (3), Lib.image.generate.pattern.Rings (2)
- Vuo mapping:
  - Vuo input types: _FillRatio -> VuoReal; _HighlightRatio -> VuoReal; _Ratio -> VuoPoint2d or integer pair; _Segments -> VuoPoint2d or integer pair; _Thickness -> VuoPoint2d or integer pair; _Twist -> VuoPoint2d or integer pair; Background -> VuoColor or VuoPoint4d; BlendMode -> VuoInteger / enum; Constrast -> VuoReal; Count -> VuoReal
  - Vuo output types: TextureOutput -> VuoImage
  - direct built-in Vuo equivalent, if any: vuo.image.make.gradient.radial (partial)
  - missing Vuo support: HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## RyojiPattern1

- TiXL full path: `Lib.image.generate.pattern.RyojiPattern1`
- Namespace: `Lib.image.generate.pattern`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/generate/pattern/RyojiPattern1.cs`
  - .t3 defaults: `Operators/Lib/image/generate/pattern/RyojiPattern1.t3`
  - docs: `.help/docs/operators/lib/image/generate/pattern/RyojiPattern1.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/generate/RyojiPattern1.hlsl`
- Purpose: Generates animated patterns inspired by Ryoji Ikeda.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `Background`: Vector4, default {'X': 0.0, 'Y': 0.0, 'Z': 0.0, 'W': 1.0}; semantic role Unknown
  - `Contrast`: float, default 0.75; semantic role Unknown
  - `Foreground`: Vector4, default {'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}; semantic role Unknown
  - `ForgroundRatio`: float, default 0.5; semantic role Unknown
  - `GenerateMipmaps`: bool, default False; semantic role Unknown
  - `Highlight`: Vector4, default {'X': 1.0, 'Y': 0.0, 'Z': 0.0, 'W': 1.0}; semantic role Unknown
  - `HighlightProbability`: float, default 0.01; semantic role Unknown
  - `HighlightSeed`: float, default 0.0; semantic role Unknown
  - `Image`: Texture2D, default Unknown; semantic role Unknown
  - `Iterations`: float, default 7.0; semantic role Unknown
  - `MixOriginal`: float, default 0.0; semantic role Unknown
  - `Padding`: Vector2, default {'X': 0.02, 'Y': 0.023333333}; semantic role Unknown
  - `Resolution`: Vector.Int2, default {'X': 0, 'Y': 0}; semantic role Unknown
  - `ScrollProbability`: Vector2, default {'X': 0.0, 'Y': 0.5}; semantic role Unknown
  - `ScrollSpeed`: Vector2, default {'X': 0.0, 'Y': -0.23333332}; semantic role Unknown
  - `Seed`: float, default 0.0; semantic role Unknown
  - `SplitProbability`: Vector2, default {'X': 0.0, 'Y': 0.27666667}; semantic role Unknown
  - `Splits`: Vector2, default {'X': 4.0, 'Y': 3.0}; semantic role Unknown
- Outputs:
  - `TextureOutput`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: ad7710c7 (2), IntToFloat (2), Lib.numbers.anim._obsolete.Counter (2), Lib.render._dx11.fxsetup._ImageFxShaderSetup2 (1), Lib.numbers.color.SampleGradient (1)
  - common outgoing nodes: Lib.image.generate.pattern.RyojiPattern1 (10), Lib.image.generate.pattern.RyojiPattern1 (5), Lib.image.generate.pattern.RyojiPattern1 (3), Lib.image.generate.pattern.RyojiPattern1 (3), Lib.image.generate.pattern.RyojiPattern1 (1)
- Vuo mapping:
  - Vuo input types: Background -> VuoColor or VuoPoint4d; Contrast -> VuoReal; Foreground -> VuoColor or VuoPoint4d; ForgroundRatio -> VuoReal; GenerateMipmaps -> VuoBoolean; Highlight -> VuoColor or VuoPoint4d; HighlightProbability -> VuoReal; HighlightSeed -> VuoReal; Image -> VuoImage; Iterations -> VuoReal
  - Vuo output types: TextureOutput -> VuoImage
  - direct built-in Vuo equivalent, if any: shader rewrite
  - missing Vuo support: HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## RyojiPattern2

- TiXL full path: `Lib.image.generate.pattern.RyojiPattern2`
- Namespace: `Lib.image.generate.pattern`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/generate/pattern/RyojiPattern2.cs`
  - .t3 defaults: `Operators/Lib/image/generate/pattern/RyojiPattern2.t3`
  - docs: `.help/docs/operators/lib/image/generate/pattern/RyojiPattern2.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/generate/RyojiPattern2.hlsl`
- Purpose: A pattern generator inspired by the work of Ryoji Ikeda.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `Background`: Vector4, default {'X': 1e-06, 'Y': 9.9999e-07, 'Z': 9.9999e-07, 'W': 0.0}; semantic role Unknown
  - `Contrast`: float, default 0.5; semantic role Unknown
  - `Foreground`: Vector4, default {'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}; semantic role Unknown
  - `ForgroundRatio`: float, default 0.50333333; semantic role Unknown
  - `Highlight`: Vector4, default {'X': 1.0, 'Y': 0.0, 'Z': 0.0, 'W': 1.0}; semantic role Unknown
  - `HighlightProbability`: float, default 0.01; semantic role Unknown
  - `HighlightSeed`: int, default 0; semantic role Unknown
  - `Image`: Texture2D, default Unknown; semantic role Unknown
  - `MixOriginal`: float, default 0.0; semantic role Unknown
  - `Padding`: Vector2, default {'X': 0.02, 'Y': 0.02}; semantic role Unknown
  - `Resolution`: Vector.Int2, default {'X': 0, 'Y': 0}; semantic role Unknown
  - `ScrollOffset`: float, default 0.0; semantic role Unknown
  - `ScrollProbability`: Vector2, default {'X': 0.0, 'Y': 0.5}; semantic role Unknown
  - `ScrollSpeed`: Vector2, default {'X': 0.04, 'Y': 0.5}; semantic role Unknown
  - `Seed`: float, default 42.0; semantic role Unknown
  - `SplitB`: Vector2, default {'X': 1.0, 'Y': 3.0}; semantic role Unknown
  - `SplitC`: Vector2, default {'X': 1.0, 'Y': 10.0}; semantic role Unknown
  - `SplitProbability`: Vector2, default {'X': 0.1, 'Y': 0.5}; semantic role Unknown
  - `Splits`: Vector2, default {'X': 14.0, 'Y': 4.0}; semantic role Unknown
- Outputs:
  - `TextureOutput`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: Lib.numbers.color.SampleGradient (5), Lib.numbers.anim._obsolete.Counter (5), Lib.io.audio.AudioReaction (2), Lib.render._dx11.fxsetup._ImageFxShaderSetup2 (1), Lib.io.midi.MidiInput (1)
  - common outgoing nodes: Lib.image.generate.pattern.RyojiPattern2 (9), Lib.image.generate.pattern.RyojiPattern2 (7), Lib.image.generate.pattern.RyojiPattern2 (5), Lib.image.generate.pattern.RyojiPattern2 (3), Lib.image.generate.pattern.RyojiPattern2 (2)
- Vuo mapping:
  - Vuo input types: Background -> VuoColor or VuoPoint4d; Contrast -> VuoReal; Foreground -> VuoColor or VuoPoint4d; ForgroundRatio -> VuoReal; Highlight -> VuoColor or VuoPoint4d; HighlightProbability -> VuoReal; HighlightSeed -> VuoInteger / enum; Image -> VuoImage; MixOriginal -> VuoReal; Padding -> VuoPoint2d or integer pair
  - Vuo output types: TextureOutput -> VuoImage
  - direct built-in Vuo equivalent, if any: shader rewrite
  - missing Vuo support: HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## SinForm

- TiXL full path: `Lib.image.generate.pattern.SinForm`
- Namespace: `Lib.image.generate.pattern`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/generate/pattern/SinForm.cs`
  - .t3 defaults: `Operators/Lib/image/generate/pattern/SinForm.t3`
  - docs: `.help/docs/operators/lib/image/generate/pattern/SinForm.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/fx/SinForm.hlsl`
- Purpose: Generates a sine curve with various parameters The presets show various application possibilities for different patterns Other interesting patterns can be generated with [SinForm] [ZollnerPattern] [FraserGrid] [Raster] [...
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `Background`: Vector4, default {'X': 0.0, 'Y': 0.0, 'Z': 0.0, 'W': 0.0}; Defines the background color
  - `Copies`: float, default 0.0; Defines the amount of duplicated lines
  - `Fade`: float, default 1.0; Defines the smoothness of the line edges
  - `Fill`: Vector4, default {'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}; Defines the color of the line
  - `Image`: Texture2D, default Unknown; Custom input for a background image
  - `LineWidth`: float, default 0.04333334; Defines the thickness of the lines
  - `Offset`: Vector2, default {'X': 0.0, 'Y': 0.0}; Transforms the position of the line
  - `OffsetCopies`: Vector2, default {'X': 0.0, 'Y': 0.05}; Offsets the duplicated lines
  - `Resolution`: Vector.Int2, default {'X': 0, 'Y': 0}; semantic role Unknown
  - `Rotate`: float, default 0.0; Rotates the line
  - `Size`: Vector2, default {'X': 1.0, 'Y': 1.0}; Scales the lines on the x and y axis
  - `TextureFormat`: DXGI.Format, default R16G16B16A16_Float; semantic role Unknown
- Outputs:
  - `TextureOutput`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - DX11/app-specific evidence: DXGI.Format, SharpDX
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: Lib.render._dx11.fxsetup._ImageFxShaderSetupStatic (1), Lib.numbers.anim._obsolete.Counter (1), c032dc02 (1), 40f7d36d (1)
  - common outgoing nodes: Lib.image.generate.pattern.SinForm (7), Lib.image.generate.pattern.SinForm (6), Lib.image.generate.pattern.SinForm (3), Lib.image.generate.pattern.SinForm (2), Lib.image.generate.pattern.SinForm (1)
- Vuo mapping:
  - Vuo input types: Background -> VuoColor or VuoPoint4d; Copies -> VuoReal; Fade -> VuoReal; Fill -> VuoColor or VuoPoint4d; Image -> VuoImage; LineWidth -> VuoReal; Offset -> VuoPoint2d or integer pair; OffsetCopies -> VuoPoint2d or integer pair; Resolution -> VuoPoint2d or integer pair; Rotate -> VuoReal
  - Vuo output types: TextureOutput -> VuoImage
  - direct built-in Vuo equivalent, if any: shader rewrite
  - missing Vuo support: HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## ValueRaster

- TiXL full path: `Lib.image.generate.pattern.ValueRaster`
- Namespace: `Lib.image.generate.pattern`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/generate/pattern/ValueRaster.cs`
  - .t3 defaults: `Operators/Lib/image/generate/pattern/ValueRaster.t3`
  - docs: `.help/docs/operators/lib/image/generate/pattern/ValueRaster.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/fx/ValueRaster.hlsl`
- Purpose: *No description yet.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `Background`: Vector4, default {'X': 0.0, 'Y': 0.0, 'Z': 0.0, 'W': 0.0}; semantic role Unknown
  - `Color`: Vector4, default {'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 0.695}; semantic role Unknown
  - `Density`: Vector2, default {'X': 1000.0, 'Y': 1000.0}; semantic role Unknown
  - `Image`: Texture2D, default Unknown; semantic role Unknown
  - `MajorLineWidth`: float, default 1.0; semantic role Unknown
  - `MinorLineWidth`: float, default 0.25; semantic role Unknown
  - `MixOriginal`: float, default 1.0; semantic role Unknown
  - `RangeX`: Vector2, default {'X': 0.0, 'Y': 1.0}; semantic role Unknown
  - `RangeY`: Vector2, default {'X': 0.0, 'Y': 1.0}; semantic role Unknown
  - `Resolution`: Vector.Int2, default {'X': 0, 'Y': 0}; semantic role Unknown
- Outputs:
  - `TextureOutput`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: Lib.numbers.vec2.DampVec2 (1), 859aa648 (1), Lib.render._dx11.fxsetup._ImageFxShaderSetupStatic (1)
  - common outgoing nodes: Lib.image.generate.pattern.ValueRaster (5), Lib.image.generate.pattern.ValueRaster (3), Lib.image.generate.pattern.ValueRaster (2), Lib.image.generate.pattern.ValueRaster (1)
- Vuo mapping:
  - Vuo input types: Background -> VuoColor or VuoPoint4d; Color -> VuoColor or VuoPoint4d; Density -> VuoPoint2d or integer pair; Image -> VuoImage; MajorLineWidth -> VuoReal; MinorLineWidth -> VuoReal; MixOriginal -> VuoReal; RangeX -> VuoPoint2d or integer pair; RangeY -> VuoPoint2d or integer pair; Resolution -> VuoPoint2d or integer pair
  - Vuo output types: TextureOutput -> VuoImage
  - direct built-in Vuo equivalent, if any: shader rewrite
  - missing Vuo support: HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## ZollnerPattern

- TiXL full path: `Lib.image.generate.pattern.ZollnerPattern`
- Namespace: `Lib.image.generate.pattern`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/generate/pattern/ZollnerPattern.cs`
  - .t3 defaults: `Operators/Lib/image/generate/pattern/ZollnerPattern.t3`
  - docs: `.help/docs/operators/lib/image/generate/pattern/ZollnerPattern.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/fx/ZollnerGrid.hlsl`
- Purpose: Creates an image for an optical illusion.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `AmplifyIllusion`: float, default 0.0; Rotates bars in pairs in opposite directions
  - `Background`: Vector4, default {'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}; Defines the background color
  - `BAffects_HookRotation`: float, default 0.0; semantic role Unknown
  - `BarWidth`: float, default 0.2; Defines the thickness of the center bars
  - `Feather`: float, default 0.02; Smoothes the edges
  - `Fill`: Vector4, default {'X': 0.0, 'Y': 0.0, 'Z': 0.0, 'W': 1.0}; Defines the color of the line
  - `GAffects_HookLength`: float, default 0.0; semantic role Unknown
  - `HookLength`: float, default 0.7; Defines the length of the hooks
  - `HookRotation`: float, default 60.0; Shifts every second space between the bars
  - `HookWidth`: float, default 0.33; Defines the thickness of the hooks
  - `Image`: Texture2D, default Unknown; semantic role Unknown
  - `Offset`: Vector2, default {'X': 0.0, 'Y': 0.0}; Transforms the pattern on the x and y axis
  - `RAffects_BarWidth`: float, default 0.0; semantic role Unknown
  - `Resolution`: Int2, default {'X': 0, 'Y': 0}; semantic role Unknown
  - `Rotate`: float, default 45.0; Rotates the pattern
  - `RowSwift`: float, default 0.0; Shifts the picture elements with increasing intensity towards the edge
  - `Scale`: float, default 1.0; Uniformly scales the pattern
  - `Stretch`: Vector2, default {'X': 0.5, 'Y': 1.0}; Stretches the pattern on the x and y axis
- Outputs:
  - `TextureOutput`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: 013ceaa8 (3), 3b0b823d (3), Lib.image.use.Combine3Images (2), MainRotation (2), Multiply (2)
  - common outgoing nodes: Lib.image.generate.pattern.ZollnerPattern (15), Lib.image.generate.pattern.ZollnerPattern (2), Lib.image.generate.pattern.ZollnerPattern (2), Lib.image.generate.pattern.ZollnerPattern (2), Lib.image.generate.pattern.ZollnerPattern (1)
- Vuo mapping:
  - Vuo input types: AmplifyIllusion -> VuoReal; Background -> VuoColor or VuoPoint4d; BAffects_HookRotation -> VuoReal; BarWidth -> VuoReal; Feather -> VuoReal; Fill -> VuoColor or VuoPoint4d; GAffects_HookLength -> VuoReal; HookLength -> VuoReal; HookRotation -> VuoReal; HookWidth -> VuoReal
  - Vuo output types: TextureOutput -> VuoImage
  - direct built-in Vuo equivalent, if any: shader rewrite
  - missing Vuo support: HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## Crop

- TiXL full path: `Lib.image.transform.Crop`
- Namespace: `Lib.image.transform`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/transform/Crop.cs`
  - .t3 defaults: `Operators/Lib/image/transform/Crop.t3`
  - docs: `.help/docs/operators/lib/image/transform/Crop.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/CropImage-cs.hlsl`
- Purpose: Crops an image or adds a frame.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `LeftRight`: Int2, default {'X': 0, 'Y': 0}; semantic role Unknown
  - `PaddingColor`: Vector4, default {'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 0.0}; semantic role Unknown
  - `Texture2d`: Texture2D, default Unknown; semantic role Unknown
  - `TopBottom`: Int2, default {'X': 0, 'Y': 0}; semantic role Unknown
- Outputs:
  - `Output`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: Lib.image.generate.load.LoadImage (2), Lib.render._dx11.fxsetup.ExecuteTextureUpdate (1), PlayAtlas (1), 2c2ab357 (1), 824ce6c4 (1)
  - common outgoing nodes: Lib.image.transform.Crop (3), Lib.image.transform.Crop (2), Lib.image.transform.Crop (1), Lib.image.transform.Crop (1), Lib.image.transform.Crop (1)
- Vuo mapping:
  - Vuo input types: LeftRight -> VuoPoint2d or integer pair; PaddingColor -> VuoColor or VuoPoint4d; Texture2d -> VuoImage; TopBottom -> VuoPoint2d or integer pair
  - Vuo output types: Output -> VuoImage
  - direct built-in Vuo equivalent, if any: vuo.image.crop / crop.pixels
  - missing Vuo support: HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## MakeTileableImage

- TiXL full path: `Lib.image.transform.MakeTileableImage`
- Namespace: `Lib.image.transform`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/transform/MakeTileableImage.cs`
  - .t3 defaults: `Operators/Lib/image/transform/MakeTileableImage.t3`
  - docs: `.help/docs/operators/lib/image/transform/MakeTileableImage.md`
  - related shader / helper source: None found in C#/.t3 scan
- Purpose: Makes an incoming image tileable based on linear edge Falloff.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `EdgeFallOff`: float, default 0.2; Defines the length / size of the transition
  - `ImageA`: Texture2D, default Unknown; semantic role Unknown
  - `IsEnabled`: bool, default True; semantic role Unknown
  - `TilingMode`: int, default 3; Defines how the image is tiled:
0 = No Tiling
1 = horizontal tiling only
2 = vertical tiling only
3 = Vertical and horizontal tiling
- Outputs:
  - `Selected`: Texture2D, output image/data role
- Runtime behavior:
  - C# file mainly declares slots; `.t3` graph/defaults should be inspected for exact runtime composition. No shader file was found by the local scan.
  - important edge cases: Unknown beyond documented ports
- Observed graph usage:
  - common incoming nodes: Lib.flow.Once (9), Lib.image.generate.noise.FractalNoise (7), Lib.image.use.Blend (2), Lib.render._dx11.fxsetup.ExecuteTextureUpdate (1), Lib.image.generate.noise.WorleyNoise (1)
  - common outgoing nodes: Lib.image.transform.MakeTileableImage (6), Lib.image.transform.MakeTileableImage (5), Lib.image.transform.MakeTileableImage (3), Lib.image.transform.MakeTileableImage (2), Lib.image.transform.MakeTileableImage (2)
- Vuo mapping:
  - Vuo input types: EdgeFallOff -> VuoReal; ImageA -> VuoImage; IsEnabled -> VuoBoolean; TilingMode -> VuoInteger / enum
  - Vuo output types: Selected -> VuoImage
  - direct built-in Vuo equivalent, if any: vuo.image.tileable
  - missing Vuo support: mostly type/default parity and composition details
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## MirrorRepeat

- TiXL full path: `Lib.image.transform.MirrorRepeat`
- Namespace: `Lib.image.transform`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/transform/MirrorRepeat.cs`
  - .t3 defaults: `Operators/Lib/image/transform/MirrorRepeat.t3`
  - docs: `.help/docs/operators/lib/image/transform/MirrorRepeat.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/fx/MirrorRepeat.hlsl`
- Purpose: Shifts, slices or mirrors the incoming image to create endless textures or kaleidoscopic patterns when combined with itself A more sophisticated version: [KochKaleidoskope] For simpler transformations: [TransformImage] S...
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `Image`: Texture2D, default Unknown; Image input
  - `Offset`: float, default 0.0; Offsets the result along the mirror axis
  - `OffsetEdge`: float, default 0.0; semantic role Unknown
  - `Offsetimage`: Vector2, default {'X': 0.0, 'Y': 0.0}; Offsets the image before it is mirrored
  - `Resolution`: Int2, default {'X': -1, 'Y': -1}; Defines the resolution of the result in pixels
  - `RotateImage`: float, default 0.0; Rotates the incoming image before it is mirrored
  - `RotateMirror`: float, default 0.0; Rotates the result
  - `ShadeAmount`: float, default 0.0; Darkens one side of the mirrored image
Hint: If combined with a small width it creates a 'Fanfold' effect
  - `ShadeColor`: Vector4, default {'X': 1e-06, 'Y': 9.999922e-07, 'Z': 9.9999e-07, 'W': 1.0}; Defines the color that is used for shading
  - `Width`: float, default 1.0; Defines the distance between the mirrors. A smaller distance means many mirrored stripes.
- Outputs:
  - `TextureOutput`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - DX11/app-specific evidence: RenderTarget
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: Lib.numbers.anim._obsolete.Counter (10), Lib.image.fx.glitch.SortPixelGlitch (5), Lib.numbers.anim.time.Time (3), Lib.image.generate.basic.RoundedRect (3), Lib.image.generate.basic.RenderTarget (3)
  - common outgoing nodes: Lib.image.transform.MirrorRepeat (8), Lib.image.transform.MirrorRepeat (5), Lib.image.transform.MirrorRepeat (3), Lib.image.transform.MirrorRepeat (3), Lib.image.transform.MirrorRepeat (3)
- Vuo mapping:
  - Vuo input types: Image -> VuoImage; Offset -> VuoReal; OffsetEdge -> VuoReal; Offsetimage -> VuoPoint2d or integer pair; Resolution -> VuoPoint2d or integer pair; RotateImage -> VuoReal; RotateMirror -> VuoReal; ShadeAmount -> VuoReal; ShadeColor -> VuoColor or VuoPoint4d; Width -> VuoReal
  - Vuo output types: TextureOutput -> VuoImage
  - direct built-in Vuo equivalent, if any: vuo.image.mirror / tile (partial)
  - missing Vuo support: HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## TransformImage

- TiXL full path: `Lib.image.transform.TransformImage`
- Namespace: `Lib.image.transform`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/transform/TransformImage.cs`
  - .t3 defaults: `Operators/Lib/image/transform/TransformImage.t3`
  - docs: `.help/docs/operators/lib/image/transform/TransformImage.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/fx/TransformImage.hlsl`
- Purpose: Rotates, offsets, and scales the incoming image.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `Filter`: Filter, default MinMagMipLinear; Defines the image filtering used
  - `GenerateMips`: bool, default False; Defines whether mipmaps should be generated
  - `Image`: Texture2D, default Unknown; semantic role Unknown
  - `Offset`: Vector2, default {'X': 0.0, 'Y': 0.0}; Moves the incoming image
  - `Resolution`: Int2, default {'X': 0, 'Y': 0}; Overwrites the resolution of the incoming image
  - `ResolutionFactor`: Vector2, default {'X': 1.0, 'Y': 1.0}; This can be useful to scale down and
  - `Rotation`: float, default 0.0; Rotates the incoming image
  - `Scale`: float, default 1.0; Linearly scales the incoming image
  - `Stretch`: Vector2, default {'X': 1.0, 'Y': 1.0}; Scales the incoming image in the following directions:
X: Width
Y: Height
  - `WrapMode`: int, default 2, enum: Wrap, Mirror, Clamp, Border, MirrorOnce; Defines how the area around the image will be rendered
- Outputs:
  - `TextureOutput`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - DX11/app-specific evidence: PixelShader
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: Lib.image.generate.load.LoadImage (5), Lib.image.use.BlendWithMask (3), Lib.image.use.Blend (3), Lib.image.transform.MakeTileableImage (2), Lib.image.transform.MakeTileableImageAdvanced (2)
  - common outgoing nodes: Lib.image.transform.TransformImage (9), Lib.image.transform.TransformImage (8), Lib.image.transform.TransformImage (6), Lib.image.transform.TransformImage (5), Lib.image.transform.TransformImage (5)
- Vuo mapping:
  - Vuo input types: Filter -> Unknown / custom; GenerateMips -> VuoBoolean; Image -> VuoImage; Offset -> VuoPoint2d or integer pair; Resolution -> VuoPoint2d or integer pair; ResolutionFactor -> VuoPoint2d or integer pair; Rotation -> VuoReal; Scale -> VuoReal; Stretch -> VuoPoint2d or integer pair; WrapMode -> VuoInteger / enum
  - Vuo output types: TextureOutput -> VuoImage
  - direct built-in Vuo equivalent, if any: vuo.image.resize/rotate/translate/tile (partial)
  - missing Vuo support: HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## Blend

- TiXL full path: `Lib.image.use.Blend`
- Namespace: `Lib.image.use`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/use/Blend.cs`
  - .t3 defaults: `Operators/Lib/image/use/Blend.t3`
  - docs: `.help/docs/operators/lib/image/use/Blend.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/fx/Blend.hlsl`
- Purpose: Blends two images.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `AlphaMode`: int, default 0, enum: Normal = 0, Multiply = 1, SetToOne = 2, UseImageA_Alpha = 3, UseImageB_Alpha = 4, UseImageA_Brightness = 5, UseImageB_Brightness = 6, Additive = 7, Max = 8; Various modes for how the alpha channels are combined.
  - `BlendMode`: int, default 0, enum: Normal = 0, Screen = 1, Multiply = 2, Overlay = 3, Difference = 4, UseImageA_RGB = 5, UseImageB_RGB = 6, Max = 7, Sub = 8, MixUsingImageB_A = 9; Various blending modes for the colors.
  - `ColorA`: Vector4, default {'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}; A color multiplied onto the background image
  - `ColorB`: Vector4, default {'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}; An optional color multiplied. The alpha channel can be used to fade out the image.
Consider connecting this to a [SampleGradient].
  - `GenerateMips`: bool, default False; Generated MipMap levels. Please read the "Realtime Rendering for Artists" wiki page for mode details.
  - `ImageA`: Texture2D, default Unknown; The background image defining the resolution of the output.
  - `ImageB`: Texture2D, default Unknown; The image blended on top of the background image.
  - `NormalForUpperHalf`: bool, default False; If used with blend modes other than normal, that blend mode will only be used if the alpha channel is below 0.5.
Above that, the image will be fully blended with normal mode.
As an example: If blended with Screen mode...
- Alpha = 0.0: none of the image will be visible
- Alpha = 0.5: the image will be fully visible with screen mode
- Alpha = 1.0: the image will be fully visible with normal mode
  - `Resolution`: Int2, default {'X': 0, 'Y': 0}; The target resolution. Please make sure to check the documentation on how T3 handles resolutions.
  - `ScaleMode`: int, default 0, enum: Stretch = 0, Fit = 1, Cover = 2; semantic role Unknown
- Outputs:
  - `Output`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - DX11/app-specific evidence: RenderTarget, PixelShader
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: Lib.image.generate.basic.RenderTarget (28), Lib.image.generate.basic.LinearGradient (27), Lib.image.use.Blend (15), Lib.image.generate.basic.RadialGradient (14), Lib.image.generate.basic.RoundedRect (14)
  - common outgoing nodes: Lib.image.use.Blend (17), Lib.image.use.Blend (15), Lib.image.use.Blend (9), Lib.image.use.Blend (7), Lib.image.use.Blend (6)
- Vuo mapping:
  - Vuo input types: AlphaMode -> VuoInteger / enum; BlendMode -> VuoInteger / enum; ColorA -> VuoColor or VuoPoint4d; ColorB -> VuoColor or VuoPoint4d; GenerateMips -> VuoBoolean; ImageA -> VuoImage; ImageB -> VuoImage; NormalForUpperHalf -> VuoBoolean; Resolution -> VuoPoint2d or integer pair; ScaleMode -> VuoInteger / enum
  - Vuo output types: Output -> VuoImage
  - direct built-in Vuo equivalent, if any: vuo.image.blend
  - missing Vuo support: HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## BlendImages

- TiXL full path: `Lib.image.use.BlendImages`
- Namespace: `Lib.image.use`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/use/BlendImages.cs`
  - .t3 defaults: `Operators/Lib/image/use/BlendImages.t3`
  - docs: `.help/docs/operators/lib/image/use/BlendImages.md`
  - related shader / helper source: None found in C#/.t3 scan
- Purpose: Blends the connected input images with cross-fading and using a float index.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `BlendFraction`: float, default 0.0; semantic role Unknown
  - `Input`: Texture2D, default Unknown; semantic role Unknown
  - `Resolution`: Int2, default {'X': 0, 'Y': 0}; semantic role Unknown
- Outputs:
  - `OutputImage`: Texture2D, output image/data role
- Runtime behavior:
  - C# file mainly declares slots; `.t3` graph/defaults should be inspected for exact runtime composition. No shader file was found by the local scan.
  - DX11/app-specific evidence: RenderTarget
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: Lib.image.use.Blend (1), Lib.image.use.BlendWithMask (1), Lib.image.generate.basic.RenderTarget (1), Remap (1)
  - common outgoing nodes: Lib.image.use.BlendImages (2), Lib.image.use.BlendImages (1), Lib.image.use.BlendImages (1), Lib.image.use.BlendImages (1)
- Vuo mapping:
  - Vuo input types: BlendFraction -> VuoReal; Input -> VuoImage; Resolution -> VuoPoint2d or integer pair
  - Vuo output types: OutputImage -> VuoImage
  - direct built-in Vuo equivalent, if any: no single node; list select + blend
  - missing Vuo support: mostly type/default parity and composition details
- Porting grade:
  - B: simple VuoImage selection/list/control behavior; no shader ref found in C#/.t3
- First implementation recommendation: Implement as Vuo composition/helper after defining list/event behavior.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## BlendWithMask

- TiXL full path: `Lib.image.use.BlendWithMask`
- Namespace: `Lib.image.use`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/use/BlendWithMask.cs`
  - .t3 defaults: `Operators/Lib/image/use/BlendWithMask.t3`
  - docs: `.help/docs/operators/lib/image/use/BlendWithMask.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/fx/BlendWithMask.hlsl`, `Operators/Lib/Assets/shaders/img/fx/Default2-vs.hlsl`
- Purpose: Blends two images by the brightness of a 3rd mask image.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `ColorA`: Vector4, default {'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}; semantic role Unknown
  - `ColorB`: Vector4, default {'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}; semantic role Unknown
  - `ImageA`: Texture2D, default Unknown; semantic role Unknown
  - `ImageB`: Texture2D, default Unknown; semantic role Unknown
  - `Mask`: Texture2D, default Unknown; semantic role Unknown
  - `Resolution`: Int2, default {'X': 0, 'Y': 0}; semantic role Unknown
- Outputs:
  - `Output`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - DX11/app-specific evidence: RenderTarget
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: Lib.image.generate.basic.LinearGradient (18), Lib.image.generate.basic.RenderTarget (12), Lib.image.transform.TransformImage (9), Lib.image.generate.load.LoadImage (6), Lib.image.generate.basic.RoundedRect (6)
  - common outgoing nodes: Lib.image.use.BlendWithMask (6), Lib.image.use.BlendWithMask (3), Lib.image.use.BlendWithMask (3), Lib.image.use.BlendWithMask (3), Lib.image.use.BlendWithMask (3)
- Vuo mapping:
  - Vuo input types: ColorA -> VuoColor or VuoPoint4d; ColorB -> VuoColor or VuoPoint4d; ImageA -> VuoImage; ImageB -> VuoImage; Mask -> VuoImage; Resolution -> VuoPoint2d or integer pair
  - Vuo output types: Output -> VuoImage
  - direct built-in Vuo equivalent, if any: vuo.image.apply.mask + blend (partial)
  - missing Vuo support: HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## Combine3Images

- TiXL full path: `Lib.image.use.Combine3Images`
- Namespace: `Lib.image.use`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/use/Combine3Images.cs`
  - .t3 defaults: `Operators/Lib/image/use/Combine3Images.t3`
  - docs: `.help/docs/operators/lib/image/use/Combine3Images.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/use/img-combine-3.hlsl`
- Purpose: A node to combine 3 input images into the RGBA channels of a new one.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `ColorA`: Vector4, default {'X': 1.0, 'Y': 0.0, 'Z': 0.0, 'W': 1.0}; semantic role Unknown
  - `ColorB`: Vector4, default {'X': 0.0, 'Y': 1.0, 'Z': 0.0, 'W': 1.0}; semantic role Unknown
  - `ColorC`: Vector4, default {'X': 0.0, 'Y': 0.0, 'Z': 1.0, 'W': 1.0}; semantic role Unknown
  - `GenerateMips`: bool, default False; semantic role Unknown
  - `ImageA`: Texture2D, default Unknown; semantic role Unknown
  - `ImageB`: Texture2D, default Unknown; semantic role Unknown
  - `ImageC`: Texture2D, default Unknown; semantic role Unknown
  - `SelectAlphaChannel`: int, default 4, enum: UseImageA_Alpha = 0, UseImageB_Alpha = 1, UseImageC_Alpha = 2, SetToZero = 3, SetToOne = 4; semantic role Unknown
  - `SelectChannel_B`: int, default 12, enum: ImageA_R = 0, ImageA_G = 1, ImageA_B = 2, ImageA_Average = 3, ImageA_Brightness = 4, ImageB_R = 5, ImageB_G = 6, ImageB_B = 7, ImageB_Average = 8, ImageB_Brightness = 9, ImageC_R = 10, ImageC_G = 11, ImageC_B = 12, ImageC_Average = 13, ImageC_Brightness = 14; semantic role Unknown
  - `SelectChannel_G`: int, default 6, enum: ImageA_R = 0, ImageA_G = 1, ImageA_B = 2, ImageA_Average = 3, ImageA_Brightness = 4, ImageB_R = 5, ImageB_G = 6, ImageB_B = 7, ImageB_Average = 8, ImageB_Brightness = 9, ImageC_R = 10, ImageC_G = 11, ImageC_B = 12, ImageC_Average = 13, ImageC_Brightness = 14; semantic role Unknown
  - `SelectChannel_R`: int, default 0, enum: ImageA_R = 0, ImageA_G = 1, ImageA_B = 2, ImageA_Average = 3, ImageA_Brightness = 4, ImageB_R = 5, ImageB_G = 6, ImageB_B = 7, ImageB_Average = 8, ImageB_Brightness = 9, ImageC_R = 10, ImageC_G = 11, ImageC_B = 12, ImageC_Average = 13, ImageC_Brightness = 14; semantic role Unknown
- Outputs:
  - `Output`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: Lib.image.generate.basic.RadialGradient (26), Lib.image.generate.basic.RoundedRect (14), Lib.image.generate.basic.LinearGradient (13), Lib.image.generate._obsolete._BlobOld (10), _trippleImageFxSetup (1)
  - common outgoing nodes: Lib.image.use.Combine3Images (23), Lib.image.use.Combine3Images (4), Lib.image.use.Combine3Images (4), Lib.image.use.Combine3Images (3), Lib.image.use.Combine3Images (2)
- Vuo mapping:
  - Vuo input types: ColorA -> VuoColor or VuoPoint4d; ColorB -> VuoColor or VuoPoint4d; ColorC -> VuoColor or VuoPoint4d; GenerateMips -> VuoBoolean; ImageA -> VuoImage; ImageB -> VuoImage; ImageC -> VuoImage; SelectAlphaChannel -> VuoInteger / enum; SelectChannel_B -> VuoInteger / enum; SelectChannel_G -> VuoInteger / enum
  - Vuo output types: Output -> VuoImage
  - direct built-in Vuo equivalent, if any: compose vuo.image.blend
  - missing Vuo support: HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## CustomPixelShader

- TiXL full path: `Lib.image.use.CustomPixelShader`
- Namespace: `Lib.image.use`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/use/CustomPixelShader.cs`
  - .t3 defaults: `Operators/Lib/image/use/CustomPixelShader.t3`
  - docs: `.help/docs/operators/lib/image/use/CustomPixelShader.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/fx/Default2-vs.hlsl`, `Operators/Lib/Assets/shaders/img/use/CustomImageShader-template.hlsl`
- Purpose: Creates a custom shader from a source parameter.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor. TiXL Gradient has no direct Vuo type found in source scan; model as color/position lists or custom gradient helper.
- Inputs:
  - `A`: float, default 0.0; semantic role Unknown
  - `AdditionalCode`: string, default ; semantic role Unknown
  - `B`: float, default 0.0; semantic role Unknown
  - `C`: float, default 0.0; semantic role Unknown
  - `Clear`: bool, default True; Clearing RenderTarget
setting it to false also enables blending mode, so that result is blended on top of backbuffer
  - `ConstantBuffers`: Direct3D11.Buffer, default Unknown; Use FloatsToBuffer or Ints to buffer to pass additional input params, define in AdditionalCode for example:
cbuffer AdditionalIntParams : register(b2)
{
    int Index;
}
cbuffer AdditionalFloatParams : register(b3)
{
    float4 Color;
}
  - `CustomSampler`: Direct3D11.SamplerState, default Unknown; Use SamplerState node to add custom sampler(s); first sampler input is defined in template as CustomSampler.
samplers coming after that will need to be defined in AdditionalCode, for example:
sampler Sampler2 : register(s2);
  - `D`: float, default 0.0; semantic role Unknown
  - `GainAndBias`: Vector2, default {'X': 0.5, 'Y': 0.5}; semantic role Unknown
  - `GenerateMips`: bool, default False; semantic role Unknown
  - `Gradient`: Gradient, default {'Gradient': {'Interpolation': 'Linear', 'Steps': [{'Id': '070f95dc-a8e9-46b0-8957-50c3d34a253e', 'NormalizedPosition': 0.0, 'Color': {'R': 0.0, 'G': 0.0, 'B': 0.0, 'A': 1.0}}, {'Id': 'f89933a8-d6bb-4e1e-a495-bd735c86b47b', 'NormalizedPosition': 1.0, 'Color': {'R': 1.0, 'G': 0.99999, 'B': 1.0, 'A': 1.0}}]}}; semantic role Unknown
  - `ImageA`: Texture2D, default Unknown; semantic role Unknown
  - `ImageB`: Texture2D, default Unknown; semantic role Unknown
  - `Offset`: Vector2, default {'X': 0.0, 'Y': 0.0}; semantic role Unknown
  - `Resolution`: Vector.Int2, default {'X': 0, 'Y': 0}; semantic role Unknown
  - `ShaderCode`: string, default float d= 1 - length(uv - 0.5 - Offset * float2(1,-1));
d = ApplyGainAndBias(d, GainAndBias);

c= ImageA.Sample(Sampler, uv);
c.rgb*= Gradient.SampleLevel( ClampedSampler, float2(d, 0.5),0); ; semantic role Unknown
  - `ShaderResources`: Direct3D11.ShaderResourceView, default Unknown; Use SrvFromTexutre2d for additional texture inputs. They will need to be defined in AdditionalCode, for example:
Texture2D<float4> Image3 : register(t2);
  - `TemplateFile`: string, default Lib:shaders/img/use/CustomImageShader-template.hlsl; semantic role Unknown
  - `TextureFormat`: DXGI.Format, default R16G16B16A16_Float; semantic role Unknown
- Outputs:
  - `ShaderCode_`: string, output image/data role
  - `TextureOutput`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - DX11/app-specific evidence: Direct3D11, ShaderResourceView, RenderTarget, PixelShader, VertexShader, SamplerState, DXGI.Format, SharpDX
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: Lib.image.use.CustomPixelShader (21), ImageFFT (17), Lib.image.generate.load.LoadImage (8), Lib.image.transform.TransformImage (6), Lib.image.generate.basic.RenderTarget (5)
  - common outgoing nodes: Lib.image.use.CustomPixelShader (21), Lib.image.use.CustomPixelShader (15), Lib.image.use.CustomPixelShader (4), Lib.image.use.CustomPixelShader (4), Lib.image.use.CustomPixelShader (4)
- Vuo mapping:
  - Vuo input types: A -> VuoReal; AdditionalCode -> Unknown / custom; B -> VuoReal; C -> VuoReal; Clear -> VuoBoolean; ConstantBuffers -> Unknown / custom; CustomSampler -> Unknown / custom; D -> VuoReal; GainAndBias -> VuoPoint2d or integer pair; GenerateMips -> VuoBoolean
  - Vuo output types: ShaderCode_ -> Unknown / custom; TextureOutput -> VuoImage
  - direct built-in Vuo equivalent, if any: Vuo ISF / shader node, not direct
  - missing Vuo support: HLSL must be translated or replaced; first-pass D dependency must be designed away or postponed
- Porting grade:
  - D: internal/obsolete/DX11-heavy first-pass document-only node; DX11/app terms: Direct3D11, ShaderResourceView, RenderTarget, PixelShader, VertexShader; shader/compute dependency
- First implementation recommendation: Document and postpone until renderer/state/DX11 replacement strategy is chosen.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## FirstValidTexture

- TiXL full path: `Lib.image.use.FirstValidTexture`
- Namespace: `Lib.image.use`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/use/FirstValidTexture.cs`
  - .t3 defaults: `Operators/Lib/image/use/FirstValidTexture.t3`
  - docs: `.help/docs/operators/lib/image/use/FirstValidTexture.md`
  - related shader / helper source: None found in C#/.t3 scan
- Purpose: *No description yet.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `Input`: Texture2D, default Unknown; semantic role Unknown
- Outputs:
  - `Output`: Texture2D, output image/data role
- Runtime behavior:
  - C# file mainly declares slots; `.t3` graph/defaults should be inspected for exact runtime composition. No shader file was found by the local scan.
  - DX11/app-specific evidence: RenderTarget, PixelShader
  - important edge cases: Unknown beyond documented ports
- Observed graph usage:
  - common incoming nodes: Lib.image.generate.load.LoadImage (6), Lib.image.generate.basic.RenderTarget (3), Lib.image.fx.stylize.MosiacTiling (2), ImageQuiz (1), Lib.image.fx.glitch.SubdivisionStretch (1)
  - common outgoing nodes: Lib.image.use.FirstValidTexture (6), Lib.image.use.FirstValidTexture (2), Lib.image.use.FirstValidTexture (2), Lib.image.use.FirstValidTexture (2), Lib.image.use.FirstValidTexture (2)
- Vuo mapping:
  - Vuo input types: Input -> VuoImage
  - Vuo output types: Output -> VuoImage
  - direct built-in Vuo equivalent, if any: Vuo selection/list logic + VuoImage
  - missing Vuo support: mostly type/default parity and composition details
- Porting grade:
  - B: simple VuoImage selection/list/control behavior; no shader ref found in C#/.t3
- First implementation recommendation: Implement as Vuo composition/helper after defining list/event behavior.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## Fxaa

- TiXL full path: `Lib.image.use.Fxaa`
- Namespace: `Lib.image.use`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/use/Fxaa.cs`
  - .t3 defaults: `Operators/Lib/image/use/Fxaa.t3`
  - docs: `.help/docs/operators/lib/image/use/Fxaa.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/fx/Default2-vs.hlsl`, `Operators/Lib/Assets/shaders/img/use/FXAA.hlsl`
- Purpose: Fast approXimate Anti-Aliasing is a post-FX, use it to improve SDF / RayMarching.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `Image`: Texture2D, default Unknown; semantic role Unknown
  - `KeepAlpha`: bool, default False; semantic role Unknown
  - `Preset`: int, default 0; semantic role Unknown
- Outputs:
  - `TextureOutput`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - DX11/app-specific evidence: RenderTarget
  - important edge cases: Unknown beyond documented ports
- Observed graph usage:
  - common incoming nodes: Lib.image.generate.basic.RenderTarget (2)
  - common outgoing nodes: Lib.image.use.Fxaa (1), Lib.image.use.Fxaa (1), Lib.image.use.Fxaa (1), Lib.image.use.Fxaa (1), Lib.image.use.Fxaa (1)
- Vuo mapping:
  - Vuo input types: Image -> VuoImage; KeepAlpha -> VuoBoolean; Preset -> VuoInteger / enum
  - Vuo output types: TextureOutput -> VuoImage
  - direct built-in Vuo equivalent, if any: no direct FXAA node found
  - missing Vuo support: direct built-in equivalent not found; HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; likely needs ISF/Metal/GLSL or Vuo-specific renderer design
- First implementation recommendation: Write an ISF/Metal/GLSL parity shader after freezing TiXL defaults and a fixture image.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## KeepPreviousFrame

- TiXL full path: `Lib.image.use.KeepPreviousFrame`
- Namespace: `Lib.image.use`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/use/KeepPreviousFrame.cs`
  - .t3 defaults: `Operators/Lib/image/use/KeepPreviousFrame.t3`
  - docs: `.help/docs/operators/lib/image/use/KeepPreviousFrame.md`
  - related shader / helper source: None found in C#/.t3 scan
- Purpose: *No description yet.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `ImageA`: Texture2D, default Unknown; semantic role Unknown
  - `Keep`: bool, default Unknown; semantic role Unknown
- Outputs:
  - `CurrentFrame`: Texture2D, output image/data role
  - `PreviousFrame`: Texture2D, output image/data role
- Runtime behavior:
  - C# file mainly declares slots; `.t3` graph/defaults should be inspected for exact runtime composition. No shader file was found by the local scan.
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: HasIntChanged (2), Any (1), DetectMotion (1), Lib.image.analyze.RemoveStaticBackground (1), MaskRaw (1)
  - common outgoing nodes: Lib.image.use.KeepPreviousFrame (6), Lib.image.use.KeepPreviousFrame (2), Lib.image.use.KeepPreviousFrame (1), Lib.image.use.KeepPreviousFrame (1)
- Vuo mapping:
  - Vuo input types: ImageA -> VuoImage; Keep -> VuoBoolean
  - Vuo output types: CurrentFrame -> VuoImage; PreviousFrame -> VuoImage
  - direct built-in Vuo equivalent, if any: vuo.image.feedback / hold value (partial)
  - missing Vuo support: mostly type/default parity and composition details
- Porting grade:
  - C: image/shader node; Vuo built-in mapping exists or is partial
- First implementation recommendation: First try Vuo built-in node parity fixture; only port shader where the TiXL surface differs materially.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## NormalMap

- TiXL full path: `Lib.image.use.NormalMap`
- Namespace: `Lib.image.use`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/use/NormalMap.cs`
  - .t3 defaults: `Operators/Lib/image/use/NormalMap.t3`
  - docs: `.help/docs/operators/lib/image/use/NormalMap.md`
  - related shader / helper source: `Operators/Lib/Assets/shaders/img/fx/NormalMap.hlsl`
- Purpose: Converts the brightness of an image into a normal map that can be used with [SetMaterial].
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `Impact`: float, default 1.0; semantic role Unknown
  - `LightMap`: Texture2D, default Unknown; semantic role Unknown
  - `Mode`: int, default 0, enum: Gray_ToNormalizedRGB, Gray_ToNormalizedRGBSigned, Gray_ToAngleAndMagnitude, Red_ToRG_KeepBA; semantic role Unknown
  - `OutputFormat`: Format, default R16G16B16A16_Float; semantic role Unknown
  - `Resolution`: Int2, default {'X': 0, 'Y': 0}; semantic role Unknown
  - `SampleRadius`: float, default 2.0; semantic role Unknown
  - `TextureRepeat`: Direct3D11.TextureAddressMode, default MirrorOnce; semantic role Unknown
  - `Twist`: float, default 180.0; semantic role Unknown
- Outputs:
  - `Output`: Texture2D, output image/data role
- Runtime behavior:
  - GPU behavior is primarily in the listed HLSL shader(s), usually wired through the `.t3` graph.
  - DX11/app-specific evidence: Direct3D11, TextureReference, RenderTarget, SharpDX
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: Lib.image.use.Blend (3), Lib.image.transform.MakeTileableImage (3), Lib.image.generate._obsolete._FractalNoiseOld (3), Lib.image.color.AdjustColors (2), Lib.render._dx11.fxsetup._ImageFxShaderSetupStatic (1)
  - common outgoing nodes: Lib.image.use.NormalMap (18), Lib.image.use.NormalMap (7), Lib.image.use.NormalMap (3), Lib.image.use.NormalMap (2), Lib.image.use.NormalMap (1)
- Vuo mapping:
  - Vuo input types: Impact -> VuoReal; LightMap -> VuoImage; Mode -> VuoInteger / enum; OutputFormat -> Unknown / custom; Resolution -> VuoPoint2d or integer pair; SampleRadius -> VuoReal; TextureRepeat -> Unknown / custom; Twist -> VuoReal
  - Vuo output types: Output -> VuoImage
  - direct built-in Vuo equivalent, if any: no direct normal-map node
  - missing Vuo support: direct built-in equivalent not found; HLSL must be translated or replaced
- Porting grade:
  - C: image/shader node; likely needs ISF/Metal/GLSL or Vuo-specific renderer design
- First implementation recommendation: Write an ISF/Metal/GLSL parity shader after freezing TiXL defaults and a fixture image.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## PickTexture

- TiXL full path: `Lib.image.use.PickTexture`
- Namespace: `Lib.image.use`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/use/PickTexture.cs`
  - .t3 defaults: `Operators/Lib/image/use/PickTexture.t3`
  - docs: `.help/docs/operators/lib/image/use/PickTexture.md`
  - related shader / helper source: None found in C#/.t3 scan
- Purpose: Picks one of the connected textures.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `Index`: int, default 0; semantic role Unknown
  - `Input`: Texture2D, default Unknown; semantic role Unknown
- Outputs:
  - `Selected`: Texture2D, output image/data role
- Runtime behavior:
  - C# file mainly declares slots; `.t3` graph/defaults should be inspected for exact runtime composition. No shader file was found by the local scan.
  - DX11/app-specific evidence: RenderTarget, PixelShader
  - important edge cases: resolution/format/mipmap/state behavior must be matched explicitly
- Observed graph usage:
  - common incoming nodes: Lib.image.use.Blend (9), BoolToInt (8), Lib.image.generate.basic.RenderTarget (7), Lib.image.use.BlendWithMask (6), Lib.image.generate.load.LoadImage (6)
  - common outgoing nodes: Lib.image.use.PickTexture (4), Lib.image.use.PickTexture (3), Lib.image.use.PickTexture (3), Lib.image.use.PickTexture (3), Lib.image.use.PickTexture (3)
- Vuo mapping:
  - Vuo input types: Index -> VuoInteger / enum; Input -> VuoImage
  - Vuo output types: Selected -> VuoImage
  - direct built-in Vuo equivalent, if any: Vuo list/select logic + VuoImage
  - missing Vuo support: mostly type/default parity and composition details
- Porting grade:
  - B: simple VuoImage selection/list/control behavior; no shader ref found in C#/.t3
- First implementation recommendation: Implement as Vuo composition/helper after defining list/event behavior.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## UseFallbackTexture

- TiXL full path: `Lib.image.use.UseFallbackTexture`
- Namespace: `Lib.image.use`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `Operators/Lib/image/use/UseFallbackTexture.cs`
  - .t3 defaults: `Operators/Lib/image/use/UseFallbackTexture.t3`
  - docs: `.help/docs/operators/lib/image/use/UseFallbackTexture.md`
  - related shader / helper source: None found in C#/.t3 scan
- Purpose: Automatically replaces a non-loadable texture with a predefined backup.
- Conversion: Texture2D -> VuoImage; float -> VuoReal; int enum -> VuoInteger/menu; bool -> VuoBoolean; Vector2/Int2 -> VuoPoint2d or explicit width/height pair; Vector4 color-like slots need role check before mapping to VuoColor.
- Inputs:
  - `Fallback`: Texture2D, default Unknown; Input for Backup / Fallback texture
  - `TextureA`: Texture2D, default Unknown; Main Texture input
- Outputs:
  - `Output`: Texture2D, output image/data role
- Runtime behavior:
  - C# file mainly declares slots; `.t3` graph/defaults should be inspected for exact runtime composition. No shader file was found by the local scan.
  - DX11/app-specific evidence: RenderTarget, PixelShader
  - important edge cases: Unknown beyond documented ports
- Observed graph usage:
  - common incoming nodes: Lib.image.generate.load.LoadImage (31), Lib.image.generate.basic.RenderTarget (3), Lib.image.use.CustomPixelShader (2), Lib.image.generate.basic.Blob (2), Lib.image.fx.blur.DirectionalBlur (1)
  - common outgoing nodes: Lib.image.use.UseFallbackTexture (32), Lib.image.use.UseFallbackTexture (3), Lib.image.use.UseFallbackTexture (2), Lib.image.use.UseFallbackTexture (1), Lib.image.use.UseFallbackTexture (1)
- Vuo mapping:
  - Vuo input types: Fallback -> VuoImage; TextureA -> VuoImage
  - Vuo output types: Output -> VuoImage
  - direct built-in Vuo equivalent, if any: Vuo select logic + VuoImage
  - missing Vuo support: mostly type/default parity and composition details
- Porting grade:
  - B: simple VuoImage selection/list/control behavior; no shader ref found in C#/.t3
- First implementation recommendation: Implement as Vuo composition/helper after defining list/event behavior.
- Verification fixture: render a small fixed-resolution gradient/checker/noise/image input through TiXL and Vuo candidate, then compare dimensions, alpha behavior, and representative pixels; add animated/state frames when inputs include time/frame/reset/index.
- Risks / unknowns: exact `.t3` child graph parameters may alter shader defaults; Vuo color space and premultiplied alpha behavior must be checked; Unknown where source scan did not expose implementation details.

## First Batch Recommendation

1. `Lib.image.generate.basic.LinearGradient` — grade C; vuo.image.make.gradient.linear; image/shader node; Vuo built-in mapping exists or is partial
2. `Lib.image.generate.basic.RadialGradient` — grade C; vuo.image.make.gradient.radial; image/shader node; Vuo built-in mapping exists or is partial
3. `Lib.image.generate.basic.CheckerBoard` — grade C; vuo.image.make.checkerboard; image/shader node; Vuo built-in mapping exists or is partial
4. `Lib.image.generate.noise.FractalNoise` — grade C; vuo.image.make.noise (partial); image/shader node; Vuo built-in mapping exists or is partial
5. `Lib.image.use.Blend` — grade C; vuo.image.blend; image/shader node; Vuo built-in mapping exists or is partial
6. `Lib.image.use.BlendImages` — grade B; no single node; list select + blend; simple VuoImage selection/list/control behavior; no shader ref found in C#/.t3
7. `Lib.image.color.AdjustColors` — grade C; vuo.image.color.adjust (partial); image/shader node; Vuo built-in mapping exists or is partial
8. `Lib.image.fx.blur.Blur` — grade C; vuo.image.blur; image/shader node; Vuo built-in mapping exists or is partial
9. `Lib.image.transform.TransformImage` — grade C; vuo.image.resize/rotate/translate/tile (partial); image/shader node; Vuo built-in mapping exists or is partial
10. `Lib.image.transform.Crop` — grade C; vuo.image.crop / crop.pixels; image/shader node; Vuo built-in mapping exists or is partial

These ten cover procedural generation, compositing, color correction, blur, and transform/crop. They also have obvious Vuo built-in or partial built-in anchors, so they are the cleanest first pressure test before the heavier feedback/glitch/DX11 nodes.
