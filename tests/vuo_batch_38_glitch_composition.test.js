#!/usr/bin/env node
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),test=require("node:test");
const repoRoot=path.resolve(__dirname,"..");
test("Batch 38 proof wires glitch filters to a colored checkerboard source and save path",()=>{ const s=fs.readFileSync(path.join(repoRoot,"vuo-compositions/generated/myworld-batch-38-glitch-proof.vuo"),"utf8"); for (const title of ["my_GlitchDisplace","my_RgbTV","my_SortPixelGlitch","my_SubdivisionStretch","my_Batch38GlitchProof"]) assert.match(s,new RegExp(title)); assert.match(s,/Source:textureOutput -> GlitchDisplace:image/); assert.match(s,/Source:textureOutput -> SortPixelGlitch:texture2d/); assert.match(s,/batch-38-glitch-vuo-save/); });
