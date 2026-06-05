#!/usr/bin/env node
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),test=require("node:test");
const repoRoot=path.resolve(__dirname,"..");
test("Batch 42 proof composition wires Lib.image.analyze nodes to a visible save path",()=>{ const s=fs.readFileSync(path.join(repoRoot,"vuo-compositions/generated/myworld-batch-42-analyze-proof.vuo"),"utf8"); for (const title of ["my_CompareImages", "my_DetectMotion", "my_GetImageBrightness", "my_ImageLevels", "my_OpticalFlow", "my_RemoveStaticBackground", "my_WaveForm","my_Batch42AnalyzeProof"]) assert.match(s,new RegExp(title)); assert.match(s,/batch-42-analyze-vuo-save/); });
