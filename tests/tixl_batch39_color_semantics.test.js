#!/usr/bin/env node
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),test=require("node:test");
const repoRoot=path.resolve(__dirname,".."); const read=p=>fs.readFileSync(path.join(repoRoot,p),"utf8");
test("TiXL image color namespace has eleven Texture2D color nodes",()=>{ for (const name of ["AdjustColors", "ChannelMixer", "ColorGrade", "ColorGradeDepth", "ConvertColors", "ConvertFormat", "HSE", "KeyColor", "RemapColor", "Tint", "ToneMapping"]) { assert.match(read(`external/tixl/Operators/Lib/image/color/${name}.cs`),new RegExp(`sealed class ${name}`)); assert.match(read(`external/tixl/Operators/Lib/image/color/${name}.t3`),/DefaultValue/); } });
test("ConvertFormat and ColorGradeDepth are documented as bounded renderer adapters",()=>{ assert.match(read("vuo-nodes/my.image.color.convertFormat.c"),/DXGI formats/); assert.match(read("vuo-nodes/my.image.color.colorGradeDepth.c"),/depth gradients/); });
