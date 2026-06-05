#!/usr/bin/env node
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),test=require("node:test");
const repoRoot=path.resolve(__dirname,".."); const read=p=>fs.readFileSync(path.join(repoRoot,p),"utf8");
test("Batch 41 Vuo nodes preserve TiXL naming, source, output evidence, and bounded limits",()=>{ const nodes=[
    ["vuo-nodes/my.image.fx.stylize.asciiRender.c","my_AsciiRender","AsciiRender.cs","AsciiRender.hlsl","Output"],
    ["vuo-nodes/my.image.fx.stylize.chromaticAbberation.c","my_ChromaticAbberation","ChromaticAbberation.cs","ChromaticAbberation.hlsl","TextureOutput"],
    ["vuo-nodes/my.image.fx.stylize.colorPhysarum.c","my_ColorPhysarum","ColorPhysarum.cs","color-physarum-cs.hlsl","ImgOutput"],
    ["vuo-nodes/my.image.fx.stylize.detectEdges.c","my_DetectEdges","DetectEdges.cs","DetectEdges.hlsl","TextureOutput"],
    ["vuo-nodes/my.image.fx.stylize.dither.c","my_Dither","Dither.cs","Dither.hlsl","TextureOutput"],
    ["vuo-nodes/my.image.fx.stylize.fakeLight.c","my_FakeLight","FakeLight.cs","FakeLight.hlsl","Output"],
    ["vuo-nodes/my.image.fx.stylize.glow.c","my_Glow","Glow.cs","Glow.cs","ImgOutput"],
    ["vuo-nodes/my.image.fx.stylize.honeyCombTiles.c","my_HoneyCombTiles","HoneyCombTiles.cs","HexGridDisplace.hlsl","TextureOutput"],
    ["vuo-nodes/my.image.fx.stylize.lightRaysFx.c","my_LightRaysFx","LightRaysFx.cs","LightRayFx.hlsl","Output"],
    ["vuo-nodes/my.image.fx.stylize.mosiacTiling.c","my_MosiacTiling","MosiacTiling.cs","MosiacTiling.hlsl","TextureOutput"],
    ["vuo-nodes/my.image.fx.stylize.pixelate.c","my_Pixelate","Pixelate.cs","Pixelate.hlsl","TextureOutput"],
    ["vuo-nodes/my.image.fx.stylize.screenCloseUp.c","my_ScreenCloseUp","ScreenCloseUp.cs","ScreenCloseUp.cs","Output"],
    ["vuo-nodes/my.image.fx.stylize.starGlowStreaks.c","my_StarGlowStreaks","StarGlowStreaks.cs","StarGlowStreaks.hlsl","TextureOutput"],
    ["vuo-nodes/my.image.fx.stylize.steps.c","my_Steps","Steps.cs","Steps.hlsl","TextureOutput"],
    ["vuo-nodes/my.image.fx.stylize.voronoiCells.c","my_VoronoiCells","VoronoiCells.cs","VoronoiCells.hlsl","TextureOutput"]
  ]; for (const [file,title,donor,shader,output] of nodes) { const s=read(file); assert.match(s,new RegExp(`"title"\\s*:\\s*"${title}"`)); assert.match(s,new RegExp(`Source: external/tixl/Operators/Lib/image/fx/stylize/${donor}`)); assert.match(s,new RegExp(shader.replace(/[.*+?^${}()|[\]\\]/g,"\\$&"))); assert.match(s,new RegExp(output)); assert.match(s,/bounded Vuo body-layer adapter|bounded Vuo adapter/); } });
