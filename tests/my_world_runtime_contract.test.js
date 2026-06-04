const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/MY_WORLD_RUNTIME_CONTRACT.md");

test("My World runtime contract requires a Vuo body-layer trial for new contracts", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /Contract-To-Vuo Proof Gate/);
  assert.match(source, /New runtime contracts should not stay as headless text/);
  assert.match(source, /my_<ExactTiXLNodeName>/);
  assert.match(source, /small Vuo proof composition that wires several related nodes together/);
  assert.match(source, /Vuo proof does not need to prove native GPU parity/);
  assert.match(source, /Headless tests remain the authority/);
  assert.match(source, /Vuo remains the current canvas\/body-layer trial/);
});
