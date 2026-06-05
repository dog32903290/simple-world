#!/usr/bin/env node
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),test=require("node:test");
const repoRoot=path.resolve(__dirname,".."); const read=p=>fs.readFileSync(path.join(repoRoot,p),"utf8");
test("Batch 44 Vuo nodes preserve TiXL naming, source, output evidence, and bounded limits",()=>{ const nodes=[
    ["vuo-nodes/my.image.generate.misc.jumpFloodFill.c","my_JumpFloodFill","JumpFloodFill.cs","img-generate-JumpFloodFill.hlsl","ImageOutput"],
    ["vuo-nodes/my.image.generate.misc.sketch.c","my_Sketch","Sketch.cs","Sketch.cs","ColorBuffer"],
    ["vuo-nodes/my.image.generate.misc.slidingHistory.c","my_SlidingHistory","SlidingHistory.cs","SlidingHistory.hlsl","Output"]
  ]; for (const [file,title,donor,shader,output] of nodes) { const s=read(file); assert.match(s,new RegExp(`"title"\\s*:\\s*"${title}"`)); assert.match(s,new RegExp(`Source: external/tixl/Operators/Lib/image/generate/misc/${donor}`)); assert.match(s,new RegExp(shader.replace(/[.*+?^${}()|[\]\\]/g,"\\$&"))); assert.match(s,new RegExp(output)); assert.match(s,/bounded Vuo body-layer adapter|bounded Vuo adapter/); } });
