#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
}

test("Batch 34 Vuo node sources preserve TiXL titles, donor paths, and texture color", () => {
  const nodes = [
    ["vuo-nodes/my.image.generate.noise.fractalNoise.c", "my_FractalNoise", "FractalNoise.cs", "FractalNoise.hlsl"],
    ["vuo-nodes/my.image.generate.noise.grain.c", "my_Grain", "Grain.cs", "Grain.hlsl"],
    ["vuo-nodes/my.image.generate.noise.shardNoise.c", "my_ShardNoise", "ShardNoise.cs", "ShardNoise.hlsl"],
    ["vuo-nodes/my.image.generate.noise.tileableNoise.c", "my_TileableNoise", "TileableNoise.cs", "PerlinNoise2d.hlsl"],
    ["vuo-nodes/my.image.generate.noise.worleyNoise.c", "my_WorleyNoise", "WorleyNoise.cs", "WorleyNoise.hlsl"],
  ];

  for (const [file, title, donor, shader] of nodes) {
    const source = read(file);
    assert.match(source, new RegExp(`"title"\\s*:\\s*"${title}"`), file);
    assert.match(source, new RegExp(`Source: external/tixl/Operators/Lib/image/generate/noise/${donor}`), file);
    assert.match(source, new RegExp(shader.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")), file);
    assert.match(source, /Category: Operators\/Lib\/image\/generate\/noise/, file);
    assert.match(source, /Primary output: Texture2D TextureOutput \(ColorForTextures #9F008A\)/, file);
  }
});

test("Batch 34 Vuo node sources preserve defaults and bounded limits", () => {
  assert.match(read("vuo-nodes/my.image.generate.noise.fractalNoise.c"), /Default: ColorA=\(0,0,0,1\), ColorB=\(1,1,1,1\).*Iterations=2.*WarpXY=\(0,0\).*OutputFormat=R16G16B16A16_Float/s);
  assert.match(read("vuo-nodes/my.image.generate.noise.grain.c"), /Default: Image=null, Amount=0\.05, Color=0, Exponent=1, Brightness=0, Animate=5/);
  assert.match(read("vuo-nodes/my.image.generate.noise.shardNoise.c"), /Default: ColorA=\(0,0,0,1\), ColorB=\(1,1,1,1\).*Method=0/s);
  assert.match(read("vuo-nodes/my.image.generate.noise.tileableNoise.c"), /Default: ColorA=\(0,0,0,1\), ColorB=\(1,1,1,1\), Detail=1, Octaves=2, Gain=0\.5, Lacunarity=2/);
  assert.match(read("vuo-nodes/my.image.generate.noise.worleyNoise.c"), /Default: Texture=null, TextureBlend=1, ColorA=\(1,1,1,1\), ColorB=\(0,0,0,1\).*Method=0/s);

  assert.match(read("vuo-nodes/my.image.generate.noise.grain.c"), /Image blending is represented as standalone procedural grain/);
  assert.match(read("vuo-nodes/my.image.generate.noise.worleyNoise.c"), /source texture multiplication is omitted when Texture is null/);
  assert.match(read("vuo-nodes/my.image.generate.noise.tileableNoise.c"), /tileable fbm is preserved/);
});

test("Batch 34 Vuo shader bodies expose distinct noise family cues", () => {
  assert.match(read("vuo-nodes/my.image.generate.noise.fractalNoise.c"), /valueNoise[\s\S]*iterations/);
  assert.match(read("vuo-nodes/my.image.generate.noise.grain.c"), /floor\(uv\)[\s\S]*amount/);
  assert.match(read("vuo-nodes/my.image.generate.noise.shardNoise.c"), /shard\(/);
  assert.match(read("vuo-nodes/my.image.generate.noise.tileableNoise.c"), /tileHash[\s\S]*mod\(cell, period\)[\s\S]*tileValueNoise/);
  assert.match(read("vuo-nodes/my.image.generate.noise.worleyNoise.c"), /f2MinusF1[\s\S]*metricKind/);
});
