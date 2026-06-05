#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
}

test("Batch 32 Vuo node sources preserve TiXL names, donor paths, defaults, and texture color", () => {
  const nodes = [
    ["vuo-nodes/my.image.generate.basic.checkerBoard.c", "my_CheckerBoard", "CheckerBoard.cs", "Default: ColorA=(0.20212764,0.20212561,0.20212561,1), ColorB=(0.12056738,0.120566174,0.120566174,1), Stretch=(1,1), Scale=1, UseAspectRatio=true, Offset=(0,0), Resolution=(0,0), GenerateMips=false"],
    ["vuo-nodes/my.image.generate.basic.linearGradient.c", "my_LinearGradient", "LinearGradient.cs", "Default: Gradient=black-to-white, Width=1, SizeMode=0, Offset=0, OffsetMode=0, PingPong=false, Repeat=false, Rotate=90, Center=(0,0), GainAndBias=(0.5,0.5), BlendMode=0, Resolution=(0,0), GenerateMips=false, TextureFormat=R16G16B16A16_Float, Image=null"],
    ["vuo-nodes/my.image.generate.basic.radialGradient.c", "my_RadialGradient", "RadialGradient.cs", "Default: Gradient=white-to-black, Width=1, Stretch=(1,1), Offset=0, PingPong=false, Repeat=false, Center=(0,0), PolarOrientation=false, BiasAndGain=(0.5,0.5), Noise=0, BlendMode=0, Resolution=(0,0), TextureFormat=R16G16B16A16_Float, GenerateMipMaps=false, Image=null"],
    ["vuo-nodes/my.image.generate.basic.roundedRect.c", "my_RoundedRect", "RoundedRect.cs", "Default: Image=null, Color=(1,1,1,1), Background=(0,0,0,0), Position=(0,0), Stretch=(1,1), Scale=0.5, Rotate=0, Round=0.5, StrokeColor=(1,1,1,1), Stroke=0, Feather=0, FeatherBias=-0.001, Resolution=(0,0), GenerateMips=false"],
  ];

  for (const [file, title, donor, defaults] of nodes) {
    const source = read(file);
    assert.match(source, new RegExp(`"title"\\s*:\\s*"${title}"`), file);
    assert.match(source, new RegExp(`Source: external/tixl/Operators/Lib/image/generate/basic/${donor}`), file);
    assert.match(source, new RegExp(defaults.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")), file);
    assert.match(source, /Primary output: Texture2D TextureOutput \(ColorForTextures #9F008A\)/, file);
  }
});

test("Batch 32 Vuo node sources preserve shader cues and bounded gradient limits", () => {
  assert.match(read("vuo-nodes/my.image.generate.basic.checkerBoard.c"), /CheckerBoard\.hlsl/);
  assert.match(read("vuo-nodes/my.image.generate.basic.linearGradient.c"), /LinearGradient\.hlsl/);
  assert.match(read("vuo-nodes/my.image.generate.basic.linearGradient.c"), /Gradient datatype is represented as colorA\/colorB/);
  assert.match(read("vuo-nodes/my.image.generate.basic.radialGradient.c"), /RadialGradient\.hlsl/);
  assert.match(read("vuo-nodes/my.image.generate.basic.radialGradient.c"), /Gradient datatype is represented as colorA\/colorB/);
  assert.match(read("vuo-nodes/my.image.generate.basic.roundedRect.c"), /RoundedRect\.hlsl/);
});

test("TiXL .t3 defaults support Batch 32 basic generate contracts", () => {
  assert.match(read("external/tixl/Operators/Lib/image/generate/basic/CheckerBoard.t3"), /\/\*UseAspectRatio\*\/[\s\S]*"DefaultValue": true/);
  assert.match(read("external/tixl/Operators/Lib/image/generate/basic/LinearGradient.t3"), /\/\*Rotate\*\/[\s\S]*"DefaultValue": 90\.0/);
  assert.match(read("external/tixl/Operators/Lib/image/generate/basic/RadialGradient.t3"), /\/\*PolarOrientation\*\/[\s\S]*"DefaultValue": false/);
  assert.match(read("external/tixl/Operators/Lib/image/generate/basic/RoundedRect.t3"), /\/\*FeatherBias\*\/[\s\S]*"DefaultValue": -0\.001/);
});
