#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
}

test("Batch 23 Vuo node sources preserve TiXL names, donor paths, defaults, and colors", () => {
  const nodes = [
    ["my.numbers.color.pickGradient.c", "my_PickGradient", "color/PickGradient.cs", /Default: Gradients=\[\], Index=0/],
    ["my.numbers.color.blendGradients.c", "my_BlendGradients", "color/BlendGradients.cs", /Default: BlendMode=3, MixFactor=0\.0/],
  ];

  for (const [file, title, donor, defaults] of nodes) {
    const source = read(`vuo-nodes/${file}`);
    assert.match(source, new RegExp(`"title"\\s*:\\s*"${title}"`));
    assert.match(source, new RegExp(`Source: external/tixl/Operators/Lib/numbers/${donor.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}`));
    assert.match(source, /Primary output: Gradient .*ColorForValues #868C8D/);
    assert.match(source, defaults);
    assert.match(source, /Vuo bounded adapter: TiXL Gradient maps to color list \+ position list \+ interpolation enum/);
  }
});

test("Batch 23 Vuo node sources preserve gradient picking and blending behavior", () => {
  const pick = read("vuo-nodes/my.numbers.color.pickGradient.c");
  const blend = read("vuo-nodes/my.numbers.color.blendGradients.c");

  assert.match(pick, /positiveModulo/);
  assert.match(pick, /previousSelected/);
  assert.match(pick, /inputCount/);
  assert.match(blend, /blendGradientColor/);
  assert.match(blend, /sampleGradientColor/);
  assert.match(blend, /mergeStep/);
  assert.match(blend, /result interpolation is always Linear/);
  assert.match(blend, /clamp01\(mixFactor\)/);
});

test("TiXL .t3 defaults support Batch 23 Vuo contracts", () => {
  assert.match(read("external/tixl/Operators/Lib/numbers/color/PickGradient.t3"), /\/\*Index\*\/[\s\S]*"DefaultValue": 0/);
  assert.match(read("external/tixl/Operators/Lib/numbers/color/BlendGradients.t3"), /\/\*BlendMode\*\/[\s\S]*"DefaultValue": 3/);
  assert.match(read("external/tixl/Operators/Lib/numbers/color/BlendGradients.t3"), /\/\*MixFactor\*\/[\s\S]*"DefaultValue": 0\.0/);
});
