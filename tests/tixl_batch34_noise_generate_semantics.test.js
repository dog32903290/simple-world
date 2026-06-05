#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
}

test("TiXL noise namespace has the five expected image generator nodes", () => {
  for (const name of ["FractalNoise", "Grain", "ShardNoise", "TileableNoise", "WorleyNoise"]) {
    assert.match(read(`external/tixl/Operators/Lib/image/generate/noise/${name}.cs`), new RegExp(`sealed class ${name}`));
    assert.match(read(`external/tixl/Operators/Lib/image/generate/noise/${name}.cs`), /Slot<Texture2D> TextureOutput|Slot<Texture2D> TextureOutput = new \(\)/);
  }
});

test("FractalNoise and TileableNoise preserve TiXL fbm controls", () => {
  assert.match(read("external/tixl/Operators/Lib/image/generate/noise/FractalNoise.t3"), /\/\*Iterations\*\/[\s\S]*"DefaultValue": 2/);
  assert.match(read("external/tixl/Operators/Lib/image/generate/noise/FractalNoise.t3"), /\/\*WarpXY\*\/[\s\S]*"X": 0\.0/);
  assert.match(read("external/tixl/Operators/Lib/Assets/shaders/img/generate/FractalNoise.hlsl"), /noise_sum_abs|simplex_noise/);

  assert.match(read("external/tixl/Operators/Lib/image/generate/noise/TileableNoise.t3"), /\/\*Detail\*\/[\s\S]*"DefaultValue": 1/);
  assert.match(read("external/tixl/Operators/Lib/image/generate/noise/TileableNoise.t3"), /\/\*Octaves\*\/[\s\S]*"DefaultValue": 2/);
  assert.match(read("external/tixl/Operators/Lib/Assets/shaders/img/generate/PerlinNoise2d.hlsl"), /perlinTileable|fbmPerlinTileable/);
});

test("ShardNoise and WorleyNoise preserve TiXL method families", () => {
  assert.match(read("external/tixl/Operators/Lib/image/generate/noise/ShardNoise.cs"), /Cubism[\s\S]*Cubism_X_Octaves[\s\S]*Octaves/);
  assert.match(read("external/tixl/Operators/Lib/Assets/shaders/img/generate/ShardNoise.hlsl"), /Shard Noise|shard_noise/);

  assert.match(read("external/tixl/Operators/Lib/image/generate/noise/WorleyNoise.cs"), /Worley_F1[\s\S]*Manhattan_worley_F1[\s\S]*Chebyshev_worley_F1[\s\S]*Worley_F2_F1/);
  assert.match(read("external/tixl/Operators/Lib/Assets/shaders/img/generate/WorleyNoise.hlsl"), /f2 - f1|cellular|Voronoi|Worley/i);
});

test("Grain defaults keep image-null procedural grain behavior", () => {
  assert.match(read("external/tixl/Operators/Lib/image/generate/noise/Grain.t3"), /\/\*Image\*\/[\s\S]*"DefaultValue": null/);
  assert.match(read("external/tixl/Operators/Lib/image/generate/noise/Grain.t3"), /\/\*Amount\*\/[\s\S]*"DefaultValue": 0\.05/);
  assert.match(read("external/tixl/Operators/Lib/image/generate/noise/Grain.t3"), /\/\*Animate\*\/[\s\S]*"DefaultValue": 5\.0/);
});
