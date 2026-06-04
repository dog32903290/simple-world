#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");

const nodes = [
  {
    node: "Abs",
    file: "my.numbers.float.adjust.abs.c",
    title: "my_Abs",
    sourcePath: "external/tixl/Operators/Lib/numbers/float/adjust/Abs.cs",
    ports: [
      { type: "VuoReal", name: "value", default: "0.0" },
    ],
    output: "result",
  },
  {
    node: "Ceil",
    file: "my.numbers.float.adjust.ceil.c",
    title: "my_Ceil",
    sourcePath: "external/tixl/Operators/Lib/numbers/float/adjust/Ceil.cs",
    ports: [
      { type: "VuoReal", name: "value", default: "0.0" },
    ],
    output: "result",
  },
  {
    node: "Floor",
    file: "my.numbers.float.adjust.floor.c",
    title: "my_Floor",
    sourcePath: "external/tixl/Operators/Lib/numbers/float/adjust/Floor.cs",
    ports: [
      { type: "VuoReal", name: "value", default: "29.0" },
    ],
    output: "result",
  },
  {
    node: "Round",
    file: "my.numbers.float.adjust.round.c",
    title: "my_Round",
    sourcePath: "external/tixl/Operators/Lib/numbers/float/adjust/Round.cs",
    ports: [
      { type: "VuoReal", name: "value", default: "0.0" },
      { type: "VuoReal", name: "stepsPerUnit", default: "1.0" },
      { type: "VuoReal", name: "roundRatio", default: "0.0" },
    ],
    output: "result",
  },
  {
    node: "InvertFloat",
    file: "my.numbers.float.adjust.invertFloat.c",
    title: "my_InvertFloat",
    sourcePath: "external/tixl/Operators/Lib/numbers/float/adjust/InvertFloat.cs",
    ports: [
      { type: "VuoReal", name: "a", default: "1.0" },
      { type: "VuoBoolean", name: "invert", default: "true" },
    ],
    output: "result",
  },
  {
    node: "Clamp",
    file: "my.numbers.float.adjust.clamp.c",
    title: "my_Clamp",
    sourcePath: "external/tixl/Operators/Lib/numbers/float/adjust/Clamp.cs",
    ports: [
      { type: "VuoReal", name: "value", default: "0.0" },
      { type: "VuoReal", name: "min", default: "0.0" },
      { type: "VuoReal", name: "max", default: "1.0" },
    ],
    output: "result",
  },
];

function readNode(file) {
  return fs.readFileSync(path.join(repoRoot, "vuo-nodes", file), "utf8");
}

function inputPattern({ type, name, default: defaultValue }) {
  return new RegExp(`VuoInputData\\(${type},\\s*\\{\\\"default\\\":${defaultValue}\\}\\)\\s*${name}\\b`);
}

for (const node of nodes) {
  test(`${node.title} exposes exact TiXL metadata, color, ports, and defaults`, () => {
    const source = readNode(node.file);

    assert.match(source, new RegExp(`"title"\\s*:\\s*"${node.title}"`));
    assert.match(source, new RegExp(node.sourcePath.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")));
    assert.match(source, /Category: Operators\/Lib\/numbers\/float\/adjust/);
    assert.match(source, /Primary output: float/);
    assert.match(source, /ColorForValues #868C8D/);
    assert.match(source, /"dependencies" : \[\s*\]/);
    assert.match(source, new RegExp(`VuoOutputData\\(VuoReal\\)\\s*${node.output}\\b`));

    for (const port of node.ports) {
      assert.match(source, inputPattern(port));
    }
  });
}

test("visible titles preserve TiXL names with only my_ prefix", () => {
  for (const node of nodes) {
    const source = readNode(node.file);

    assert.match(source, new RegExp(`"title"\\s*:\\s*"my_${node.node}"`));
    assert.doesNotMatch(source, new RegExp(`my_${node.node.replace(/[A-Z]/g, "_$&").toLowerCase()}`));
  }
});

test("nontrivial TiXL semantics are visible in source", () => {
  const floor = readNode("my.numbers.float.adjust.floor.c");
  const round = readNode("my.numbers.float.adjust.round.c");
  const clamp = readNode("my.numbers.float.adjust.clamp.c");

  assert.match(floor, /\(int\)\s*value/);
  assert.doesNotMatch(floor, /\bfloor\s*\(/);
  assert.match(round, /static VuoReal myRoundValue2/);
  assert.match(round, /fmod\(i,\s*u\)/);
  assert.match(round, /1\.0 - 2\.0 \* stepsPerUnit \* v/);
  assert.match(clamp, /fmin\(fmax\(value,\s*min\),\s*max\)/);
});
