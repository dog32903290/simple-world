#!/usr/bin/env node
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),test=require("node:test");
const repoRoot=path.resolve(__dirname,"..");
test("Batch 37 proof wires blur filters to a checkerboard source and save path",()=>{ const s=fs.readFileSync(path.join(repoRoot,"vuo-compositions/generated/myworld-batch-37-blur-proof.vuo"),"utf8"); for (const title of ["my_Bloom","my_Blur","my_DirectionalBlur","my_FastBlur","my_Sharpen","my_Batch37BlurProof"]) assert.match(s,new RegExp(title)); assert.match(s,/Source:textureOutput -> Bloom:image/); assert.match(s,/batch-37-blur-vuo-save/); });
