#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
}

test("Batch 30 Vuo node sources preserve TiXL names, donor paths, defaults, and texture color", () => {
  const nodes = [
    ["vuo-nodes/my.image.use.fxaa.c", "my_Fxaa", "Fxaa.cs", "Default: Image=null, Preset=0, KeepAlpha=false", "TextureOutput"],
    ["vuo-nodes/my.image.use.normalMap.c", "my_NormalMap", "NormalMap.cs", "Default: Impact=1, SampleRadius=2, Resolution=(0,0), Twist=180, Mode=0", "Output"],
    ["vuo-nodes/my.image.use.depthBufferAsGrayScale.c", "my_DepthBufferAsGrayScale", "DepthBufferAsGrayScale.cs", "Default: NearFarRange=(0.01,1000), OutputRange=(0,5), ClampOutput=false, Mode=0", "Output"],
  ];

  for (const [file, title, donor, defaults, primaryOutput] of nodes) {
    const source = read(file);
    assert.match(source, new RegExp(`"title"\\s*:\\s*"${title}"`), file);
    assert.match(source, new RegExp(`Source: external/tixl/Operators/Lib/image/use/${donor}`), file);
    assert.match(source, new RegExp(defaults.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")), file);
    assert.match(source, new RegExp(`Primary output: Texture2D ${primaryOutput} \\(ColorForTextures #9F008A\\)`), file);
  }
});

test("Batch 30 Vuo node sources preserve shader cues and bounded adapter limits", () => {
  const fxaa = read("vuo-nodes/my.image.use.fxaa.c");
  assert.match(fxaa, /FXAA\.hlsl/);
  assert.match(fxaa, /keepAlpha/);
  assert.match(fxaa, /bounded approximation/);

  const normalMap = read("vuo-nodes/my.image.use.normalMap.c");
  assert.match(normalMap, /NormalMap\.hlsl/);
  assert.match(normalMap, /sampleRadius/);
  assert.match(normalMap, /twist/);
  assert.match(normalMap, /mode/);

  const depth = read("vuo-nodes/my.image.use.depthBufferAsGrayScale.c");
  assert.match(depth, /depth-to-linear\.hlsl/);
  assert.match(depth, /nearFarRange/);
  assert.match(depth, /outputRange/);
  assert.match(depth, /negative depth checker/);
});

test("TiXL .t3 defaults support Batch 30 post-fx contracts", () => {
  assert.match(read("external/tixl/Operators/Lib/image/use/Fxaa.t3"), /\/\*Preset\*\/[\s\S]*"DefaultValue": 0/);
  assert.match(read("external/tixl/Operators/Lib/image/use/NormalMap.t3"), /\/\*SampleRadius\*\/[\s\S]*"DefaultValue": 2\.0/);
  assert.match(read("external/tixl/Operators/Lib/image/use/DepthBufferAsGrayScale.t3"), /\/\*OutputRange\*\/[\s\S]*"X": 0\.0[\s\S]*"Y": 5\.0/);
});
