#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const repoRoot = path.resolve(__dirname, "..");
const read = (p) => fs.readFileSync(path.join(repoRoot, p), "utf8");

test("TiXL pattern namespace has the nine expected Texture2D generators", () => {
  for (const name of ["FraserGrid", "NumberPattern", "Raster", "Rings", "RyojiPattern1", "RyojiPattern2", "SinForm", "ValueRaster", "ZollnerPattern"]) {
    assert.match(read(`external/tixl/Operators/Lib/image/generate/pattern/${name}.cs`), new RegExp(`sealed class ${name}`));
    assert.match(read(`external/tixl/Operators/Lib/image/generate/pattern/${name}.cs`), /Slot<Texture2D> TextureOutput/);
    assert.match(read(`external/tixl/Operators/Lib/image/generate/pattern/${name}.t3`), /DefaultValue/);
  }
});

test("TiXL pattern shader evidence is present", () => {
  for (const shader of ["FraserGrid.hlsl", "NumberPattern.hlsl", "Raster.hlsl", "Rings.hlsl", "RyojiPattern1.hlsl", "RyojiPattern2.hlsl", "SinForm.hlsl", "ValueRaster.hlsl", "ZollnerGrid.hlsl"]) {
    const hits = [
      `external/tixl/Operators/Lib/Assets/shaders/img/fx/${shader}`,
      `external/tixl/Operators/Lib/Assets/shaders/img/generate/${shader}`,
    ].filter((p) => fs.existsSync(path.join(repoRoot, p)));
    assert.ok(hits.length > 0, shader);
  }
});
