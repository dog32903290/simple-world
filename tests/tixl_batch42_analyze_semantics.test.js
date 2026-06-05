#!/usr/bin/env node
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),test=require("node:test");
const repoRoot=path.resolve(__dirname,".."); const read=p=>fs.readFileSync(path.join(repoRoot,p),"utf8");
test("Batch 42 Lib.image.analyze source namespace is audited",()=>{ for (const name of ["CompareImages", "DetectMotion", "GetImageBrightness", "ImageLevels", "OpticalFlow", "RemoveStaticBackground", "WaveForm"]) { assert.match(read(`external/tixl/Operators/Lib/image/analyze/${name}.cs`),new RegExp(`class ${name}|sealed class ${name}`)); assert.match(read(`external/tixl/Operators/Lib/image/analyze/${name}.t3`),/DefaultValue|Inputs|Children|Id/); } });
