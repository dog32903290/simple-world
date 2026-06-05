#!/usr/bin/env node
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),test=require("node:test");
const repoRoot=path.resolve(__dirname,".."); const read=p=>fs.readFileSync(path.join(repoRoot,p),"utf8");
test("Batch 42 Vuo nodes preserve TiXL naming, source, output evidence, and bounded limits",()=>{ const nodes=[
    ["vuo-nodes/my.image.analyze.compareImages.c","my_CompareImages","CompareImages.cs","CompareImages.cs","TextureOutput"],
    ["vuo-nodes/my.image.analyze.detectMotion.c","my_DetectMotion","DetectMotion.cs","DetectMotion.cs","TextureOutput"],
    ["vuo-nodes/my.image.analyze.getImageBrightness.c","my_GetImageBrightness","GetImageBrightness.cs","cs-GetImageBrightness.hlsl","BrightnessImage"],
    ["vuo-nodes/my.image.analyze.imageLevels.c","my_ImageLevels","ImageLevels.cs","ImageLevels.hlsl","Output"],
    ["vuo-nodes/my.image.analyze.opticalFlow.c","my_OpticalFlow","OpticalFlow.cs","OpticalFlowKanade.hlsl","TextureOutput"],
    ["vuo-nodes/my.image.analyze.removeStaticBackground.c","my_RemoveStaticBackground","RemoveStaticBackground.cs","remove-static-background-cs1-learning.hlsl","Output"],
    ["vuo-nodes/my.image.analyze.waveForm.c","my_WaveForm","WaveForm.cs","waveform-cs.hlsl","ImgOutput"]
  ]; for (const [file,title,donor,shader,output] of nodes) { const s=read(file); assert.match(s,new RegExp(`"title"\\s*:\\s*"${title}"`)); assert.match(s,new RegExp(`Source: external/tixl/Operators/Lib/image/analyze/${donor}`)); assert.match(s,new RegExp(shader.replace(/[.*+?^${}()|[\]\\]/g,"\\$&"))); assert.match(s,new RegExp(output)); assert.match(s,/bounded Vuo body-layer adapter|bounded Vuo adapter/); } });
