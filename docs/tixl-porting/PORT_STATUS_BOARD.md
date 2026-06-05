# TiXL -> Vuo Port Status Board

Generated from current repo evidence. Do not hand-edit table rows; run:

```bash
python3 docs/tixl-porting/scripts/generate_port_status_board.py --write
```

## Summary

- TiXL indexed nodes: 935
- Vuo custom node sources: 266
- Vuo proof compositions: 58
- Source inventory: `docs/tixl-porting/reports/source_inventory.md`
- Grade rules: `docs/tixl-porting/reports/porting_grade_rules.md`

## Current Vuo Node Evidence

| visible node | category | status | source |
|---|---|---|---|
| my_MainClock | My World runtime adapter | Vuo node + generated composition proof | `vuo-nodes/my.runtime.clock.mainClock.c` |
| my_CombineSDF | Operators/Lib/field/combine | Vuo node + composition proof | `vuo-nodes/my.field.combine.combineSdf.c` |
| my_BoxSDF | Operators/Lib/field/generate/sdf | Vuo node + composition proof | `vuo-nodes/my.field.generate.sdf.boxSdf.c` |
| my_SphereSDF | Operators/Lib/field/generate/sdf | Vuo node + composition proof | `vuo-nodes/my.field.generate.sdf.sphereSdf.c` |
| my_RaymarchField | Operators/Lib/field/render | Vuo node + composition proof | `vuo-nodes/my.field.render.raymarchField.c` |
| my_CompareImages | Operators/Lib/image/analyze | Vuo node + composition proof | `vuo-nodes/my.image.analyze.compareImages.c` |
| my_DetectMotion | Operators/Lib/image/analyze | Vuo node + composition proof | `vuo-nodes/my.image.analyze.detectMotion.c` |
| my_GetImageBrightness | Operators/Lib/image/analyze | Vuo node + composition proof | `vuo-nodes/my.image.analyze.getImageBrightness.c` |
| my_ImageLevels | Operators/Lib/image/analyze | Vuo node + composition proof | `vuo-nodes/my.image.analyze.imageLevels.c` |
| my_OpticalFlow | Operators/Lib/image/analyze | Vuo node + composition proof | `vuo-nodes/my.image.analyze.opticalFlow.c` |
| my_RemoveStaticBackground | Operators/Lib/image/analyze | Vuo node + composition proof | `vuo-nodes/my.image.analyze.removeStaticBackground.c` |
| my_WaveForm | Operators/Lib/image/analyze | Vuo node + composition proof | `vuo-nodes/my.image.analyze.waveForm.c` |
| my_AdjustColors | Operators/Lib/image/color | Vuo node + composition proof | `vuo-nodes/my.image.color.adjustColors.c` |
| my_ChannelMixer | Operators/Lib/image/color | Vuo node + composition proof | `vuo-nodes/my.image.color.channelMixer.c` |
| my_ColorGrade | Operators/Lib/image/color | Vuo node + composition proof | `vuo-nodes/my.image.color.colorGrade.c` |
| my_ColorGradeDepth | Operators/Lib/image/color | Vuo node + composition proof | `vuo-nodes/my.image.color.colorGradeDepth.c` |
| my_ConvertColors | Operators/Lib/image/color | Vuo node + composition proof | `vuo-nodes/my.image.color.convertColors.c` |
| my_ConvertFormat | Operators/Lib/image/color | Vuo node + composition proof | `vuo-nodes/my.image.color.convertFormat.c` |
| my_HSE | Operators/Lib/image/color | Vuo node + composition proof | `vuo-nodes/my.image.color.hse.c` |
| my_KeyColor | Operators/Lib/image/color | Vuo node + composition proof | `vuo-nodes/my.image.color.keyColor.c` |
| my_RemapColor | Operators/Lib/image/color | Vuo node + composition proof | `vuo-nodes/my.image.color.remapColor.c` |
| my_Tint | Operators/Lib/image/color | Vuo node + composition proof | `vuo-nodes/my.image.color.tint.c` |
| my_ToneMapping | Operators/Lib/image/color | Vuo node + composition proof | `vuo-nodes/my.image.color.toneMapping.c` |
| my_Bloom | Operators/Lib/image/fx/blur | Vuo node + composition proof | `vuo-nodes/my.image.fx.blur.bloom.c` |
| my_Blur | Operators/Lib/image/fx/blur | Vuo node + composition proof | `vuo-nodes/my.image.fx.blur.blur.c` |
| my_DirectionalBlur | Operators/Lib/image/fx/blur | Vuo node + composition proof | `vuo-nodes/my.image.fx.blur.directionalBlur.c` |
| my_FastBlur | Operators/Lib/image/fx/blur | Vuo node + composition proof | `vuo-nodes/my.image.fx.blur.fastBlur.c` |
| my_Sharpen | Operators/Lib/image/fx/blur | Vuo node + composition proof | `vuo-nodes/my.image.fx.blur.sharpen.c` |
| my_BubbleZoom | Operators/Lib/image/fx/distort | Vuo node + composition proof | `vuo-nodes/my.image.fx.distort.bubbleZoom.c` |
| my_ChromaticDistortion | Operators/Lib/image/fx/distort | Vuo node + composition proof | `vuo-nodes/my.image.fx.distort.chromaticDistortion.c` |
| my_Displace | Operators/Lib/image/fx/distort | Vuo node + composition proof | `vuo-nodes/my.image.fx.distort.displace.c` |
| my_DistortAndShade | Operators/Lib/image/fx/distort | Vuo node + composition proof | `vuo-nodes/my.image.fx.distort.distortAndShade.c` |
| my_EdgeRepeat | Operators/Lib/image/fx/distort | Vuo node + composition proof | `vuo-nodes/my.image.fx.distort.edgeRepeat.c` |
| my_FieldToImage | Operators/Lib/image/fx/distort | Vuo node + composition proof | `vuo-nodes/my.image.fx.distort.fieldToImage.c` |
| my_KochKaleidoskope | Operators/Lib/image/fx/distort | Vuo node + composition proof | `vuo-nodes/my.image.fx.distort.kochKaleidoskope.c` |
| my_PolarCoordinates | Operators/Lib/image/fx/distort | Vuo node + composition proof | `vuo-nodes/my.image.fx.distort.polarCoordinates.c` |
| my_TimeDisplace | Operators/Lib/image/fx/distort | Vuo node + composition proof | `vuo-nodes/my.image.fx.distort.timeDisplace.c` |
| my_GlitchDisplace | Operators/Lib/image/fx/glitch | Vuo node + composition proof | `vuo-nodes/my.image.fx.glitch.glitchDisplace.c` |
| my_RgbTV | Operators/Lib/image/fx/glitch | Vuo node + composition proof | `vuo-nodes/my.image.fx.glitch.rgbTv.c` |
| my_SortPixelGlitch | Operators/Lib/image/fx/glitch | Vuo node + composition proof | `vuo-nodes/my.image.fx.glitch.sortPixelGlitch.c` |
| my_SubdivisionStretch | Operators/Lib/image/fx/glitch | Vuo node + composition proof | `vuo-nodes/my.image.fx.glitch.subdivisionStretch.c` |
| my_AsciiRender | Operators/Lib/image/fx/stylize | Vuo node + composition proof | `vuo-nodes/my.image.fx.stylize.asciiRender.c` |
| my_ChromaticAbberation | Operators/Lib/image/fx/stylize | Vuo node + composition proof | `vuo-nodes/my.image.fx.stylize.chromaticAbberation.c` |
| my_ColorPhysarum | Operators/Lib/image/fx/stylize | Vuo node + composition proof | `vuo-nodes/my.image.fx.stylize.colorPhysarum.c` |
| my_DetectEdges | Operators/Lib/image/fx/stylize | Vuo node + composition proof | `vuo-nodes/my.image.fx.stylize.detectEdges.c` |
| my_Dither | Operators/Lib/image/fx/stylize | Vuo node + composition proof | `vuo-nodes/my.image.fx.stylize.dither.c` |
| my_FakeLight | Operators/Lib/image/fx/stylize | Vuo node + composition proof | `vuo-nodes/my.image.fx.stylize.fakeLight.c` |
| my_Glow | Operators/Lib/image/fx/stylize | Vuo node + composition proof | `vuo-nodes/my.image.fx.stylize.glow.c` |
| my_HoneyCombTiles | Operators/Lib/image/fx/stylize | Vuo node + composition proof | `vuo-nodes/my.image.fx.stylize.honeyCombTiles.c` |
| my_LightRaysFx | Operators/Lib/image/fx/stylize | Vuo node + composition proof | `vuo-nodes/my.image.fx.stylize.lightRaysFx.c` |
| my_MosiacTiling | Operators/Lib/image/fx/stylize | Vuo node + composition proof | `vuo-nodes/my.image.fx.stylize.mosiacTiling.c` |
| my_Pixelate | Operators/Lib/image/fx/stylize | Vuo node + composition proof | `vuo-nodes/my.image.fx.stylize.pixelate.c` |
| my_ScreenCloseUp | Operators/Lib/image/fx/stylize | Vuo node + composition proof | `vuo-nodes/my.image.fx.stylize.screenCloseUp.c` |
| my_StarGlowStreaks | Operators/Lib/image/fx/stylize | Vuo node + composition proof | `vuo-nodes/my.image.fx.stylize.starGlowStreaks.c` |
| my_Steps | Operators/Lib/image/fx/stylize | Vuo node + composition proof | `vuo-nodes/my.image.fx.stylize.steps.c` |
| my_VoronoiCells | Operators/Lib/image/fx/stylize | Vuo node + composition proof | `vuo-nodes/my.image.fx.stylize.voronoiCells.c` |
| my_MunchingSquares2 | Operators/Lib/image/generate | Vuo node + composition proof | `vuo-nodes/my.image.generate.munchingSquares2.c` |
| my_Blob | Operators/Lib/image/generate/basic | Vuo node + composition proof | `vuo-nodes/my.image.generate.basic.blob.c` |
| my_BoxGradient | Operators/Lib/image/generate/basic | Vuo node + composition proof | `vuo-nodes/my.image.generate.basic.boxGradient.c` |
| my_CheckerBoard | Operators/Lib/image/generate/basic | Vuo node + composition proof | `vuo-nodes/my.image.generate.basic.checkerBoard.c` |
| my_ConstantImage | Operators/Lib/image/generate/basic | Vuo node + composition proof | `vuo-nodes/my.image.generate.basic.constantImage.c` |
| my_LinearGradient | Operators/Lib/image/generate/basic | Vuo node + composition proof | `vuo-nodes/my.image.generate.basic.linearGradient.c` |
| my_NGon | Operators/Lib/image/generate/basic | Vuo node + composition proof | `vuo-nodes/my.image.generate.basic.nGon.c` |
| my_NGonGradient | Operators/Lib/image/generate/basic | Vuo node + composition proof | `vuo-nodes/my.image.generate.basic.nGonGradient.c` |
| my_RadialGradient | Operators/Lib/image/generate/basic | Vuo node + composition proof | `vuo-nodes/my.image.generate.basic.radialGradient.c` |
| my_RenderTarget | Operators/Lib/image/generate/basic | Vuo node + composition proof | `vuo-nodes/my.image.generate.basic.renderTarget.c` |
| my_RoundedRect | Operators/Lib/image/generate/basic | Vuo node + composition proof | `vuo-nodes/my.image.generate.basic.roundedRect.c` |
| my_MandelbrotFractal | Operators/Lib/image/generate/fractal | Vuo node + composition proof | `vuo-nodes/my.image.generate.fractal.mandelbrotFractal.c` |
| my_ImageSequenceClip | Operators/Lib/image/generate/load | Vuo node + composition proof | `vuo-nodes/my.image.generate.load.imageSequenceClip.c` |
| my_LoadImage | Operators/Lib/image/generate/load | Vuo node + composition proof | `vuo-nodes/my.image.generate.load.loadImage.c` |
| my_LoadImageFromUrl | Operators/Lib/image/generate/load | Vuo node + composition proof | `vuo-nodes/my.image.generate.load.loadImageFromUrl.c` |
| my_LoadSvgAsTexture2D | Operators/Lib/image/generate/load | Vuo node + composition proof | `vuo-nodes/my.image.generate.load.loadSvgAsTexture2D.c` |
| my_JumpFloodFill | Operators/Lib/image/generate/misc | Vuo node + composition proof | `vuo-nodes/my.image.generate.misc.jumpFloodFill.c` |
| my_Sketch | Operators/Lib/image/generate/misc | Vuo node + composition proof | `vuo-nodes/my.image.generate.misc.sketch.c` |
| my_SlidingHistory | Operators/Lib/image/generate/misc | Vuo node + composition proof | `vuo-nodes/my.image.generate.misc.slidingHistory.c` |
| my_FractalNoise | Operators/Lib/image/generate/noise | Vuo node + composition proof | `vuo-nodes/my.image.generate.noise.fractalNoise.c` |
| my_Grain | Operators/Lib/image/generate/noise | Vuo node + composition proof | `vuo-nodes/my.image.generate.noise.grain.c` |
| my_ShardNoise | Operators/Lib/image/generate/noise | Vuo node + composition proof | `vuo-nodes/my.image.generate.noise.shardNoise.c` |
| my_TileableNoise | Operators/Lib/image/generate/noise | Vuo node + composition proof | `vuo-nodes/my.image.generate.noise.tileableNoise.c` |
| my_WorleyNoise | Operators/Lib/image/generate/noise | Vuo node + composition proof | `vuo-nodes/my.image.generate.noise.worleyNoise.c` |
| my_FraserGrid | Operators/Lib/image/generate/pattern | Vuo node + composition proof | `vuo-nodes/my.image.generate.pattern.fraserGrid.c` |
| my_NumberPattern | Operators/Lib/image/generate/pattern | Vuo node + composition proof | `vuo-nodes/my.image.generate.pattern.numberPattern.c` |
| my_Raster | Operators/Lib/image/generate/pattern | Vuo node + composition proof | `vuo-nodes/my.image.generate.pattern.raster.c` |
| my_Rings | Operators/Lib/image/generate/pattern | Vuo node + composition proof | `vuo-nodes/my.image.generate.pattern.rings.c` |
| my_RyojiPattern1 | Operators/Lib/image/generate/pattern | Vuo node + composition proof | `vuo-nodes/my.image.generate.pattern.ryojiPattern1.c` |
| my_RyojiPattern2 | Operators/Lib/image/generate/pattern | Vuo node + composition proof | `vuo-nodes/my.image.generate.pattern.ryojiPattern2.c` |
| my_SinForm | Operators/Lib/image/generate/pattern | Vuo node + composition proof | `vuo-nodes/my.image.generate.pattern.sinForm.c` |
| my_ValueRaster | Operators/Lib/image/generate/pattern | Vuo node + composition proof | `vuo-nodes/my.image.generate.pattern.valueRaster.c` |
| my_ZollnerPattern | Operators/Lib/image/generate/pattern | Vuo node + composition proof | `vuo-nodes/my.image.generate.pattern.zollnerPattern.c` |
| my_Crop | Operators/Lib/image/transform | Vuo node + composition proof | `vuo-nodes/my.image.transform.crop.c` |
| my_MakeTileableImage | Operators/Lib/image/transform | Vuo node + composition proof | `vuo-nodes/my.image.transform.makeTileableImage.c` |
| my_MirrorRepeat | Operators/Lib/image/transform | Vuo node + composition proof | `vuo-nodes/my.image.transform.mirrorRepeat.c` |
| my_TransformImage | Operators/Lib/image/transform | Vuo node + composition proof | `vuo-nodes/my.image.transform.transformImage.c` |
| my_Blend | Operators/Lib/image/use | Vuo node + composition proof | `vuo-nodes/my.image.use.blend.c` |
| my_BlendImages | Operators/Lib/image/use | Vuo node + composition proof | `vuo-nodes/my.image.use.blendImages.c` |
| my_BlendWithMask | Operators/Lib/image/use | Vuo node + composition proof | `vuo-nodes/my.image.use.blendWithMask.c` |
| my_Combine3Images | Operators/Lib/image/use | Vuo node + composition proof | `vuo-nodes/my.image.use.combine3Images.c` |
| my_CombineMaterialChannels | Operators/Lib/image/use | Vuo node + composition proof | `vuo-nodes/my.image.use.combineMaterialChannels.c` |
| my_CombineMaterialChannels2 | Operators/Lib/image/use | Vuo node + composition proof | `vuo-nodes/my.image.use.combineMaterialChannels2.c` |
| my_DepthBufferAsGrayScale | Operators/Lib/image/use | Vuo node + composition proof | `vuo-nodes/my.image.use.depthBufferAsGrayScale.c` |
| my_FirstValidTexture | Operators/Lib/image/use | Vuo node + composition proof | `vuo-nodes/my.image.use.firstValidTexture.c` |
| my_Fxaa | Operators/Lib/image/use | Vuo node + composition proof | `vuo-nodes/my.image.use.fxaa.c` |
| my_KeepPreviousFrame | Operators/Lib/image/use | Vuo node + composition proof | `vuo-nodes/my.image.use.keepPreviousFrame.c` |
| my_NormalMap | Operators/Lib/image/use | Vuo node + composition proof | `vuo-nodes/my.image.use.normalMap.c` |
| my_PickTexture | Operators/Lib/image/use | Vuo node + composition proof | `vuo-nodes/my.image.use.pickTexture.c` |
| my_SwapTextures | Operators/Lib/image/use | Vuo node + composition proof | `vuo-nodes/my.image.use.swapTextures.c` |
| my_UseFallbackTexture | Operators/Lib/image/use | Vuo node + composition proof | `vuo-nodes/my.image.use.useFallbackTexture.c` |
| my_All | Operators/Lib/numbers/bool/combine | Vuo node + composition proof | `vuo-nodes/my.numbers.bool.combine.all.c` |
| my_And | Operators/Lib/numbers/bool/combine | Vuo node + composition proof | `vuo-nodes/my.numbers.bool.combine.and.c` |
| my_Any | Operators/Lib/numbers/bool/combine | Vuo node + composition proof | `vuo-nodes/my.numbers.bool.combine.any.c` |
| my_Or | Operators/Lib/numbers/bool/combine | Vuo node + composition proof | `vuo-nodes/my.numbers.bool.combine.or.c` |
| my_BoolToFloat | Operators/Lib/numbers/bool/convert | Vuo node + composition proof | `vuo-nodes/my.numbers.bool.convert.boolToFloat.c` |
| my_BoolToInt | Operators/Lib/numbers/bool/convert | Vuo node + composition proof | `vuo-nodes/my.numbers.bool.convert.boolToInt.c` |
| my_Not | Operators/Lib/numbers/bool/logic | Vuo node + composition proof | `vuo-nodes/my.numbers.bool.logic.not.c` |
| my_PickBool | Operators/Lib/numbers/bool/logic | Vuo node + composition proof | `vuo-nodes/my.numbers.bool.logic.pickBool.c` |
| my_Xor | Operators/Lib/numbers/bool/logic | Vuo node + composition proof | `vuo-nodes/my.numbers.bool.logic.xor.c` |
| my_BlendColors | Operators/Lib/numbers/color | Vuo node + composition proof | `vuo-nodes/my.numbers.color.blendColors.c` |
| my_BlendGradients | Operators/Lib/numbers/color | Vuo node + composition proof | `vuo-nodes/my.numbers.color.blendGradients.c` |
| my_BuildGradient | Operators/Lib/numbers/color | Vuo node + composition proof | `vuo-nodes/my.numbers.color.buildGradient.c` |
| my_CombineColorLists | Operators/Lib/numbers/color | Vuo node + composition proof | `vuo-nodes/my.numbers.color.combineColorLists.c` |
| my_DefineGradient | Operators/Lib/numbers/color | Vuo node + composition proof | `vuo-nodes/my.numbers.color.defineGradient.c` |
| my_GradientsToTexture | Operators/Lib/numbers/color | Vuo node + composition proof | `vuo-nodes/my.numbers.color.gradientsToTexture.c` |
| my_HSBToColor | Operators/Lib/numbers/color | Vuo node + composition proof | `vuo-nodes/my.numbers.color.hsbToColor.c` |
| my_HSLToColor | Operators/Lib/numbers/color | Vuo node + composition proof | `vuo-nodes/my.numbers.color.hslToColor.c` |
| my_KeepColors | Operators/Lib/numbers/color | Vuo node + composition proof | `vuo-nodes/my.numbers.color.keepColors.c` |
| my_OKLChToColor | Operators/Lib/numbers/color | Vuo node + composition proof | `vuo-nodes/my.numbers.color.oklchToColor.c` |
| my_PickColorFromImage | Operators/Lib/numbers/color | Vuo node + composition proof | `vuo-nodes/my.numbers.color.pickColorFromImage.c` |
| my_PickColorFromList | Operators/Lib/numbers/color | Vuo node + composition proof | `vuo-nodes/my.numbers.color.pickColorFromList.c` |
| my_PickGradient | Operators/Lib/numbers/color | Vuo node + composition proof | `vuo-nodes/my.numbers.color.pickGradient.c` |
| my_SampleGradient | Operators/Lib/numbers/color | Vuo node + composition proof | `vuo-nodes/my.numbers.color.sampleGradient.c` |
| my_SelectBoolFromFloatDict | Operators/Lib/numbers/data/utils | Vuo node + composition proof | `vuo-nodes/my.numbers.data.utils.selectBoolFromFloatDict.c` |
| my_SelectFloatFromDict | Operators/Lib/numbers/data/utils | Vuo node + composition proof | `vuo-nodes/my.numbers.data.utils.selectFloatFromDict.c` |
| my_SelectVec2FromDict | Operators/Lib/numbers/data/utils | Vuo node + composition proof | `vuo-nodes/my.numbers.data.utils.selectVec2FromDict.c` |
| my_SelectVec3FromDict | Operators/Lib/numbers/data/utils | Vuo node + composition proof | `vuo-nodes/my.numbers.data.utils.selectVec3FromDict.c` |
| TiXL Remap | Operators/Lib/numbers/float/adjust | Vuo node + source contract test | `vuo-nodes/myworld.tixl.remap.c` |
| my_Abs | Operators/Lib/numbers/float/adjust | Vuo node + composition proof | `vuo-nodes/my.numbers.float.adjust.abs.c` |
| my_Ceil | Operators/Lib/numbers/float/adjust | Vuo node + composition proof | `vuo-nodes/my.numbers.float.adjust.ceil.c` |
| my_Clamp | Operators/Lib/numbers/float/adjust | Vuo node + composition proof | `vuo-nodes/my.numbers.float.adjust.clamp.c` |
| my_Floor | Operators/Lib/numbers/float/adjust | Vuo node + composition proof | `vuo-nodes/my.numbers.float.adjust.floor.c` |
| my_InvertFloat | Operators/Lib/numbers/float/adjust | Vuo node + composition proof | `vuo-nodes/my.numbers.float.adjust.invertFloat.c` |
| my_Remap | Operators/Lib/numbers/float/adjust | Vuo node + composition proof | `vuo-nodes/my.numbers.float.adjust.remap.c` |
| my_Round | Operators/Lib/numbers/float/adjust | Vuo node + composition proof | `vuo-nodes/my.numbers.float.adjust.round.c` |
| my_Sigmoid | Operators/Lib/numbers/float/adjust | Vuo node + composition proof | `vuo-nodes/my.numbers.float.adjust.sigmoid.c` |
| my_Add | Operators/Lib/numbers/float/basic | Vuo node + composition proof | `vuo-nodes/my.numbers.float.basic.add.c` |
| my_Div | Operators/Lib/numbers/float/basic | Vuo node + composition proof | `vuo-nodes/my.numbers.float.basic.div.c` |
| my_Log | Operators/Lib/numbers/float/basic | Vuo node + composition proof | `vuo-nodes/my.numbers.float.basic.log.c` |
| my_Modulo | Operators/Lib/numbers/float/basic | Vuo node + composition proof | `vuo-nodes/my.numbers.float.basic.modulo.c` |
| my_Multiply | Operators/Lib/numbers/float/basic | Vuo node + composition proof | `vuo-nodes/my.numbers.float.basic.multiply.c` |
| my_Pow | Operators/Lib/numbers/float/basic | Vuo node + composition proof | `vuo-nodes/my.numbers.float.basic.pow.c` |
| my_Sqrt | Operators/Lib/numbers/float/basic | Vuo node + composition proof | `vuo-nodes/my.numbers.float.basic.sqrt.c` |
| my_Sub | Operators/Lib/numbers/float/basic | Vuo node + composition proof | `vuo-nodes/my.numbers.float.basic.sub.c` |
| my_Sum | Operators/Lib/numbers/float/basic | Vuo node + composition proof | `vuo-nodes/my.numbers.float.basic.sum.c` |
| my_Compare | Operators/Lib/numbers/float/logic | Vuo node + composition proof | `vuo-nodes/my.numbers.float.logic.compare.c` |
| my_IsGreater | Operators/Lib/numbers/float/logic | Vuo node + composition proof | `vuo-nodes/my.numbers.float.logic.isGreater.c` |
| my_PickFloat | Operators/Lib/numbers/float/logic | Vuo node + composition proof | `vuo-nodes/my.numbers.float.logic.pickFloat.c` |
| my_TryParse | Operators/Lib/numbers/float/logic | Vuo node + composition proof | `vuo-nodes/my.numbers.float.logic.tryParse.c` |
| my_ValueToRate | Operators/Lib/numbers/float/logic | Vuo node + composition proof | `vuo-nodes/my.numbers.float.logic.valueToRate.c` |
| TiXL Lerp | Operators/Lib/numbers/float/process | Vuo node + source contract test | `vuo-nodes/myworld.tixl.lerp.c` |
| my_BlendValues | Operators/Lib/numbers/float/process | Vuo node + composition proof | `vuo-nodes/my.numbers.float.process.blendValues.c` |
| my_Lerp | Operators/Lib/numbers/float/process | Vuo node + composition proof | `vuo-nodes/my.numbers.float.process.lerp.c` |
| my_RemapValues | Operators/Lib/numbers/float/process | Vuo node + composition proof | `vuo-nodes/my.numbers.float.process.remapValues.c` |
| my_SmoothStep | Operators/Lib/numbers/float/process | Vuo node + composition proof | `vuo-nodes/my.numbers.float.process.smoothStep.c` |
| my_Atan2 | Operators/Lib/numbers/float/trigonometry | Vuo node + composition proof | `vuo-nodes/my.numbers.float.trigonometry.atan2.c` |
| my_Cos | Operators/Lib/numbers/float/trigonometry | Vuo node + composition proof | `vuo-nodes/my.numbers.float.trigonometry.cos.c` |
| my_Sin | Operators/Lib/numbers/float/trigonometry | Vuo node + composition proof | `vuo-nodes/my.numbers.float.trigonometry.sin.c` |
| my_ColorsToList | Operators/Lib/numbers/floats/basic | Vuo node + composition proof | `vuo-nodes/my.numbers.floats.basic.colorsToList.c` |
| my_FloatListLength | Operators/Lib/numbers/floats/basic | Vuo node + composition proof | `vuo-nodes/my.numbers.floats.basic.floatListLength.c` |
| my_FloatsToList | Operators/Lib/numbers/floats/basic | Vuo node + composition proof | `vuo-nodes/my.numbers.floats.basic.floatsToList.c` |
| my_SetFloatListValue | Operators/Lib/numbers/floats/basic | Vuo node + composition proof | `vuo-nodes/my.numbers.floats.basic.setFloatListValue.c` |
| my_FloatListToIntList | Operators/Lib/numbers/floats/conversion | Vuo node + composition proof | `vuo-nodes/my.numbers.floats.conversion.floatListToIntList.c` |
| my_IntListToFloatList | Operators/Lib/numbers/floats/conversion | Vuo node + composition proof | `vuo-nodes/my.numbers.floats.conversion.intListToFloatList.c` |
| my_PickFloatFromList | Operators/Lib/numbers/floats/logic | Vuo node + composition proof | `vuo-nodes/my.numbers.floats.logic.pickFloatFromList.c` |
| my_PickFloatList | Operators/Lib/numbers/floats/logic | Vuo node + composition proof | `vuo-nodes/my.numbers.floats.logic.pickFloatList.c` |
| my_AnalyzeFloatList | Operators/Lib/numbers/floats/process | Vuo node + composition proof | `vuo-nodes/my.numbers.floats.process.analyzeFloatList.c` |
| my_ColorListToInts | Operators/Lib/numbers/floats/process | Vuo node + composition proof | `vuo-nodes/my.numbers.floats.process.colorListToInts.c` |
| my_CombineFloatLists | Operators/Lib/numbers/floats/process | Vuo node + composition proof | `vuo-nodes/my.numbers.floats.process.combineFloatLists.c` |
| my_CompareFloatLists | Operators/Lib/numbers/floats/process | Vuo node + composition proof | `vuo-nodes/my.numbers.floats.process.compareFloatLists.c` |
| my_RemapFloatList | Operators/Lib/numbers/floats/process | Vuo node + composition proof | `vuo-nodes/my.numbers.floats.process.remapFloatList.c` |
| my_SumRange | Operators/Lib/numbers/floats/process | Vuo node + composition proof | `vuo-nodes/my.numbers.floats.process.sumRange.c` |
| my_AddInts | Operators/Lib/numbers/int/basic | Vuo node + composition proof | `vuo-nodes/my.numbers.int.basic.addInts.c` |
| my_IntAdd | Operators/Lib/numbers/int/basic | Vuo node + composition proof | `vuo-nodes/my.numbers.int.basic.intAdd.c` |
| my_IntDiv | Operators/Lib/numbers/int/basic | Vuo node + composition proof | `vuo-nodes/my.numbers.int.basic.intDiv.c` |
| my_IntToFloat | Operators/Lib/numbers/int/basic | Vuo node + composition proof | `vuo-nodes/my.numbers.int.basic.intToFloat.c` |
| my_ModInt | Operators/Lib/numbers/int/basic | Vuo node + composition proof | `vuo-nodes/my.numbers.int.basic.modInt.c` |
| my_MultiplyInt | Operators/Lib/numbers/int/basic | Vuo node + composition proof | `vuo-nodes/my.numbers.int.basic.multiplyInt.c` |
| my_MultiplyInts | Operators/Lib/numbers/int/basic | Vuo node + composition proof | `vuo-nodes/my.numbers.int.basic.multiplyInts.c` |
| my_SubInts | Operators/Lib/numbers/int/basic | Vuo node + composition proof | `vuo-nodes/my.numbers.int.basic.subInts.c` |
| my_SumInts | Operators/Lib/numbers/int/basic | Vuo node + composition proof | `vuo-nodes/my.numbers.int.basic.sumInts.c` |
| my_CompareInt | Operators/Lib/numbers/int/logic | Vuo node + composition proof | `vuo-nodes/my.numbers.int.logic.compareInt.c` |
| my_IsIntEven | Operators/Lib/numbers/int/logic | Vuo node + composition proof | `vuo-nodes/my.numbers.int.logic.isIntEven.c` |
| my_PickInt | Operators/Lib/numbers/int/logic | Vuo node + composition proof | `vuo-nodes/my.numbers.int.logic.pickInt.c` |
| my_ClampInt | Operators/Lib/numbers/int/process | Vuo node + composition proof | `vuo-nodes/my.numbers.int.process.clampInt.c` |
| my_FloatToInt | Operators/Lib/numbers/int/process | Vuo node + composition proof | `vuo-nodes/my.numbers.int.process.floatToInt.c` |
| my_GetAPrime | Operators/Lib/numbers/int/process | Vuo node + composition proof | `vuo-nodes/my.numbers.int.process.getAPrime.c` |
| my_MaxInt | Operators/Lib/numbers/int/process | Vuo node + composition proof | `vuo-nodes/my.numbers.int.process.maxInt.c` |
| my_MinInt | Operators/Lib/numbers/int/process | Vuo node + composition proof | `vuo-nodes/my.numbers.int.process.minInt.c` |
| my_AddInt2 | Operators/Lib/numbers/int2/basic | Vuo node + composition proof | `vuo-nodes/my.numbers.int2.basic.addInt2.c` |
| my_Int2Components | Operators/Lib/numbers/int2/process | Vuo node + composition proof | `vuo-nodes/my.numbers.int2.process.int2Components.c` |
| my_MakeResolution | Operators/Lib/numbers/int2/process | Vuo node + composition proof | `vuo-nodes/my.numbers.int2.process.makeResolution.c` |
| my_MaxInt2 | Operators/Lib/numbers/int2/process | Vuo node + composition proof | `vuo-nodes/my.numbers.int2.process.maxInt2.c` |
| my_ScaleResolution | Operators/Lib/numbers/int2/process | Vuo node + composition proof | `vuo-nodes/my.numbers.int2.process.scaleResolution.c` |
| my_ScaleSize | Operators/Lib/numbers/int2/process | Vuo node + composition proof | `vuo-nodes/my.numbers.int2.process.scaleSize.c` |
| my_IntListLength | Operators/Lib/numbers/ints | Vuo node + composition proof | `vuo-nodes/my.numbers.ints.intListLength.c` |
| my_IntsToList | Operators/Lib/numbers/ints | Vuo node + composition proof | `vuo-nodes/my.numbers.ints.intsToList.c` |
| my_MergeIntLists | Operators/Lib/numbers/ints | Vuo node + composition proof | `vuo-nodes/my.numbers.ints.mergeIntLists.c` |
| my_PickIntFromList | Operators/Lib/numbers/ints | Vuo node + composition proof | `vuo-nodes/my.numbers.ints.pickIntFromList.c` |
| my_SetIntListValue | Operators/Lib/numbers/ints | Vuo node + composition proof | `vuo-nodes/my.numbers.ints.setIntListValue.c` |
| my_ClearRenderTarget | Operators/Lib/render/_dx11/api | Vuo node + composition proof | `vuo-nodes/my.render.dx11.api.clearRenderTarget.c` |
| my_SetMaterial | Operators/Lib/render/shading | Vuo node + composition proof | `vuo-nodes/my.render.shading.setMaterial.c` |
| my_StringRepeat | Operators/Lib/string/combine | Vuo node + composition proof | `vuo-nodes/my.string.combine.stringRepeat.c` |
| my_FloatToString | Operators/Lib/string/convert | Vuo node + composition proof | `vuo-nodes/my.string.convert.floatToString.c` |
| my_IntToString | Operators/Lib/string/convert | Vuo node + composition proof | `vuo-nodes/my.string.convert.intToString.c` |
| my_JoinStringList | Operators/Lib/string/list | Vuo node + composition proof | `vuo-nodes/my.string.list.joinStringList.c` |
| my_SplitString | Operators/Lib/string/list | Vuo node + composition proof | `vuo-nodes/my.string.list.splitString.c` |
| my_StringLength | Operators/Lib/string/list | Vuo node + composition proof | `vuo-nodes/my.string.list.stringLength.c` |
| my_IndexOf | Operators/Lib/string/search | Vuo node + composition proof | `vuo-nodes/my.string.search.indexOf.c` |
| my_SearchAndReplace | Operators/Lib/string/search | Vuo node + composition proof | `vuo-nodes/my.string.search.searchAndReplace.c` |
| my_SubString | Operators/Lib/string/search | Vuo node + composition proof | `vuo-nodes/my.string.search.subString.c` |
| my_ChangeCase | Operators/Lib/string/transform | Vuo node + composition proof | `vuo-nodes/my.string.transform.changeCase.c` |
| my_Batch10IntsProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.numbers.batch.batch10IntsProof.c` |
| my_Batch11BoolAggregateProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.numbers.batch.batch11BoolAggregateProof.c` |
| my_Batch12FloatAggregateProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.numbers.batch.batch12FloatAggregateProof.c` |
| my_Batch13DictSelectorsProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.numbers.batch.batch13DictSelectorsProof.c` |
| my_Batch14FloatListsProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.numbers.batch.batch14FloatListsProof.c` |
| my_Batch15FloatListConversionProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.numbers.batch.batch15FloatListConversionProof.c` |
| my_Batch16FloatListProcessProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.numbers.batch.batch16FloatListProcessProof.c` |
| my_Batch17FloatListTransformProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.numbers.batch.batch17FloatListTransformProof.c` |
| my_Batch18ColorValuesProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.numbers.batch.batch18ColorValuesProof.c` |
| my_Batch19ColorListToIntsProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.numbers.batch.batch19ColorListToIntsProof.c` |
| my_Batch1GradeANumbersProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.numbers.batch.gradeANumbersProof.c` |
| my_Batch20ColorMixPickProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.numbers.batch.batch20ColorMixPickProof.c` |
| my_Batch21OklchCombineColorListsProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.numbers.batch.batch21OklchCombineColorListsProof.c` |
| my_Batch22GradientCoreProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.numbers.batch.batch22GradientCoreProof.c` |
| my_Batch23GradientPickBlendProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.numbers.batch.batch23GradientPickBlendProof.c` |
| my_Batch24KeepColorsProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.numbers.batch.batch24KeepColorsProof.c` |
| my_Batch25PickColorFromImageProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.numbers.batch.batch25PickColorFromImageProof.c` |
| my_Batch27ImageUseRoutingProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.image.batch.batch27ImageUseRoutingProof.c` |
| my_Batch28ImageUseBlendCombineProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.image.batch.batch28ImageUseBlendCombineProof.c` |
| my_Batch29ImageUseMaterialChannelsProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.image.batch.batch29ImageUseMaterialChannelsProof.c` |
| my_Batch2GradeAStringsProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.string.batch.gradeAStringsProof.c` |
| my_Batch30ImageUsePostfxProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.image.batch.batch30ImageUsePostfxProof.c` |
| my_Batch31ImageTransformProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.image.batch.batch31ImageTransformProof.c` |
| my_Batch32BasicGenerateProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.image.batch.batch32BasicGenerateProof.c` |
| my_Batch33BasicGenerateShapesProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.image.batch.batch33BasicGenerateShapesProof.c` |
| my_Batch34NoiseGenerateProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.image.batch.batch34NoiseGenerateProof.c` |
| my_Batch35PatternGenerateProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.image.batch.batch35PatternGenerateProof.c` |
| my_Batch36FractalGenerateProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.image.batch.batch36FractalGenerateProof.c` |
| my_Batch37BlurProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.image.batch.batch37BlurProof.c` |
| my_Batch38GlitchProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.image.batch.batch38GlitchProof.c` |
| my_Batch39ColorProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.image.batch.batch39ColorProof.c` |
| my_Batch3ScalarValuesProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.numbers.batch.scalarValuesProof.c` |
| my_Batch40DistortProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.image.batch.batch40DistortProof.c` |
| my_Batch41StylizeProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.image.batch.batch41StylizeProof.c` |
| my_Batch42AnalyzeProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.image.batch.batch42AnalyzeProof.c` |
| my_Batch43LoadProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.image.batch.batch43LoadProof.c` |
| my_Batch44MiscProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.image.batch.batch44MiscProof.c` |
| my_Batch4RemapLerpProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.numbers.batch.remapLerpProof.c` |
| my_Batch5ScalarProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.numbers.batch.batch5ScalarProof.c` |
| my_Batch6FloatLogicProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.numbers.batch.batch6FloatLogicProof.c` |
| my_Batch7IntLogicProcessProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.numbers.batch.batch7IntLogicProcessProof.c` |
| my_Batch8MixedMathProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.numbers.batch.batch8MixedMathProof.c` |
| my_Batch9Int2Proof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.numbers.batch.batch9Int2Proof.c` |
| my_DrawMeshPbrProof | proof-adapter | Vuo node + composition proof | `vuo-nodes/my.mesh.draw.drawMeshPbrProof.c` |
| my_Batch25PickColorFromImageSource | unknown | Vuo node + composition proof | `vuo-nodes/my.numbers.batch.batch25PickColorFromImageSource.c` |
| my_Batch30PostfxSource | unknown | Vuo node + composition proof | `vuo-nodes/my.image.batch.batch30PostfxSource.c` |
| my_Batch31TransformSource | unknown | Vuo node + composition proof | `vuo-nodes/my.image.batch.batch31TransformSource.c` |

## Read

This board says what exists in Vuo today. It does not upgrade a body-layer adapter into native parity.
Use the grade rules before turning any unbuilt TiXL node into code.

Next batch should start from Grade A value/control nodes, then move outward only when the harness catches drift.
