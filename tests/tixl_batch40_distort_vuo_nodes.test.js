#!/usr/bin/env node
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),test=require("node:test");
const repoRoot=path.resolve(__dirname,".."); const read=p=>fs.readFileSync(path.join(repoRoot,p),"utf8");
test("Batch 40 Vuo nodes preserve TiXL naming, source, output evidence, and bounded limits",()=>{ const nodes=[
    ["vuo-nodes/my.image.fx.distort.bubbleZoom.c","my_BubbleZoom","BubbleZoom.cs","BubbleZoom.hlsl","TextureOutput"],
    ["vuo-nodes/my.image.fx.distort.chromaticDistortion.c","my_ChromaticDistortion","ChromaticDistortion.cs","ChromaticDistortion.hlsl","Output"],
    ["vuo-nodes/my.image.fx.distort.displace.c","my_Displace","Displace.cs","Displace.hlsl","Output"],
    ["vuo-nodes/my.image.fx.distort.distortAndShade.c","my_DistortAndShade","DistortAndShade.cs","DistortAndShade.hlsl","Output"],
    ["vuo-nodes/my.image.fx.distort.edgeRepeat.c","my_EdgeRepeat","EdgeRepeat.cs","EdgeRepeat.hlsl","TextureOutput"],
    ["vuo-nodes/my.image.fx.distort.fieldToImage.c","my_FieldToImage","FieldToImage.cs","FieldToImageTemplate.hlsl","TextureOutput"],
    ["vuo-nodes/my.image.fx.distort.kochKaleidoskope.c","my_KochKaleidoskope","KochKaleidoskope.cs","KochKaleidoscope.hlsl","TextureOutput"],
    ["vuo-nodes/my.image.fx.distort.polarCoordinates.c","my_PolarCoordinates","PolarCoordinates.cs","PolarCoordinates.hlsl","TextureOutput"],
    ["vuo-nodes/my.image.fx.distort.timeDisplace.c","my_TimeDisplace","TimeDisplace.cs","TimeDisplace.hlsl","Output"]
  ]; for (const [file,title,donor,shader,output] of nodes) { const s=read(file); assert.match(s,new RegExp(`"title"\\s*:\\s*"${title}"`)); assert.match(s,new RegExp(`Source: external/tixl/Operators/Lib/image/fx/distort/${donor}`)); assert.match(s,new RegExp(shader.replace(/[.*+?^${}()|[\]\\]/g,"\\$&"))); assert.match(s,new RegExp(output)); assert.match(s,/bounded Vuo body-layer adapter|bounded Vuo adapter/); } });
