#!/usr/bin/env node
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),test=require("node:test");
const repoRoot=path.resolve(__dirname,"..");
test("Batch 44 proof composition wires Lib.image.generate.misc nodes to a visible save path",()=>{ const s=fs.readFileSync(path.join(repoRoot,"vuo-compositions/generated/myworld-batch-44-misc-proof.vuo"),"utf8"); for (const title of ["my_JumpFloodFill", "my_Sketch", "my_SlidingHistory","my_Batch44MiscProof"]) assert.match(s,new RegExp(title)); assert.match(s,/batch-44-misc-vuo-save/); });
