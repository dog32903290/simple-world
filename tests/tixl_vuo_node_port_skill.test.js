const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const skillPath = path.join(repoRoot, "skills/tixl-vuo-node-port/SKILL.md");
const agentsPath = path.join(repoRoot, "AGENTS.md");

test("TiXL to Vuo node port skill preserves the proven porting workflow", () => {
  const source = fs.readFileSync(skillPath, "utf8");

  assert.match(source, /name: tixl-vuo-node-port/);
  assert.match(source, /TiXL source audit/);
  assert.match(source, /visible title.*`my_<ExactTiXLNodeName>`/i);
  assert.match(source, /`SphereSDF` -> `my_SphereSDF`/);
  assert.doesNotMatch(source, /my_sphere_sdf/);
  assert.match(source, /Classify by TiXL `Operators\/Lib` path/);
  assert.match(source, /Color Contract/);
  assert.match(source, /type color/i);
  assert.match(source, /`ShaderGraphNode`.*`ColorForShaderGraph`/s);
  assert.match(source, /`ColorForShaderGraph`.*`#D142B3`/s);
  assert.match(source, /`Texture2D`.*`ColorForTextures`/s);
  assert.match(source, /Do not color nodes by namespace/i);
  assert.match(source, /`renderTick`/);
  assert.match(source, /Fire on Display Refresh.*renderTick/);
  assert.match(source, /data ports are semantic values/i);
  assert.match(source, /persistent WebGL shader server/);
  assert.match(source, /Contract-to-Vuo proof gate/);
  assert.match(source, /Every new contract needs a matching Vuo trial/);
  assert.match(source, /small proof composition that wires several related nodes together/);
  assert.match(source, /what it proves and what it does not prove/);
  assert.match(source, /Vuo inputs are const/i);
  assert.match(source, /Not Installed/);
  assert.match(source, /install.*Application Support\/Vuo\/Modules/i);
  assert.match(source, /CLI.*comment/i);
  assert.match(source, /tools\/vuo_harness\.py cli-proof/);
  assert.match(source, /mostlyBlack/i);
  assert.match(source, /module cache.*not fatal/i);
  assert.match(source, /black window/i);
});

test("repo instructions advertise the local TiXL to Vuo port skill", () => {
  const source = fs.readFileSync(agentsPath, "utf8");

  assert.match(source, /skills\/tixl-vuo-node-port\/SKILL\.md/);
  assert.match(source, /my_<ExactTiXLNodeName>/);
  assert.match(source, /TiXL type color/);
});
