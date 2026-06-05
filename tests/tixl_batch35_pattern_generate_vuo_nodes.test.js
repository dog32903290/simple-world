#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const repoRoot = path.resolve(__dirname, "..");
const read = (p) => fs.readFileSync(path.join(repoRoot, p), "utf8");

test("Batch 35 Vuo pattern node sources preserve TiXL names, paths, shader cues, and texture color", () => {
  const nodes = [
    ["vuo-nodes/my.image.generate.pattern.fraserGrid.c", "my_FraserGrid", "FraserGrid.cs", "FraserGrid.hlsl"],
    ["vuo-nodes/my.image.generate.pattern.numberPattern.c", "my_NumberPattern", "NumberPattern.cs", "NumberPattern.hlsl"],
    ["vuo-nodes/my.image.generate.pattern.raster.c", "my_Raster", "Raster.cs", "Raster.hlsl"],
    ["vuo-nodes/my.image.generate.pattern.rings.c", "my_Rings", "Rings.cs", "Rings.hlsl"],
    ["vuo-nodes/my.image.generate.pattern.ryojiPattern1.c", "my_RyojiPattern1", "RyojiPattern1.cs", "RyojiPattern1.hlsl"],
    ["vuo-nodes/my.image.generate.pattern.ryojiPattern2.c", "my_RyojiPattern2", "RyojiPattern2.cs", "RyojiPattern2.hlsl"],
    ["vuo-nodes/my.image.generate.pattern.sinForm.c", "my_SinForm", "SinForm.cs", "SinForm.hlsl"],
    ["vuo-nodes/my.image.generate.pattern.valueRaster.c", "my_ValueRaster", "ValueRaster.cs", "ValueRaster.hlsl"],
    ["vuo-nodes/my.image.generate.pattern.zollnerPattern.c", "my_ZollnerPattern", "ZollnerPattern.cs", "ZollnerGrid.hlsl"]
  ];
  for (const [file, title, donor, shader] of nodes) {
    const source = read(file);
    assert.match(source, new RegExp(`"title"\\s*:\\s*"${title}"`), file);
    assert.match(source, new RegExp(`Source: external/tixl/Operators/Lib/image/generate/pattern/${donor}`), file);
    assert.match(source, new RegExp(shader.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")), file);
    assert.match(source, /Category: Operators\/Lib\/image\/generate\/pattern/, file);
    assert.match(source, /Primary output: Texture2D TextureOutput \(ColorForTextures #9F008A\)/, file);
    assert.match(source, /Vuo body-layer limit: complex TiXL image-input modulation/, file);
  }
});

test("Batch 35 pattern nodes expose distinct pattern-kind shader routing", () => {
  for (const [file] of [
    ["vuo-nodes/my.image.generate.pattern.fraserGrid.c", "my_FraserGrid", "FraserGrid.cs", "FraserGrid.hlsl"],
    ["vuo-nodes/my.image.generate.pattern.numberPattern.c", "my_NumberPattern", "NumberPattern.cs", "NumberPattern.hlsl"],
    ["vuo-nodes/my.image.generate.pattern.raster.c", "my_Raster", "Raster.cs", "Raster.hlsl"],
    ["vuo-nodes/my.image.generate.pattern.rings.c", "my_Rings", "Rings.cs", "Rings.hlsl"],
    ["vuo-nodes/my.image.generate.pattern.ryojiPattern1.c", "my_RyojiPattern1", "RyojiPattern1.cs", "RyojiPattern1.hlsl"],
    ["vuo-nodes/my.image.generate.pattern.ryojiPattern2.c", "my_RyojiPattern2", "RyojiPattern2.cs", "RyojiPattern2.hlsl"],
    ["vuo-nodes/my.image.generate.pattern.sinForm.c", "my_SinForm", "SinForm.cs", "SinForm.hlsl"],
    ["vuo-nodes/my.image.generate.pattern.valueRaster.c", "my_ValueRaster", "ValueRaster.cs", "ValueRaster.hlsl"],
    ["vuo-nodes/my.image.generate.pattern.zollnerPattern.c", "my_ZollnerPattern", "ZollnerPattern.cs", "ZollnerGrid.hlsl"]
  ]) {
    assert.match(read(file), /patternKind/);
    assert.match(read(file), /stripe|ring|ryoji/);
  }
});
