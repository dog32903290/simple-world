#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
}

test("Batch 21 Vuo node sources preserve TiXL names, donor paths, defaults, and colors", () => {
  const nodes = [
    ["my.numbers.color.oklchToColor.c", "my_OKLChToColor", "color/OKLChToColor.cs", /Default: Hue=0\.0, Saturation=0\.0, Brightness=0\.50000006, Alpha=1\.0, UseGamma=false, IntensityBoost=1\.0/],
    ["my.numbers.color.combineColorLists.c", "my_CombineColorLists", "color/CombineColorLists.cs", /Default: InputLists=\[\]/],
  ];

  for (const [file, title, donor, defaults] of nodes) {
    const source = read(`vuo-nodes/${file}`);
    assert.match(source, new RegExp(`"title"\\s*:\\s*"${title}"`));
    assert.match(source, new RegExp(`Source: external/tixl/Operators/Lib/numbers/${donor.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}`));
    assert.match(source, /Primary output: .*ColorForValues #868C8D/);
    assert.match(source, defaults);
  }
});

test("Batch 21 Vuo node sources preserve OKLCh and color-list edge behavior", () => {
  const oklch = read("vuo-nodes/my.numbers.color.oklchToColor.c");
  const combine = read("vuo-nodes/my.numbers.color.combineColorLists.c");

  assert.match(oklch, /okLabToRgba/);
  assert.match(oklch, /pow\(.*1\.0 \/ 2\.2\)/);
  assert.match(oklch, /UseGamma is passed through by TiXL but currently not branched/);
  assert.match(oklch, /intensityBoost/);
  assert.match(combine, /Vuo bounded adapter: fixed 3 color-list inputs/);
  assert.match(combine, /VuoListAppendValue_VuoColor/);
});

test("TiXL .t3 defaults support Batch 21 Vuo contracts", () => {
  assert.match(read("external/tixl/Operators/Lib/numbers/color/OKLChToColor.t3"), /\/\*Brightness\*\/[\s\S]*"DefaultValue": 0\.50000006/);
  assert.match(read("external/tixl/Operators/Lib/numbers/color/OKLChToColor.t3"), /\/\*IntensityBoost\*\/[\s\S]*"DefaultValue": 1\.0/);
  assert.match(read("external/tixl/Operators/Lib/numbers/color/CombineColorLists.t3"), /\/\*InputLists\*\/[\s\S]*"Values": \[\]/);
});
