#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
}

test("Batch 22 Vuo node sources preserve TiXL names, donor paths, defaults, and colors", () => {
  const nodes = [
    ["my.numbers.color.buildGradient.c", "my_BuildGradient", "color/BuildGradient.cs", /Default: Colors=\[black, white\], Positions=\[\], Interpolation=3/],
    ["my.numbers.color.defineGradient.c", "my_DefineGradient", "color/DefineGradient.cs", /Default: Color1Pos=0\.0, Color2Pos=1\.0, Color3Pos=-1\.0, Color4Pos=-1\.0, Interpolation=0/],
    ["my.numbers.color.sampleGradient.c", "my_SampleGradient", "color/SampleGradient.cs", /Default: SamplePos=0\.0, OverrideInterpolation=false, Interpolation=0/],
  ];

  for (const [file, title, donor, defaults] of nodes) {
    const source = read(`vuo-nodes/${file}`);
    assert.match(source, new RegExp(`"title"\\s*:\\s*"${title}"`));
    assert.match(source, new RegExp(`Source: external/tixl/Operators/Lib/numbers/${donor.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}`));
    assert.match(source, /Primary output: .*ColorForValues #868C8D/);
    assert.match(source, defaults);
    assert.match(source, /Vuo bounded adapter: TiXL Gradient maps to color list \+ position list \+ interpolation enum/);
  }
});

test("Batch 22 Vuo node sources preserve gradient construction and sampling behavior", () => {
  const build = read("vuo-nodes/my.numbers.color.buildGradient.c");
  const define = read("vuo-nodes/my.numbers.color.defineGradient.c");
  const sample = read("vuo-nodes/my.numbers.color.sampleGradient.c");

  assert.match(build, /appendNormalizedPositions/);
  assert.match(build, /sortGradientSteps/);
  assert.match(define, /appendStepIfEnabled/);
  assert.match(define, /fallback to Color1/);
  assert.match(sample, /sampleGradientColor/);
  assert.match(sample, /smootherStep/);
  assert.match(sample, /overrideInterpolation/);
  assert.match(sample, /Spline mode is adapter-bounded/);
});

test("TiXL .t3 defaults support Batch 22 Vuo contracts", () => {
  assert.match(read("external/tixl/Operators/Lib/numbers/color/BuildGradient.t3"), /\/\*Interpolation\*\/[\s\S]*"DefaultValue": 3/);
  assert.match(read("external/tixl/Operators/Lib/numbers/color/BuildGradient.t3"), /\/\*Positions\*\/[\s\S]*"Values": \[\]/);
  assert.match(read("external/tixl/Operators/Lib/numbers/color/DefineGradient.t3"), /\/\*Color3Pos\*\/[\s\S]*"DefaultValue": -1\.0/);
  assert.match(read("external/tixl/Operators/Lib/numbers/color/SampleGradient.t3"), /\/\*OverrideInterpolation\*\/[\s\S]*"DefaultValue": false/);
});
