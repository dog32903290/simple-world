#!/usr/bin/env node
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),test=require("node:test");
const repoRoot=path.resolve(__dirname,".."); const read=p=>fs.readFileSync(path.join(repoRoot,p),"utf8");
test("Batch 43 Vuo nodes preserve TiXL naming, source, output evidence, and bounded limits",()=>{ const nodes=[
    ["vuo-nodes/my.image.generate.load.imageSequenceClip.c","my_ImageSequenceClip","ImageSequenceClip.cs","ImageSequenceClip.cs","OutputImage"],
    ["vuo-nodes/my.image.generate.load.loadImage.c","my_LoadImage","LoadImage.cs","LoadImage.cs","Texture"],
    ["vuo-nodes/my.image.generate.load.loadImageFromUrl.c","my_LoadImageFromUrl","LoadImageFromUrl.cs","LoadImageFromUrl.cs","Texture"],
    ["vuo-nodes/my.image.generate.load.loadSvgAsTexture2D.c","my_LoadSvgAsTexture2D","LoadSvgAsTexture2D.cs","LoadSvgAsTexture2D.cs","Texture"]
  ]; for (const [file,title,donor,shader,output] of nodes) { const s=read(file); assert.match(s,new RegExp(`"title"\\s*:\\s*"${title}"`)); assert.match(s,new RegExp(`Source: external/tixl/Operators/Lib/image/generate/load/${donor}`)); assert.match(s,new RegExp(shader.replace(/[.*+?^${}()|[\]\\]/g,"\\$&"))); assert.match(s,new RegExp(output)); assert.match(s,/bounded Vuo body-layer adapter|bounded Vuo adapter/); } });
