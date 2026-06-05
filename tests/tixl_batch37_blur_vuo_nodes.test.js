#!/usr/bin/env node
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),test=require("node:test");
const repoRoot=path.resolve(__dirname,".."); const read=p=>fs.readFileSync(path.join(repoRoot,p),"utf8");
test("Batch 37 Vuo blur nodes preserve names, paths, shader cues, and bounded limits",()=>{ const nodes=[
    ["vuo-nodes/my.image.fx.blur.bloom.c", "my_Bloom", "Bloom.cs", "Bloom-BrightpassPS.hlsl"],
    ["vuo-nodes/my.image.fx.blur.blur.c", "my_Blur", "Blur.cs", "Blur.hlsl"],
    ["vuo-nodes/my.image.fx.blur.directionalBlur.c", "my_DirectionalBlur", "DirectionalBlur.cs", "DirectionalBlur.hlsl"],
    ["vuo-nodes/my.image.fx.blur.fastBlur.c", "my_FastBlur", "FastBlur.cs", "FastBlur-BlurPS.hlsl"],
    ["vuo-nodes/my.image.fx.blur.sharpen.c", "my_Sharpen", "Sharpen.cs", "Sharpen.hlsl"]
  ]; for (const [file,title,donor,shader] of nodes) { const source=read(file); assert.match(source,new RegExp(`"title"\\s*:\\s*"${title}"`)); assert.match(source,new RegExp(`Source: external/tixl/Operators/Lib/image/fx/blur/${donor}`)); assert.match(source,new RegExp(shader.replace(/[.*+?^${}()|[\]\\]/g,"\\$&"))); assert.match(source,/ColorForTextures #9F008A/); assert.match(source,/single-pass image filter/); } });
