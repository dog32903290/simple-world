#!/usr/bin/env node
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),test=require("node:test");
const repoRoot=path.resolve(__dirname,"..");
test("Batch 40 proof composition wires Lib.image.fx.distort nodes to a visible save path",()=>{ const s=fs.readFileSync(path.join(repoRoot,"vuo-compositions/generated/myworld-batch-40-distort-proof.vuo"),"utf8"); for (const title of ["my_BubbleZoom", "my_ChromaticDistortion", "my_Displace", "my_DistortAndShade", "my_EdgeRepeat", "my_FieldToImage", "my_KochKaleidoskope", "my_PolarCoordinates", "my_TimeDisplace","my_Batch40DistortProof"]) assert.match(s,new RegExp(title)); assert.match(s,/batch-40-distort-vuo-save/); });
