#!/usr/bin/env node
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),test=require("node:test");
const repoRoot=path.resolve(__dirname,".."); const read=p=>fs.readFileSync(path.join(repoRoot,p),"utf8");
test("TiXL blur namespace has five Texture2D filter nodes",()=>{ for (const name of ["Bloom", "Blur", "DirectionalBlur", "FastBlur", "Sharpen"]) { assert.match(read(`external/tixl/Operators/Lib/image/fx/blur/${name}.cs`),new RegExp(`sealed class ${name}`)); assert.match(read(`external/tixl/Operators/Lib/image/fx/blur/${name}.t3`),/DefaultValue/); } });
