const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");

const boolNodes = [
  {
    name: "And",
    title: "my_And",
    file: "my.numbers.bool.combine.and.c",
    category: "Operators/Lib/numbers/bool/combine",
    source: "external/tixl/Operators/Lib/numbers/bool/combine/And.cs",
    output: "Result",
    outputType: "VuoBoolean",
    inputs: [
      ["VuoBoolean", "a", "false"],
      ["VuoBoolean", "b", "false"],
    ],
    body: /a\s*&\s*b/,
  },
  {
    name: "Or",
    title: "my_Or",
    file: "my.numbers.bool.combine.or.c",
    category: "Operators/Lib/numbers/bool/combine",
    source: "external/tixl/Operators/Lib/numbers/bool/combine/Or.cs",
    output: "Result",
    outputType: "VuoBoolean",
    inputs: [
      ["VuoBoolean", "a", "false"],
      ["VuoBoolean", "b", "false"],
    ],
    body: /a\s*\|\|\s*b/,
  },
  {
    name: "Not",
    title: "my_Not",
    file: "my.numbers.bool.logic.not.c",
    category: "Operators/Lib/numbers/bool/logic",
    source: "external/tixl/Operators/Lib/numbers/bool/logic/Not.cs",
    output: "Result",
    outputType: "VuoBoolean",
    inputs: [["VuoBoolean", "boolValue", "false"]],
    body: /!\s*boolValue/,
  },
  {
    name: "Xor",
    title: "my_Xor",
    file: "my.numbers.bool.logic.xor.c",
    category: "Operators/Lib/numbers/bool/logic",
    source: "external/tixl/Operators/Lib/numbers/bool/logic/Xor.cs",
    output: "Result",
    outputType: "VuoBoolean",
    inputs: [
      ["VuoBoolean", "a", "false"],
      ["VuoBoolean", "b", "true"],
    ],
    body: /b\s*\?\s*!a\s*:\s*a/,
  },
  {
    name: "BoolToFloat",
    title: "my_BoolToFloat",
    file: "my.numbers.bool.convert.boolToFloat.c",
    category: "Operators/Lib/numbers/bool/convert",
    source: "external/tixl/Operators/Lib/numbers/bool/convert/BoolToFloat.cs",
    output: "Result",
    outputType: "VuoReal",
    inputs: [
      ["VuoBoolean", "boolValue", "false"],
      ["VuoReal", "forFalse", "0.0"],
      ["VuoReal", "forTrue", "1.0"],
    ],
    body: /boolValue\s*\?\s*forTrue\s*:\s*forFalse/,
  },
  {
    name: "BoolToInt",
    title: "my_BoolToInt",
    file: "my.numbers.bool.convert.boolToInt.c",
    category: "Operators/Lib/numbers/bool/convert",
    source: "external/tixl/Operators/Lib/numbers/bool/convert/BoolToInt.cs",
    output: "Result",
    outputType: "VuoInteger",
    inputs: [
      ["VuoBoolean", "boolValue", "false"],
      ["VuoInteger", "resultForFalse", "0"],
      ["VuoInteger", "resultForTrue", "1"],
    ],
    body: /boolValue\s*\?\s*resultForTrue\s*:\s*resultForFalse/,
  },
  {
    name: "PickBool",
    title: "my_PickBool",
    file: "my.numbers.bool.logic.pickBool.c",
    category: "Operators/Lib/numbers/bool/logic",
    source: "external/tixl/Operators/Lib/numbers/bool/logic/PickBool.cs",
    output: "Selected",
    outputType: "VuoBoolean",
    inputs: [
      ["VuoBoolean", "boolValue0", "false"],
      ["VuoBoolean", "boolValue1", "false"],
      ["VuoInteger", "index", "0"],
    ],
    body: /normalizedIndex\s*==\s*0\s*\?\s*boolValue0\s*:\s*boolValue1/,
    adapter: true,
  },
];

for (const node of boolNodes) {
  test(`${node.title} exposes TiXL title, category, source, color, ports, and body`, () => {
    const source = fs.readFileSync(path.join(repoRoot, "vuo-nodes", node.file), "utf8");

    assert.match(source, new RegExp(`"title"\\s*:\\s*"${node.title}"`));
    assert.match(source, new RegExp(`Category: ${node.category.replaceAll("/", "\\/")}`));
    assert.match(source, new RegExp(`TiXL source: ${node.source.replaceAll("/", "\\/")}`));
    assert.match(source, /Primary output type color: ColorForValues #868C8D/);
    assert.match(source, new RegExp(`VuoOutputData\\(${node.outputType}[^)]*\\)\\s+${lowerCamel(node.output)}`));
    assert.match(source, node.body);

    for (const [type, portName, defaultValue] of node.inputs) {
      assert.match(source, new RegExp(`VuoInputData\\(${type},\\s*\\{"default":${escapeRegExp(defaultValue)}\\}\\)\\s*${portName}`));
    }

    if (node.adapter) {
      assert.match(source, /Vuo body-layer adapter limitation: TiXL MultiInputSlot<bool> BoolValues is exposed here as two VuoBoolean inputs/);
    } else {
      assert.doesNotMatch(source, /body-layer adapter limitation/i);
    }
  });
}

test("TiXL source evidence still matches audited bool/value node facts", () => {
  assert.match(readTixl("combine/And.cs"), /Result\.Value\s*=\s*A\.GetValue\(context\)\s*&\s*B\.GetValue\(context\)/);
  assert.match(readTixl("combine/Or.cs"), /var resultValue\s*=\s*a\s*\|\|\s*b/);
  assert.match(readTixl("logic/Not.cs"), /Result\.Value\s*=\s*!\s*BoolValue\.GetValue\(context\)/);
  assert.match(readTixl("logic/Xor.cs"), /Result\.Value\s*=\s*B\.GetValue\(context\)\s*\?\s*!a\s*:\s*a/);
  assert.match(readTixl("convert/BoolToFloat.cs"), /Result\.Value\s*=\s*BoolValue\.GetValue\(context\)\s*\?\s*trueValue\s*:\s*falseValue/s);
  assert.match(readTixl("convert/BoolToInt.cs"), /Result\.Value\s*=\s*BoolValue\.GetValue\(context\)\s*\?\s*valueForTrue\s*:?\s*valueForFalse/s);
  assert.match(readTixl("logic/PickBool.cs"), /public readonly MultiInputSlot<bool> BoolValues = new\(\)/);
  assert.match(readTixl("logic/PickBool.cs"), /Index\.GetValue\(context\)\.Mod\(connections\.Count\)/);
});

function readTixl(relativePath) {
  return fs.readFileSync(path.join(repoRoot, "external/tixl/Operators/Lib/numbers/bool", relativePath), "utf8");
}

function lowerCamel(name) {
  return name[0].toLowerCase() + name.slice(1);
}

function escapeRegExp(value) {
  return value.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}
