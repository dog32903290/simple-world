#!/usr/bin/env node
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),test=require("node:test");
const repoRoot=path.resolve(__dirname,"..");
test("Batch 43 proof composition wires Lib.image.generate.load nodes to a visible save path",()=>{ const s=fs.readFileSync(path.join(repoRoot,"vuo-compositions/generated/myworld-batch-43-load-proof.vuo"),"utf8"); for (const title of ["my_ImageSequenceClip", "my_LoadImage", "my_LoadImageFromUrl", "my_LoadSvgAsTexture2D","my_Batch43LoadProof"]) assert.match(s,new RegExp(title)); assert.match(s,/batch-43-load-vuo-save/); });
