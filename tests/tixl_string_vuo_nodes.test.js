#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");

const nodes = [
  {
    title: "my_FloatToString",
    file: "my.string.convert.floatToString.c",
    category: "Operators/Lib/string/convert",
    source: "external/tixl/Operators/Lib/string/convert/FloatToString.cs",
    color: "ColorForString #779552",
    output: "VuoText",
    ports: [
      ["VuoReal", "value", "0\\.0"],
      ["VuoText", "format", "\"\\{0:0\\.000\\}\""],
    ],
    required: [/myFormatReal/, /Invalid Format/, /adapter-bounded.*string\.Format/i],
  },
  {
    title: "my_IntToString",
    file: "my.string.convert.intToString.c",
    category: "Operators/Lib/string/convert",
    source: "external/tixl/Operators/Lib/string/convert/IntToString.cs",
    color: "ColorForString #779552",
    output: "VuoText",
    ports: [
      ["VuoInteger", "value", "0"],
      ["VuoText", "format", "\"\\{0:0\\}\""],
    ],
    required: [/myFormatInteger/, /Invalid Format/, /adapter-bounded.*string\.Format/i],
  },
  {
    title: "my_IndexOf",
    file: "my.string.search.indexOf.c",
    category: "Operators/Lib/string/search",
    source: "external/tixl/Operators/Lib/string/search/IndexOf.cs",
    color: "ColorForValues #868C8D",
    output: "VuoInteger",
    ports: [
      ["VuoText", "originalString", "\"\""],
      ["VuoText", "searchPattern", "\"\""],
    ],
    required: [/strstr/, /\*index = -1/],
  },
  {
    title: "my_SearchAndReplace",
    file: "my.string.search.searchAndReplace.c",
    category: "Operators/Lib/string/search",
    source: "external/tixl/Operators/Lib/string/search/SearchAndReplace.cs",
    color: "ColorForString #779552",
    output: "VuoText",
    ports: [
      ["VuoText", "originalString", "\"\""],
      ["VuoText", "searchPattern", "\"\""],
      ["VuoText", "replace", "\"\""],
      ["VuoBoolean", "useRegex", "false"],
    ],
    required: [/VuoText_replace/, /myExpandEscapedNewlines/, /adapter-bounded.*regex/i],
  },
  {
    title: "my_SubString",
    file: "my.string.search.subString.c",
    category: "Operators/Lib/string/search",
    source: "external/tixl/Operators/Lib/string/search/SubString.cs",
    color: "ColorForString #779552",
    output: "VuoText",
    ports: [
      ["VuoText", "inputText", "\"\""],
      ["VuoInteger", "start", "0"],
      ["VuoInteger", "length", "10000"],
    ],
    required: [/mySubstringBytes/, /clamp/],
  },
  {
    title: "my_StringLength",
    file: "my.string.list.stringLength.c",
    category: "Operators/Lib/string/list",
    source: "external/tixl/Operators/Lib/string/list/StringLength.cs",
    color: "ColorForValues #868C8D",
    output: "VuoInteger",
    ports: [["VuoText", "inputString", "\"ten plus eleven is 21\""]],
    required: [/strlen/, /adapter-bounded.*UTF-16/i],
  },
  {
    title: "my_StringRepeat",
    file: "my.string.combine.stringRepeat.c",
    category: "Operators/Lib/string/combine",
    source: "external/tixl/Operators/Lib/string/combine/StringRepeat.cs",
    color: "ColorForString #779552",
    output: "VuoText",
    ports: [
      ["VuoText", "fragment", "\"\""],
      ["VuoInteger", "count", "5"],
    ],
    required: [/myRepeatText/, /1000/],
  },
  {
    title: "my_ChangeCase",
    file: "my.string.transform.changeCase.c",
    category: "Operators/Lib/string/transform",
    source: "external/tixl/Operators/Lib/string/transform/ChangeCase.cs",
    color: "ColorForString #779552",
    output: "VuoText",
    ports: [
      ["VuoText", "inputText", "\"\""],
      ["VuoInteger", "mode", "0"],
    ],
    required: [/VuoText_changeCase/, /VuoTextCase_UppercaseAll/, /VuoTextCase_LowercaseAll/],
  },
];

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
}

function inputPattern(type, name, defaultValue) {
  return new RegExp(`VuoInputData\\(${type},\\s*\\{\\"default\\":${defaultValue}\\}\\)\\s*${name}\\b`);
}

for (const node of nodes) {
  test(`${node.title} preserves TiXL string source, defaults, color, ports, and bounded behavior`, () => {
    const source = read(path.join("vuo-nodes", node.file));
    assert.match(source, new RegExp(`"title"\\s*:\\s*"${node.title}"`));
    assert.match(source, new RegExp(`Category: ${node.category}`));
    assert.match(source, new RegExp(node.source.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")));
    assert.match(source, new RegExp(`Primary output: .*${node.color}`));
    assert.match(source, new RegExp(`VuoOutputData\\(${node.output},\\s*\\{\\"name\\":\\"(?:Output|Result|Index|Length)\\"\\}\\)\\s*\\w+`));
    for (const [type, name, defaultValue] of node.ports) {
      assert.match(source, inputPattern(type, name, defaultValue));
    }
    for (const pattern of node.required) {
      assert.match(source, pattern);
    }
  });
}

test("TiXL string C# sources and .t3 defaults support the Vuo contracts", () => {
  for (const node of nodes) {
    assert.ok(fs.existsSync(path.join(repoRoot, node.source)), node.source);
    const t3 = read(node.source.replace(/\.cs$/, ".t3"));
    for (const [, , defaultValue] of node.ports) {
      const literal = defaultValue.replaceAll("\\", "");
      assert.match(t3, new RegExp(`"DefaultValue": ${literal}`));
    }
  }
});
