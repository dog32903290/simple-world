#!/usr/bin/env node
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),test=require("node:test");
const repoRoot=path.resolve(__dirname,"..");
test("Batch 41 proof composition wires Lib.image.fx.stylize nodes to a visible save path",()=>{ const s=fs.readFileSync(path.join(repoRoot,"vuo-compositions/generated/myworld-batch-41-stylize-proof.vuo"),"utf8"); for (const title of ["my_AsciiRender", "my_ChromaticAbberation", "my_ColorPhysarum", "my_DetectEdges", "my_Dither", "my_FakeLight", "my_Glow", "my_HoneyCombTiles", "my_LightRaysFx", "my_MosiacTiling", "my_Pixelate", "my_ScreenCloseUp", "my_StarGlowStreaks", "my_Steps", "my_VoronoiCells","my_Batch41StylizeProof"]) assert.match(s,new RegExp(title)); assert.match(s,/batch-41-stylize-vuo-save/); });
