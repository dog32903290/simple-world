#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
}

test("Batch 20 Vuo node sources preserve TiXL names, donor paths, defaults, and colors", () => {
  const nodes = [
    ["my.numbers.color.blendColors.c", "my_BlendColors", "color/BlendColors.cs", /Default: ColorA=\(1\.0,1\.0,1\.0,1\.0\), ColorB=\(1\.0,1\.0,1\.0,1\.0\), Factor=1\.0, Mode=0/],
    ["my.numbers.color.pickColorFromList.c", "my_PickColorFromList", "color/PickColorFromList.cs", /Default: Input=\[\], Index=0/],
  ];

  for (const [file, title, donor, defaults] of nodes) {
    const source = read(`vuo-nodes/${file}`);
    assert.match(source, new RegExp(`"title"\\s*:\\s*"${title}"`));
    assert.match(source, new RegExp(`Source: external/tixl/Operators/Lib/numbers/${donor.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}`));
    assert.match(source, /Primary output: .*ColorForValues #868C8D/);
    assert.match(source, defaults);
  }
});

test("Batch 20 Vuo node sources preserve blend modes and picker previous-output behavior", () => {
  const blend = read("vuo-nodes/my.numbers.color.blendColors.c");
  const pick = read("vuo-nodes/my.numbers.color.pickColorFromList.c");

  assert.match(blend, /mode == 0/);
  assert.match(blend, /mode == 3/);
  assert.match(blend, /result\.a = colorA\.a \+ colorB\.a - colorA\.a \* colorB\.a/);
  assert.match(pick, /positiveMod/);
  assert.match(pick, /previousSelected/);
  assert.match(pick, /count == 0/);
});

test("TiXL .t3 defaults support Batch 20 Vuo contracts", () => {
  assert.match(read("external/tixl/Operators/Lib/numbers/color/BlendColors.t3"), /\/\*Factor\*\/[\s\S]*"DefaultValue": 1\.0/);
  assert.match(read("external/tixl/Operators/Lib/numbers/color/BlendColors.t3"), /\/\*Mode\*\/[\s\S]*"DefaultValue": 0/);
  assert.match(read("external/tixl/Operators/Lib/numbers/color/PickColorFromList.t3"), /\/\*Index\*\/[\s\S]*"DefaultValue": 0/);
});
