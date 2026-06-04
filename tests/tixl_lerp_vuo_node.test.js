#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

const nodePath = path.join(__dirname, "..", "vuo-nodes", "myworld.tixl.lerp.c");
const source = fs.readFileSync(nodePath, "utf8");

assert.match(source, /"title"\s*:\s*"TiXL Lerp"/);
assert.match(source, /VuoInputData\(VuoReal,\s*\{"default":0\.0\}\)\s*a/);
assert.match(source, /VuoInputData\(VuoReal,\s*\{"default":1\.0\}\)\s*b/);
assert.match(source, /VuoInputData\(VuoReal,\s*\{"default":0\.0\}\)\s*f/);
assert.match(source, /VuoInputData\(VuoBoolean,\s*\{"default":false\}\)\s*clamp/);
assert.match(source, /VuoReal\s+factor\s*=\s*f/);
assert.doesNotMatch(source, /\bf\s*=\s*myworldClamp/);
assert.match(source, /a\s*\+\s*\(b\s*-\s*a\)\s*\*\s*factor/);

console.log("PASS TiXL Lerp Vuo node source contract");
