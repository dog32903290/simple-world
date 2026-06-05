#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
}

test("Batch 18 Vuo node sources preserve TiXL names, donor paths, defaults, and colors", () => {
  const nodes = [
    ["my.numbers.color.hsbToColor.c", "my_HSBToColor", "color/HSBToColor.cs", /Default: Hue=0\.0, Saturation=0\.0, Brightness=0\.50000006, Alpha=1\.0/],
    ["my.numbers.color.hslToColor.c", "my_HSLToColor", "color/HSLToColor.cs", /Default: Hue=0\.0, Saturation=0\.0, Lightness=0\.50000006, Alpha=1\.0/],
    ["my.numbers.floats.basic.colorsToList.c", "my_ColorsToList", "floats/basic/ColorsToList.cs", /Default: Colors=\(1\.0,1\.0,1\.0,1\.0\)/],
  ];

  for (const [file, title, donor, defaults] of nodes) {
    const source = read(`vuo-nodes/${file}`);
    assert.match(source, new RegExp(`"title"\\s*:\\s*"${title}"`));
    assert.match(source, new RegExp(`Source: external/tixl/Operators/Lib/numbers/${donor.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}`));
    assert.match(source, /Primary output: .*ColorForValues #868C8D/);
    assert.match(source, defaults);
  }
});

test("Batch 18 Vuo node sources preserve color conversion and multi-input edge behavior", () => {
  const hsb = read("vuo-nodes/my.numbers.color.hsbToColor.c");
  const hsl = read("vuo-nodes/my.numbers.color.hslToColor.c");
  const colorsToList = read("vuo-nodes/my.numbers.floats.basic.colorsToList.c");

  assert.match(hsb, /hue < 0\.0/);
  assert.match(hsb, /sector == 5/);
  assert.match(hsl, /fmod\(hue, 1\.0\)/);
  assert.match(hsl, /TiXL custom HSL formula/);
  assert.match(colorsToList, /Vuo bounded adapter: fixed 3 color inputs/);
  assert.match(colorsToList, /VuoListAppendValue_VuoColor/);
});

test("TiXL .t3 defaults support Batch 18 Vuo contracts", () => {
  assert.match(read("external/tixl/Operators/Lib/numbers/color/HSBToColor.t3"), /\/\*Brightness\*\/[\s\S]*"DefaultValue": 0\.50000006/);
  assert.match(read("external/tixl/Operators/Lib/numbers/color/HSLToColor.t3"), /\/\*Lightness\*\/[\s\S]*"DefaultValue": 0\.50000006/);
  assert.match(read("external/tixl/Operators/Lib/numbers/floats/basic/ColorsToList.t3"), /\/\*Colors\*\/[\s\S]*"W": 1\.0/);
});
