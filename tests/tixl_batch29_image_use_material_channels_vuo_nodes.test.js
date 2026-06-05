#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
}

test("Batch 29 Vuo node sources preserve TiXL names, donor paths, defaults, and texture color", () => {
  const nodes = [
    ["vuo-nodes/my.image.use.combineMaterialChannels2.c", "my_CombineMaterialChannels2", "CombineMaterialChannels2.cs", "Default: SelectChannel_R=0, SelectChannel_G=6, SelectChannel_B=12, SelectAlphaChannel=4", "Output"],
    ["vuo-nodes/my.image.use.combineMaterialChannels.c", "my_CombineMaterialChannels", "CombineMaterialChannels.cs", "Default: GenerateMips=true, Resolution=(0,0), RemapRoughness=identity", "Output"],
  ];

  for (const [file, title, donor, defaults, primaryOutput] of nodes) {
    const source = read(file);
    assert.match(source, new RegExp(`"title"\\s*:\\s*"${title}"`), file);
    assert.match(source, new RegExp(`Source: external/tixl/Operators/Lib/image/use/${donor}`), file);
    assert.match(source, new RegExp(defaults.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")), file);
    assert.match(source, new RegExp(`Primary output: Texture2D ${primaryOutput} \\(ColorForTextures #9F008A\\)`), file);
  }
});

test("Batch 29 Vuo node sources preserve material channel shader laws and bounded limits", () => {
  const cmc2 = read("vuo-nodes/my.image.use.combineMaterialChannels2.c");
  assert.match(cmc2, /selectedChannel/);
  assert.match(cmc2, /img-combine-3\.hlsl/);

  const cmc = read("vuo-nodes/my.image.use.combineMaterialChannels.c");
  assert.match(cmc, /CombineMaterialChannels\.hlsl/);
  assert.match(cmc, /roughness\.r/);
  assert.match(cmc, /metallic\.g/);
  assert.match(cmc, /occlusion\.r/);
  assert.match(cmc, /identity remap/);
});

test("TiXL .t3 defaults support Batch 29 material channel contracts", () => {
  assert.match(read("external/tixl/Operators/Lib/image/use/CombineMaterialChannels2.t3"), /\/\*SelectChannel_G\*\/[\s\S]*"DefaultValue": 6/);
  assert.match(read("external/tixl/Operators/Lib/image/use/CombineMaterialChannels.t3"), /\/\*GenerateMips\*\/[\s\S]*"DefaultValue": true/);
  assert.match(read("external/tixl/Operators/Lib/image/use/CombineMaterialChannels.t3"), /\/\*RemapRoughness\*\/[\s\S]*"Time": 0\.0[\s\S]*"Value": 0\.0[\s\S]*"Time": 1\.0[\s\S]*"Value": 1\.0/);
});
