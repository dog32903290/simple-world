#!/usr/bin/env node
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),test=require("node:test");
const repoRoot=path.resolve(__dirname,".."); const read=p=>fs.readFileSync(path.join(repoRoot,p),"utf8");
test("Batch 44 Lib.image.generate.misc source namespace is audited",()=>{ for (const name of ["JumpFloodFill", "Sketch", "SlidingHistory"]) { assert.match(read(`external/tixl/Operators/Lib/image/generate/misc/${name}.cs`),new RegExp(`class ${name}|sealed class ${name}`)); assert.match(read(`external/tixl/Operators/Lib/image/generate/misc/${name}.t3`),/DefaultValue|Inputs|Children|Id/); } });
