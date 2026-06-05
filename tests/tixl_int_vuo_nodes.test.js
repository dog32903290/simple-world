#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const valueColor = "ColorForValues #868C8D";

const intBasicNodes = [
  {
    name: "IntAdd",
    file: "my.numbers.int.basic.intAdd.c",
    source: "external/tixl/Operators/Lib/numbers/int/basic/IntAdd.cs",
    ports: [
      ["VuoInteger", "value1", "0"],
      ["VuoInteger", "value2", "0"],
    ],
    output: "VuoInteger",
    expression: /value1 \+ value2/,
  },
  {
    name: "SubInts",
    file: "my.numbers.int.basic.subInts.c",
    source: "external/tixl/Operators/Lib/numbers/int/basic/SubInts.cs",
    ports: [
      ["VuoInteger", "input1", "0"],
      ["VuoInteger", "input2", "0"],
    ],
    output: "VuoInteger",
    expression: /input1 - input2/,
  },
  {
    name: "MultiplyInt",
    file: "my.numbers.int.basic.multiplyInt.c",
    source: "external/tixl/Operators/Lib/numbers/int/basic/MultiplyInt.cs",
    ports: [
      ["VuoInteger", "a", "1"],
      ["VuoInteger", "b", "1"],
    ],
    output: "VuoInteger",
    expression: /a \* b/,
  },
  {
    name: "IntDiv",
    file: "my.numbers.int.basic.intDiv.c",
    source: "external/tixl/Operators/Lib/numbers/int/basic/IntDiv.cs",
    ports: [
      ["VuoInteger", "numerator", "0"],
      ["VuoInteger", "denominator", "1"],
    ],
    output: "VuoInteger",
    expression: /denominator == 0 \? 1 : numerator \/ denominator/,
  },
  {
    name: "ModInt",
    file: "my.numbers.int.basic.modInt.c",
    source: "external/tixl/Operators/Lib/numbers/int/basic/ModInt.cs",
    ports: [
      ["VuoInteger", "value", "0"],
      ["VuoInteger", "mod", "1"],
    ],
    output: "VuoInteger",
    expression: /mod == 0 \? 0 : value % mod/,
    bounded: /adapter-bounded.*mod=0/i,
  },
  {
    name: "IntToFloat",
    file: "my.numbers.int.basic.intToFloat.c",
    source: "external/tixl/Operators/Lib/numbers/int/basic/IntToFloat.cs",
    ports: [["VuoInteger", "intValue", "0"]],
    output: "VuoReal",
    expression: /\(VuoReal\)intValue/,
  },
];

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
}

function inputPattern(type, name, defaultValue) {
  return new RegExp(`VuoInputData\\(${type},\\s*\\{\\"default\\":${defaultValue}\\}\\)\\s*${name}\\b`);
}

for (const node of intBasicNodes) {
  test(`my_${node.name} preserves TiXL int source, defaults, color, ports, and expression`, () => {
    const source = read(path.join("vuo-nodes", node.file));

    assert.match(source, new RegExp(`"title"\\s*:\\s*"my_${node.name}"`));
    assert.match(source, /Category: Operators\/Lib\/numbers\/int\/basic/);
    assert.match(source, new RegExp(node.source.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")));
    assert.match(source, new RegExp(`Primary output: .*${valueColor}`));
    assert.match(source, new RegExp(`VuoOutputData\\(${node.output},\\s*\\{\\"name\\":\\"Result\\"\\}\\)\\s*result`));
    assert.match(source, node.expression);
    for (const [type, name, defaultValue] of node.ports) {
      assert.match(source, inputPattern(type, name, defaultValue));
    }
    if (node.bounded) {
      assert.match(source, node.bounded);
    }
  });
}

test("my_IsIntEven preserves TiXL int logic source, default, color, and expression", () => {
  const source = read("vuo-nodes/my.numbers.int.logic.isIntEven.c");

  assert.match(source, /"title"\s*:\s*"my_IsIntEven"/);
  assert.match(source, /Category: Operators\/Lib\/numbers\/int\/logic/);
  assert.match(source, /external\/tixl\/Operators\/Lib\/numbers\/int\/logic\/IsIntEven\.cs/);
  assert.match(source, new RegExp(`Primary output: bool.*${valueColor}`));
  assert.match(source, inputPattern("VuoInteger", "value", "0"));
  assert.match(source, /VuoOutputData\(VuoBoolean,\s*\{"name":"Result"\}\)\s*result/);
  assert.match(source, /value % 2 == 0/);
});

test("TiXL int C# sources and .t3 defaults support the Vuo contracts", () => {
  for (const node of intBasicNodes) {
    const csharp = read(node.source);
    const t3 = read(node.source.replace(/\.cs$/, ".t3"));
    assert.match(csharp, /namespace Lib\.numbers\.@int\.basic/);
    for (const [, , defaultValue] of node.ports) {
      assert.match(t3, new RegExp(`"DefaultValue": ${defaultValue}`));
    }
  }

  const evenSource = read("external/tixl/Operators/Lib/numbers/int/logic/IsIntEven.cs");
  const evenT3 = read("external/tixl/Operators/Lib/numbers/int/logic/IsIntEven.t3");
  assert.match(evenSource, /Value\.GetValue\(context\) % 2 == 0/);
  assert.match(evenT3, /\/\*Value\*\/[\s\S]*"DefaultValue": 0/);
});
