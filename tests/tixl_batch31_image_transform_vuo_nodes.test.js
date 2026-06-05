#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
}

test("Batch 31 Vuo node sources preserve TiXL names, donor paths, defaults, and texture color", () => {
  const nodes = [
    ["vuo-nodes/my.image.transform.crop.c", "my_Crop", "Crop.cs", "Default: Texture2d=null, LeftRight=(0,0), TopBottom=(0,0), PaddingColor=(1,1,1,0)", "Output"],
    ["vuo-nodes/my.image.transform.makeTileableImage.c", "my_MakeTileableImage", "MakeTileableImage.cs", "Default: ImageA=null, EdgeFallOff=0.2, TilingMode=3, IsEnabled=true", "Selected"],
    ["vuo-nodes/my.image.transform.mirrorRepeat.c", "my_MirrorRepeat", "MirrorRepeat.cs", "Default: Image=null, RotateMirror=0, RotateImage=0, Width=1, Offset=0, OffsetEdge=0, Offsetimage=(0,0), ShadeAmount=0, ShadeColor=(0.000001,0.000001,0.000001,1), Resolution=(-1,-1)", "TextureOutput"],
    ["vuo-nodes/my.image.transform.transformImage.c", "my_TransformImage", "TransformImage.cs", "Default: Image=null, Offset=(0,0), Stretch=(1,1), Scale=1, Rotation=0, Resolution=(0,0), ResolutionFactor=(1,1), GenerateMips=false, Filter=MinMagMipLinear, WrapMode=2", "TextureOutput"],
  ];

  for (const [file, title, donor, defaults, primaryOutput] of nodes) {
    const source = read(file);
    assert.match(source, new RegExp(`"title"\\s*:\\s*"${title}"`), file);
    assert.match(source, new RegExp(`Source: external/tixl/Operators/Lib/image/transform/${donor}`), file);
    assert.match(source, new RegExp(defaults.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")), file);
    assert.match(source, new RegExp(`Primary output: Texture2D ${primaryOutput} \\(ColorForTextures #9F008A\\)`), file);
  }
});

test("Batch 31 Vuo node sources preserve shader/graph cues and bounded adapter limits", () => {
  const crop = read("vuo-nodes/my.image.transform.crop.c");
  assert.match(crop, /CropImage-cs\.hlsl/);
  assert.match(crop, /compute shader/);
  assert.match(crop, /leftRight/);

  const tileable = read("vuo-nodes/my.image.transform.makeTileableImage.c");
  assert.match(tileable, /TransformImage/);
  assert.match(tileable, /BlendWithMask/);
  assert.match(tileable, /tilingMode/);

  const mirror = read("vuo-nodes/my.image.transform.mirrorRepeat.c");
  assert.match(mirror, /MirrorRepeat\.hlsl/);
  assert.match(mirror, /rotateMirror/);
  assert.match(mirror, /shadeAmount/);

  const transform = read("vuo-nodes/my.image.transform.transformImage.c");
  assert.match(transform, /TransformImage\.hlsl/);
  assert.match(transform, /wrapMode/);
  assert.match(transform, /filter\/mipmap/);
});

test("TiXL .t3 defaults support Batch 31 image transform contracts", () => {
  assert.match(read("external/tixl/Operators/Lib/image/transform/Crop.t3"), /\/\*PaddingColor\*\/[\s\S]*"W": 0\.0/);
  assert.match(read("external/tixl/Operators/Lib/image/transform/MakeTileableImage.t3"), /\/\*TilingMode\*\/[\s\S]*"DefaultValue": 3/);
  assert.match(read("external/tixl/Operators/Lib/image/transform/MirrorRepeat.t3"), /\/\*Width\*\/[\s\S]*"DefaultValue": 1\.0/);
  assert.match(read("external/tixl/Operators/Lib/image/transform/TransformImage.t3"), /\/\*WrapMode\*\/[\s\S]*"DefaultValue": 2/);
});
