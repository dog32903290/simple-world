#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
}

test("Batch 13 Vuo node sources preserve TiXL names, donors, defaults, colors, and adapter boundary", () => {
  const nodes = [
    ["my.numbers.data.utils.selectFloatFromDict.c", "my_SelectFloatFromDict", "SelectFloatFromDict.cs", /Default: DictionaryInput=null, Select=""/],
    ["my.numbers.data.utils.selectBoolFromFloatDict.c", "my_SelectBoolFromFloatDict", "SelectBoolFromFloatDict.cs", /Default: DictionaryInput=null, Select=""/],
    ["my.numbers.data.utils.selectVec2FromDict.c", "my_SelectVec2FromDict", "SelectVec2FromDict.cs", /Default: DictionaryInput=null, SelectX=""/],
    ["my.numbers.data.utils.selectVec3FromDict.c", "my_SelectVec3FromDict", "SelectVec3FromDict.cs", /Default: DictionaryInput=null, SelectX=""/],
  ];

  for (const [file, title, donor, defaults] of nodes) {
    const source = read(`vuo-nodes/${file}`);
    assert.match(source, new RegExp(`"title"\\s*:\\s*"${title}"`));
    assert.match(source, new RegExp(`Source: external/tixl/Operators/Lib/numbers/data/utils/${donor}`));
    assert.match(source, /Body-layer adapter: VuoText key=value dictionary stands in for TiXL Dict<float>/);
    assert.match(source, defaults);
  }

  assert.match(read("vuo-nodes/my.numbers.data.utils.selectFloatFromDict.c"), /Primary output: float Result \(ColorForValues #868C8D\)/);
  assert.match(read("vuo-nodes/my.numbers.data.utils.selectBoolFromFloatDict.c"), /Primary output: bool Result \(ColorForValues #868C8D\)/);
  assert.match(read("vuo-nodes/my.numbers.data.utils.selectVec2FromDict.c"), /Primary output: Vector2 Result \(ColorForValues #868C8D\)/);
  assert.match(read("vuo-nodes/my.numbers.data.utils.selectVec3FromDict.c"), /Primary output: Vector3 Result \(ColorForValues #868C8D\)/);
});

test("Batch 13 Vuo sources preserve selector edge behavior", () => {
  const floatSource = read("vuo-nodes/my.numbers.data.utils.selectFloatFromDict.c");
  const boolSource = read("vuo-nodes/my.numbers.data.utils.selectBoolFromFloatDict.c");
  const vec2Source = read("vuo-nodes/my.numbers.data.utils.selectVec2FromDict.c");
  const vec3Source = read("vuo-nodes/my.numbers.data.utils.selectVec3FromDict.c");

  assert.match(floatSource, /if \(myDictLookup\(dictionaryInput, select, &value\)\)[\s\S]*\*result = value/);
  assert.match(boolSource, /value > 0\.5/);
  assert.match(vec2Source, /qsort/);
  assert.match(vec2Source, /pairs\[xIndex \+ 1\]/);
  assert.match(vec3Source, /differentCharCount/);
  assert.match(vec3Source, /differentCharCount\(pairs\[startIndex \+ 1\]\.key, selectX\) <= 1/);
});

test("TiXL .t3 defaults support Batch 13 Vuo contracts", () => {
  assert.match(read("external/tixl/Operators/Lib/numbers/data/utils/SelectFloatFromDict.t3"), /\/\*Select\*\/[\s\S]*"DefaultValue": ""/);
  assert.match(read("external/tixl/Operators/Lib/numbers/data/utils/SelectBoolFromFloatDict.t3"), /\/\*Select\*\/[\s\S]*"DefaultValue": ""/);
  assert.match(read("external/tixl/Operators/Lib/numbers/data/utils/SelectVec2FromDict.t3"), /\/\*SelectX\*\/[\s\S]*"DefaultValue": ""/);
  assert.match(read("external/tixl/Operators/Lib/numbers/data/utils/SelectVec3FromDict.t3"), /\/\*SelectX\*\/[\s\S]*"DefaultValue": ""/);
});
