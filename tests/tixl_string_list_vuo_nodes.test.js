#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
}

test("my_SplitString preserves TiXL list source, defaults, ports, and bounded behavior", () => {
  const source = read("vuo-nodes/my.string.list.splitString.c");
  assert.match(source, /"title"\s*:\s*"my_SplitString"/);
  assert.match(source, /Category: Operators\/Lib\/string\/list/);
  assert.match(source, /external\/tixl\/Operators\/Lib\/string\/list\/SplitString\.cs/);
  assert.match(source, /Primary output: List<string> \(ColorForString #779552\)/);
  assert.match(source, /VuoInputData\(VuoText,\s*\{"default":"\."\}\)\s*stringText\b/);
  assert.match(source, /VuoInputData\(VuoText,\s*\{"default":"\\\\n"\}\)\s*split\b/);
  assert.match(source, /VuoOutputData\(VuoList_VuoText,\s*\{"name":"Fragments"\}\)\s*fragments\b/);
  assert.match(source, /VuoOutputData\(VuoInteger,\s*\{"name":"Count"\}\)\s*count\b/);
  assert.match(source, /VuoListCreate_VuoText/);
  assert.match(source, /VuoListAppendValue_VuoText/);
  assert.match(source, /adapter-bounded.*empty input.*Count/i);
  assert.match(source, /adapter-bounded.*UTF-16/i);

  const t3 = read("external/tixl/Operators/Lib/string/list/SplitString.t3");
  assert.match(t3, /"DefaultValue": "\."/);
  assert.match(t3, /"DefaultValue": "\\\\n"/);
});

test("my_JoinStringList preserves TiXL list source, defaults, ports, and bounded behavior", () => {
  const source = read("vuo-nodes/my.string.list.joinStringList.c");
  assert.match(source, /"title"\s*:\s*"my_JoinStringList"/);
  assert.match(source, /Category: Operators\/Lib\/string\/list/);
  assert.match(source, /external\/tixl\/Operators\/Lib\/string\/list\/JoinStringList\.cs/);
  assert.match(source, /Primary output: string \(ColorForString #779552\)/);
  assert.match(source, /VuoInputData\(VuoList_VuoText\)\s*input\b/);
  assert.match(source, /VuoInputData\(VuoText,\s*\{"default":"\\\\n"\}\)\s*separator\b/);
  assert.match(source, /VuoOutputData\(VuoText,\s*\{"name":"Result"\}\)\s*result\b/);
  assert.match(source, /VuoText_appendWithSeparator/);
  assert.match(source, /myExpandEscapedNewlines/);
  assert.match(source, /adapter-bounded.*IStatusProvider/i);

  const t3 = read("external/tixl/Operators/Lib/string/list/JoinStringList.t3");
  assert.match(t3, /"DefaultValue": "\\\\n"/);
  assert.match(t3, /"DefaultValue": \{\s*"Values": \[\]\s*\}/);
});
