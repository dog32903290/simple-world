#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.join(__dirname, "..");
const category = "Operators/Lib/numbers/float/basic";
const colorName = "ColorForValues";
const colorHex = "#868C8D";

const nodes = [
  {
    name: "Add",
    file: "my.numbers.float.basic.add.c",
    inputs: [
      ["Input1", 0.0],
      ["Input2", 0.0],
    ],
    expression: /Input1\s*\+\s*Input2/,
  },
  {
    name: "Sub",
    file: "my.numbers.float.basic.sub.c",
    inputs: [
      ["Input1", 0.0],
      ["Input2", 0.0],
    ],
    expression: /Input1\s*-\s*Input2/,
  },
  {
    name: "Multiply",
    file: "my.numbers.float.basic.multiply.c",
    inputs: [
      ["A", 1.0],
      ["B", 1.0],
    ],
    expression: /A\s*\*\s*B/,
  },
  {
    name: "Div",
    file: "my.numbers.float.basic.div.c",
    inputs: [
      ["A", 1.0],
      ["B", 1.0],
    ],
    expression: /B\s*==\s*0\.0\s*\?\s*NAN\s*:\s*A\s*\/\s*B/,
  },
  {
    name: "Modulo",
    file: "my.numbers.float.basic.modulo.c",
    inputs: [
      ["Value", 0.0],
      ["ModuloValue", 1.0],
    ],
    expression: /Value\s*-\s*ModuloValue\s*\*\s*floor\s*\(\s*Value\s*\/\s*ModuloValue\s*\)/,
  },
  {
    name: "Sqrt",
    file: "my.numbers.float.basic.sqrt.c",
    inputs: [["Value", 1.0]],
    expression: /sqrt\s*\(\s*Value\s*\)/,
  },
  {
    name: "Pow",
    file: "my.numbers.float.basic.pow.c",
    inputs: [
      ["Value", 1.0],
      ["Exponent", 1.0],
    ],
    expression: /pow\s*\(\s*Value\s*,\s*Exponent\s*\)/,
  },
];

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
}

function defaultPattern(value) {
  return value.toFixed(1).replace(".", "\\.");
}

function t3DefaultPattern(port, value) {
  return new RegExp(`/\\*${port}\\*/[\\s\\S]*?"DefaultValue": ${defaultPattern(value)}`);
}

for (const node of nodes) {
  test(`${node.name} Vuo node preserves TiXL naming, category, ports, and scalar color evidence`, () => {
    const source = read(path.join("vuo-nodes", node.file));

    assert.match(source, new RegExp(`"title"\\s*:\\s*"my_${node.name}"`));
    assert.match(source, new RegExp(`Category: ${category}`));
    assert.match(source, new RegExp(`Primary output: float \\(${colorName} ${colorHex}\\)`));
    assert.match(source, new RegExp(`Source: external/tixl/Operators/Lib/numbers/float/basic/${node.name}\\.cs`));
    assert.match(source, /VuoOutputData\(VuoReal,\s*\{"name":"Result"\}\)\s*result/);
    assert.match(source, node.expression);
    assert.doesNotMatch(source, /"TiXL [A-Za-z]+"/);
    assert.doesNotMatch(source, new RegExp(`my_${node.name.toLowerCase()}`));

    for (const [port, defaultValue] of node.inputs) {
      assert.match(
        source,
        new RegExp(`VuoInputData\\(VuoReal,\\s*\\{"default":${defaultPattern(defaultValue)}\\}\\)\\s*${port}\\b`)
      );
    }
  });
}

test("TiXL C# sources and .t3 defaults support the Vuo contracts", () => {
  for (const node of nodes) {
    const csharp = read(path.join("external/tixl/Operators/Lib/numbers/float/basic", `${node.name}.cs`));
    const t3 = read(path.join("external/tixl/Operators/Lib/numbers/float/basic", `${node.name}.t3`));

    assert.match(csharp, /namespace Lib\.numbers\.@float\.basic/);
    assert.match(csharp, /public readonly Slot<float> Result = new\(\)/);

    for (const [port, defaultValue] of node.inputs) {
      assert.match(csharp, new RegExp(`InputSlot<float>\\s+${port}\\s*=\\s*new\\(\\)`));
      assert.match(t3, t3DefaultPattern(port, defaultValue));
    }
  }

  assert.match(read("external/tixl/Operators/Lib/numbers/float/basic/Div.cs"), /b\s*==\s*0[\s\S]*float\.NaN/);
  assert.match(read("external/tixl/Operators/Lib/numbers/float/basic/Modulo.cs"), /Result\.Value\s*=\s*0/);
});
