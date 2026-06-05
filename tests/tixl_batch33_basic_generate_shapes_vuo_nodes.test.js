#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
}

test("Batch 33 Vuo node sources preserve TiXL names, donor paths, defaults, and texture color", () => {
  const nodes = [
    ["vuo-nodes/my.image.generate.basic.blob.c", "my_Blob", "Blob.cs", "Default: Image=null, Color=(1,1,1,1), Background=(1,1,1,0), BlendMode=0, Scale=0.5, Stretch=(1,1), Rotate=0, Feather=1, FeatherBias=0, Position=(0,0), GenerateMips=false, Resolution=(0,0), TextureFormat=R16G16B16A16_Float"],
    ["vuo-nodes/my.image.generate.basic.boxGradient.c", "my_BoxGradient", "BoxGradient.cs", "Default: Image=null, Rotation=0, Center=(0,0), Size=(0.25,0.25), UniformScale=1, CornersRadius=(0,0,0,0), Gradient=white-to-black, GradientWidth=1, Offset=0, PingPong=true, Repeat=false, GainAndBias=(0.5,0.5), BlendMode=0, Resolution=(0,0)"],
    ["vuo-nodes/my.image.generate.basic.nGon.c", "my_NGon", "NGon.cs", "Default: Image=null, Fill=(1,1,1,1), Background=(0,0,0,0), Sides=3, Radius=0.25, Curvature=0, Blades=0, Feather=0.05, Round=0, FeatherBias=0, Position=(0,0), Rotate=-90, Resolution=(0,0), BlendMode=0"],
    ["vuo-nodes/my.image.generate.basic.nGonGradient.c", "my_NGonGradient", "NGonGradient.cs", "Default: Position=(0,0), Sides=5, Radius=0.33, Curvature=0, Roundness=1, Blades=0, Rotate=180, Gradient=white-to-black, Width=0.14, Offset=0, PingPong=false, Repeat=false, BiasAndGain=(0.5,0.5), BlendMode=0, Resolution=(0,0), Image=null"],
  ];

  for (const [file, title, donor, defaults] of nodes) {
    const source = read(file);
    assert.match(source, new RegExp(`"title"\\s*:\\s*"${title}"`), file);
    assert.match(source, new RegExp(`Source: external/tixl/Operators/Lib/image/generate/basic/${donor}`), file);
    assert.match(source, new RegExp(defaults.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")), file);
    assert.match(source, /Primary output: Texture2D TextureOutput \(ColorForTextures #9F008A\)/, file);
  }
});

test("Batch 33 Vuo node sources preserve shader cues and bounded gradient limits", () => {
  assert.match(read("vuo-nodes/my.image.generate.basic.blob.c"), /Blob\.hlsl/);
  assert.match(read("vuo-nodes/my.image.generate.basic.boxGradient.c"), /BoxGradient\.hlsl/);
  assert.match(read("vuo-nodes/my.image.generate.basic.boxGradient.c"), /Gradient datatype is represented as colorA\/colorB/);
  assert.match(read("vuo-nodes/my.image.generate.basic.nGon.c"), /NGon\.hlsl/);
  assert.match(read("vuo-nodes/my.image.generate.basic.nGonGradient.c"), /NGonGradient\.hlsl/);
  assert.match(read("vuo-nodes/my.image.generate.basic.nGonGradient.c"), /Gradient datatype is represented as colorA\/colorB/);
});

test("TiXL .t3 defaults support Batch 33 basic generate shape contracts", () => {
  assert.match(read("external/tixl/Operators/Lib/image/generate/basic/Blob.t3"), /\/\*Feather\*\/[\s\S]*"DefaultValue": 1\.0/);
  assert.match(read("external/tixl/Operators/Lib/image/generate/basic/BoxGradient.t3"), /\/\*PingPong\*\/[\s\S]*"DefaultValue": true/);
  assert.match(read("external/tixl/Operators/Lib/image/generate/basic/NGon.t3"), /\/\*Rotate\*\/[\s\S]*"DefaultValue": -90\.0/);
  assert.match(read("external/tixl/Operators/Lib/image/generate/basic/NGonGradient.t3"), /\/\*Width\*\/[\s\S]*"DefaultValue": 0\.14/);
});
