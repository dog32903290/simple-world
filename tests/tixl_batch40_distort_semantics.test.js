#!/usr/bin/env node
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),test=require("node:test");
const repoRoot=path.resolve(__dirname,".."); const read=p=>fs.readFileSync(path.join(repoRoot,p),"utf8");
test("Batch 40 Lib.image.fx.distort source namespace is audited",()=>{ for (const name of ["BubbleZoom", "ChromaticDistortion", "Displace", "DistortAndShade", "EdgeRepeat", "FieldToImage", "KochKaleidoskope", "PolarCoordinates", "TimeDisplace"]) { assert.match(read(`external/tixl/Operators/Lib/image/fx/distort/${name}.cs`),new RegExp(`class ${name}|sealed class ${name}`)); assert.match(read(`external/tixl/Operators/Lib/image/fx/distort/${name}.t3`),/DefaultValue|Inputs|Children|Id/); } });
