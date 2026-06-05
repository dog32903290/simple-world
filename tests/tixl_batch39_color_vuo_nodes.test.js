#!/usr/bin/env node
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),test=require("node:test");
const repoRoot=path.resolve(__dirname,".."); const read=p=>fs.readFileSync(path.join(repoRoot,p),"utf8");
test("Batch 39 Vuo color nodes preserve names, paths, shader cues, outputs, and bounded limits",()=>{ const nodes=[
    ["vuo-nodes/my.image.color.adjustColors.c", "my_AdjustColors", "AdjustColors.cs", "AdjustColors.hlsl", "Output"],
    ["vuo-nodes/my.image.color.channelMixer.c", "my_ChannelMixer", "ChannelMixer.cs", "MixChannels.hlsl", "Output"],
    ["vuo-nodes/my.image.color.colorGrade.c", "my_ColorGrade", "ColorGrade.cs", "ColorGrade.hlsl", "Output"],
    ["vuo-nodes/my.image.color.colorGradeDepth.c", "my_ColorGradeDepth", "ColorGradeDepth.cs", "ColorGradeWithDepth.hlsl", "Output"],
    ["vuo-nodes/my.image.color.convertColors.c", "my_ConvertColors", "ConvertColors.cs", "img-fx-ConvertColors.hlsl", "Output"],
    ["vuo-nodes/my.image.color.convertFormat.c", "my_ConvertFormat", "ConvertFormat.cs", "ConvertFormat-cs.hlsl", "Output"],
    ["vuo-nodes/my.image.color.hse.c", "my_HSE", "HSE.cs", "HueShift.hlsl", "Output"],
    ["vuo-nodes/my.image.color.keyColor.c", "my_KeyColor", "KeyColor.cs", "ChromaKey.hlsl", "Output"],
    ["vuo-nodes/my.image.color.remapColor.c", "my_RemapColor", "RemapColor.cs", "ColorRemap.hlsl", "TextureOutput"],
    ["vuo-nodes/my.image.color.tint.c", "my_Tint", "Tint.cs", "Tint.hlsl", "Output"],
    ["vuo-nodes/my.image.color.toneMapping.c", "my_ToneMapping", "ToneMapping.cs", "ToneMap.hlsl", "Output"]
  ]; for (const [file,title,donor,shader,output] of nodes) { const source=read(file); assert.match(source,new RegExp(`"title"\\s*:\\s*"${title}"`)); assert.match(source,new RegExp(`Source: external/tixl/Operators/Lib/image/color/${donor}`)); assert.match(source,new RegExp(shader.replace(/[.*+?^${}()|[\]\\]/g,"\\$&"))); assert.match(source,new RegExp(`Texture2D ${output}`)); assert.match(source,/ColorForTextures #9F008A/); assert.match(source,/single-pass color proof/); } });
