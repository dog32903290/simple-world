#!/usr/bin/env node
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),test=require("node:test");
const repoRoot=path.resolve(__dirname,".."); const read=p=>fs.readFileSync(path.join(repoRoot,p),"utf8");
test("Batch 38 Vuo glitch nodes preserve names, paths, shader cues, outputs, and bounded limits",()=>{ const nodes=[
    ["vuo-nodes/my.image.fx.glitch.glitchDisplace.c", "my_GlitchDisplace", "GlitchDisplace.cs", "GlitchDisplace.hlsl", "Output2"],
    ["vuo-nodes/my.image.fx.glitch.rgbTv.c", "my_RgbTV", "RgbTV.cs", "RgbTV.hlsl", "TextureOutput"],
    ["vuo-nodes/my.image.fx.glitch.sortPixelGlitch.c", "my_SortPixelGlitch", "SortPixelGlitch.cs", "SortPixelsGlitch-cs.hlsl", "Output"],
    ["vuo-nodes/my.image.fx.glitch.subdivisionStretch.c", "my_SubdivisionStretch", "SubdivisionStretch.cs", "StretchSubdivide.hlsl", "TextureOutput"]
  ]; for (const [file,title,donor,shader,output] of nodes) { const source=read(file); assert.match(source,new RegExp(`"title"\\s*:\\s*"${title}"`)); assert.match(source,new RegExp(`Source: external/tixl/Operators/Lib/image/fx/glitch/${donor}`)); assert.match(source,new RegExp(shader.replace(/[.*+?^${}()|[\]\\]/g,"\\$&"))); assert.match(source,new RegExp(`Texture2D ${output}`)); assert.match(source,/ColorForTextures #9F008A/); assert.match(source,/single-pass visual proof/); } });
