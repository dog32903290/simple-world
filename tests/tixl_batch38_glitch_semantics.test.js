#!/usr/bin/env node
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),test=require("node:test");
const repoRoot=path.resolve(__dirname,".."); const read=p=>fs.readFileSync(path.join(repoRoot,p),"utf8");
test("TiXL glitch namespace has four Texture2D image nodes",()=>{ for (const name of ["GlitchDisplace", "RgbTV", "SortPixelGlitch", "SubdivisionStretch"]) { assert.match(read(`external/tixl/Operators/Lib/image/fx/glitch/${name}.cs`),new RegExp(`sealed class ${name}`)); assert.match(read(`external/tixl/Operators/Lib/image/fx/glitch/${name}.t3`),/DefaultValue/); } });
test("SortPixelGlitch source evidence is compute-heavy and documented as bounded",()=>{ assert.match(read("external/tixl/Operators/Lib/image/fx/glitch/SortPixelGlitch.t3"),/ComputeShader/); assert.match(read("vuo-nodes/my.image.fx.glitch.sortPixelGlitch.c"),/compute sorting/); });
