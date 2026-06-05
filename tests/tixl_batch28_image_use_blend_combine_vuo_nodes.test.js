#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
}

test("Batch 28 Vuo node sources preserve TiXL names, donor paths, defaults, and texture color", () => {
  const nodes = [
    ["vuo-nodes/my.image.use.blendImages.c", "my_BlendImages", "BlendImages.cs", "Default: BlendFraction=0, Input=null, Resolution=(0,0)", "OutputImage"],
    ["vuo-nodes/my.image.use.blendWithMask.c", "my_BlendWithMask", "BlendWithMask.cs", "Default: ImageA=null, ImageB=null, Mask=null, Resolution=(0,0)", "Output"],
    ["vuo-nodes/my.image.use.combine3Images.c", "my_Combine3Images", "Combine3Images.cs", "Default: SelectChannel_R=0, SelectChannel_G=6, SelectChannel_B=12, SelectAlphaChannel=4", "Output"],
  ];

  for (const [file, title, donor, defaults, primaryOutput] of nodes) {
    const source = read(file);
    assert.match(source, new RegExp(`"title"\\s*:\\s*"${title}"`), file);
    assert.match(source, new RegExp(`Source: external/tixl/Operators/Lib/image/use/${donor}`), file);
    assert.match(source, new RegExp(defaults.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")), file);
    assert.match(source, new RegExp(`Primary output: Texture2D ${primaryOutput} \\(ColorForTextures #9F008A\\)`), file);
  }
});

test("Batch 28 Vuo node sources preserve shader semantics and bounded adapter limits", () => {
  const blendImages = read("vuo-nodes/my.image.use.blendImages.c");
  assert.match(blendImages, /positiveModulo/);
  assert.match(blendImages, /blendFraction/);
  assert.match(blendImages, /Vuo bounded adapter: TiXL MultiInputSlot/);

  const blendWithMask = read("vuo-nodes/my.image.use.blendWithMask.c");
  assert.match(blendWithMask, /mask.r/);
  assert.match(blendWithMask, /mix\(a, b, maskValue\)/);
  assert.match(blendWithMask, /BlendWithMask\.hlsl/);

  const combine3 = read("vuo-nodes/my.image.use.combine3Images.c");
  assert.match(combine3, /selectedChannel/);
  assert.match(combine3, /0\.239/);
  assert.match(combine3, /selectAlphaChannel/);
  assert.match(combine3, /img-combine-3\.hlsl/);
});

test("TiXL .t3 defaults support Batch 28 Vuo blend/combine contract", () => {
  assert.match(read("external/tixl/Operators/Lib/image/use/BlendImages.t3"), /\/\*BlendFraction\*\/[\s\S]*"DefaultValue": 0\.0/);
  assert.match(read("external/tixl/Operators/Lib/image/use/BlendWithMask.t3"), /\/\*Mask\*\/[\s\S]*"DefaultValue": null/);
  assert.match(read("external/tixl/Operators/Lib/image/use/Combine3Images.t3"), /\/\*SelectChannel_G\*\/[\s\S]*"DefaultValue": 6/);
  assert.match(read("external/tixl/Operators/Lib/image/use/Combine3Images.t3"), /\/\*SelectAlphaChannel\*\/[\s\S]*"DefaultValue": 4/);
});
