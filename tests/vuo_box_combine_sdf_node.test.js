const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const boxNodePath = path.join(repoRoot, "vuo-nodes/my.field.generate.sdf.boxSdf.c");
const combineNodePath = path.join(repoRoot, "vuo-nodes/my.field.combine.combineSdf.c");
const raymarchNodePath = path.join(repoRoot, "vuo-nodes/my.field.render.raymarchField.c");

test("Vuo BoxSDF node exposes TiXL naming, defaults, and ShaderGraphNode contract output", () => {
  const source = fs.readFileSync(boxNodePath, "utf8");

  assert.match(source, /"title" : "my_BoxSDF"/);
  assert.match(source, /Category: Operators\/Lib\/field\/generate\/sdf/);
  assert.match(source, /TiXL source: external\/tixl\/Operators\/Lib\/field\/generate\/sdf\/BoxSDF\.cs/);
  assert.match(source, /Primary output: ShaderGraphNode/);
  assert.match(source, /ColorForShaderGraph/);
  assert.match(source, /#D142B3/);
  assert.match(source, /VuoInputEvent\(\) update/);
  assert.match(source, /VuoInputData\(VuoPoint3d[^)]*"default":\{"x":0\.0,"y":0\.0,"z":0\.0\}[^)]*\) center/);
  assert.match(source, /VuoInputData\(VuoPoint3d[^)]*"default":\{"x":1\.0,"y":1\.0,"z":1\.0\}[^)]*\) size/);
  assert.match(source, /VuoInputData\(VuoReal[^)]*"default":1\.0[^)]*\) uniformScale/);
  assert.match(source, /VuoInputData\(VuoReal[^)]*"default":0\.05[^)]*\) edgeRadius/);
  assert.match(source, /VuoOutputData\(VuoText[^)]*"name":"Result"[^)]*\) result/);
  assert.match(source, /\\"node\\":\\"BoxSDF\\"/);
  assert.match(source, /\\"category\\":\\"Operators\/Lib\/field\/generate\/sdf\\"/);
  assert.match(source, /\\"size\\":\{\\"x\\":/);
  assert.match(source, /\\"uniformScale\\":/);
  assert.match(source, /\\"edgeRadius\\":/);
  assert.doesNotMatch(source, /VuoImageRenderer/);
});

test("Vuo CombineSDF node is a bounded two-field TiXL ShaderGraphNode adapter", () => {
  const source = fs.readFileSync(combineNodePath, "utf8");

  assert.match(source, /"title" : "my_CombineSDF"/);
  assert.match(source, /Category: Operators\/Lib\/field\/combine/);
  assert.match(source, /TiXL source: external\/tixl\/Operators\/Lib\/field\/combine\/CombineSDF\.cs/);
  assert.match(source, /Primary output: ShaderGraphNode/);
  assert.match(source, /ColorForShaderGraph/);
  assert.match(source, /#D142B3/);
  assert.match(source, /bounded two-field Vuo adapter/);
  assert.match(source, /VuoInputEvent\(\) update/);
  assert.match(source, /VuoInputData\(VuoText[^)]*"name":"FieldA"[^)]*\) fieldA/);
  assert.match(source, /VuoInputData\(VuoText[^)]*"name":"FieldB"[^)]*\) fieldB/);
  assert.match(source, /VuoInputData\(VuoInteger[^)]*"default":2[^)]*\) combineMethod/);
  assert.match(source, /VuoInputData\(VuoReal[^)]*"default":0\.0[^)]*\) k/);
  assert.match(source, /VuoOutputData\(VuoText[^)]*"name":"Result"[^)]*\) result/);
  assert.match(source, /\\"node\\":\\"CombineSDF\\"/);
  assert.match(source, /\\"category\\":\\"Operators\/Lib\/field\/combine\\"/);
  assert.match(source, /\\"adapter\\":\\"two-field-vuo-body\\"/);
  assert.match(source, /\\"fieldA\\":%s,\\"fieldB\\":%s/);
  assert.doesNotMatch(source, /VuoImageRenderer/);
});

test("Vuo RaymarchField can render the SphereSDF plus BoxSDF CombineSDF proof contract", () => {
  const source = fs.readFileSync(raymarchNodePath, "utf8");

  assert.match(source, /parseSdfContract/);
  assert.match(source, /parseBoxSdfContract/);
  assert.match(source, /parseCombineSdfContract/);
  assert.match(source, /"node":"CombineSDF"/);
  assert.match(source, /"node":"BoxSDF"/);
  assert.match(source, /float sdBox/);
  assert.match(source, /float combineSdf/);
  assert.match(source, /uniform int fieldMode/);
  assert.match(source, /uniform int combineMethod/);
  assert.match(source, /VuoShader_setUniform_VuoInteger\s+\(\(\*instance\)->shader, "fieldMode"/);
  assert.match(source, /VuoShader_setUniform_VuoInteger\s+\(\(\*instance\)->shader, "combineMethod"/);
});
