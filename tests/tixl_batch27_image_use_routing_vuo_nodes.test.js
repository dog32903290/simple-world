#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
}

test("Batch 27 Vuo node sources preserve TiXL names, donor paths, defaults, and texture color", () => {
  const nodes = [
    ["vuo-nodes/my.image.use.firstValidTexture.c", "my_FirstValidTexture", "FirstValidTexture.cs", "Default: Input=null", "Output"],
    ["vuo-nodes/my.image.use.pickTexture.c", "my_PickTexture", "PickTexture.cs", "Default: Index=0, Input=null", "Selected"],
    ["vuo-nodes/my.image.use.swapTextures.c", "my_SwapTextures", "SwapTextures.cs", "Default: TextureAInput=null, TextureBInput=null, EnableSwap=false", "TextureA"],
    ["vuo-nodes/my.image.use.useFallbackTexture.c", "my_UseFallbackTexture", "UseFallbackTexture.cs", "Default: TextureA=null, Fallback=null", "Output"],
  ];

  for (const [file, title, donor, defaults, primaryOutput] of nodes) {
    const source = read(file);
    assert.match(source, new RegExp(`"title"\\s*:\\s*"${title}"`), file);
    assert.match(source, new RegExp(`Source: external/tixl/Operators/Lib/image/use/${donor}`), file);
    assert.match(source, new RegExp(defaults.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")), file);
    assert.match(source, new RegExp(`Primary output: Texture2D ${primaryOutput} \\(ColorForTextures #9F008A\\)`), file);
  }
});

test("Batch 27 Vuo node sources preserve TiXL routing behavior", () => {
  const firstValid = read("vuo-nodes/my.image.use.firstValidTexture.c");
  assert.match(firstValid, /previousOutput/);
  assert.match(firstValid, /inputCount/);
  assert.match(firstValid, /input1 \? input1 :/);

  const pick = read("vuo-nodes/my.image.use.pickTexture.c");
  assert.match(pick, /positiveModulo/);
  assert.match(pick, /previousSelected/);
  assert.match(pick, /inputCount/);

  const swap = read("vuo-nodes/my.image.use.swapTextures.c");
  assert.match(swap, /enableSwap/);
  assert.match(swap, /textureBInput/);
  assert.match(swap, /textureAInput/);

  const fallback = read("vuo-nodes/my.image.use.useFallbackTexture.c");
  assert.match(fallback, /textureA \? textureA : fallback/);
  assert.match(fallback, /bounded adapter/);
});

test("TiXL .t3 defaults support Batch 27 Vuo routing contract", () => {
  assert.match(read("external/tixl/Operators/Lib/image/use/FirstValidTexture.t3"), /\/\*Input\*\/[\s\S]*"DefaultValue": null/);
  assert.match(read("external/tixl/Operators/Lib/image/use/PickTexture.t3"), /\/\*Index\*\/[\s\S]*"DefaultValue": 0/);
  assert.match(read("external/tixl/Operators/Lib/image/use/SwapTextures.t3"), /\/\*EnableSwap\*\/[\s\S]*"DefaultValue": false/);
  assert.match(read("external/tixl/Operators/Lib/image/use/UseFallbackTexture.t3"), /\/\*Fallback\*\/[\s\S]*"DefaultValue": null/);
});
