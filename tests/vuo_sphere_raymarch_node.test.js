const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const sphereNodePath = path.join(repoRoot, "vuo-nodes/my.field.generate.sdf.sphereSdf.c");
const raymarchNodePath = path.join(repoRoot, "vuo-nodes/my.field.render.raymarchField.c");

test("Vuo SphereSDF node exposes TiXL naming, category, and ShaderGraphNode contract output", () => {
  const source = fs.readFileSync(sphereNodePath, "utf8");

  assert.match(source, /"title" : "my_SphereSDF"/);
  assert.match(source, /Category: Operators\/Lib\/field\/generate\/sdf/);
  assert.match(source, /Primary output: ShaderGraphNode/);
  assert.match(source, /VuoInputEvent\(\) update/);
  assert.match(source, /"dependencies" : \[\s*\]/);
  assert.match(source, /VuoInputData\(VuoPoint3d[^)]*\) center/);
  assert.match(source, /VuoInputData\(VuoReal[^)]*\) radius/);
  assert.match(source, /VuoOutputData\(VuoText[^)]*\) result/);
  assert.match(source, /\\"tixlType\\":\\"ShaderGraphNode\\"/);
  assert.doesNotMatch(source, /my_SphereSDF_RaymarchField/);
  assert.doesNotMatch(source, /VuoImageRenderer/);
});

test("Vuo RaymarchField node exposes TiXL naming, Command primary output, and renderTick", () => {
  const source = fs.readFileSync(raymarchNodePath, "utf8");

  assert.match(source, /"title" : "my_RaymarchField"/);
  assert.match(source, /Category: Operators\/Lib\/field\/render/);
  assert.match(source, /Primary TiXL output: Command/);
  assert.match(source, /"dependencies" : \[\s*"VuoImageRenderer"\s*\]/);
  assert.match(source, /VuoInputEvent\(\) renderTick/);
  assert.match(source, /VuoInputData\(VuoText[^)]*\) sdfField/);
  assert.match(source, /VuoInputData\(VuoColor[^)]*\) color/);
  assert.match(source, /VuoOutputData\(VuoImage[^)]*\) drawCommand/);
  assert.match(source, /parseSphereSdfContract/);
  assert.match(source, /strstr\(sdfField, "\\"center\\":\{\\"x\\":"\)/);
  assert.match(source, /VuoShader_setUniform_VuoReal\s+\(\(\*instance\)->shader, "radius", renderRadius\)/);
  assert.match(source, /VuoImageRenderer_render\(\(\*instance\)->shader, renderWidth, renderHeight, VuoImageColorDepth_8\)/);
  assert.doesNotMatch(source, /\n\s*width\s*=/);
  assert.doesNotMatch(source, /\n\s*height\s*=/);
  assert.doesNotMatch(source, /\n\s*radius\s*=/);
});
