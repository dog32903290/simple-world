const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const layeringPath = path.join(repoRoot, "docs/runtime/LAYERING.md");
const gapsPath = path.join(repoRoot, "docs/runtime/CONTRACT_GAPS.md");
const contractsReadmePath = path.join(repoRoot, "docs/runtime/contracts/README.md");

test("runtime layering doc separates node contracts, proof hosts, and future native GPU backend", () => {
  const source = fs.readFileSync(layeringPath, "utf8");

  assert.match(source, /TiXL-like node runtime contract/);
  assert.match(source, /proof \/ host adapter layer/);
  assert.match(source, /future native GPU backend/);
  assert.match(source, /Vuo \/ JS \/ Python \/ WebGL are proof hosts only/);
  assert.match(source, /NativeRendererBackend interface proof is not real Metal rendering/);
  assert.match(source, /deterministic captured sample is not GPU readback/);
  assert.match(source, /TiXL mesh draw resource binding proof does not equal full PBR binding/);
  assert.match(source, /HLSL-to-MSL translation/);
  assert.match(source, /TiXL runtime parity/);
  assert.match(source, /backend replacement/);
});

test("runtime layering doc classifies current shell, fixture, and artifact surfaces", () => {
  const source = fs.readFileSync(layeringPath, "utf8");

  [
    "docs/runtime/scripts/render_graph_shell.py",
    "docs/runtime/scripts/resource_lifetime_shell.py",
    "docs/runtime/scripts/native_resource_api.py",
    "docs/runtime/scripts/native_render_pipeline_shell.py",
    "docs/runtime/scripts/native_renderer_backend_interface_shell.py",
    "docs/runtime/fixtures/*",
    "docs/runtime/artifacts/*"
  ].forEach((entry) => assert.match(source, new RegExp(escapeRegex(entry))));
});

test("contract gaps doc defines the bug triage and contract-gap patch workflow", () => {
  const source = fs.readFileSync(gapsPath, "utf8");

  assert.match(source, /Every bug must be classified as one of/);
  assert.match(source, /code bug/);
  assert.match(source, /contract gap/);
  assert.match(source, /proof gap/);
  assert.match(source, /backend gap/);
  assert.match(source, /If the classification is `contract gap`/);
  assert.match(source, /update `docs\/runtime\/CONTRACT_GAPS\.md`/);
  assert.match(source, /add or update a fixture/);
  assert.match(source, /add or update a test/);
  assert.match(source, /only then patch implementation/);
  assert.match(source, /Do not add visual nodes to close a contract gap/);
});

test("runtime contracts README routes readers without claiming renderer completion", () => {
  const source = fs.readFileSync(contractsReadmePath, "utf8");

  assert.match(source, /Runtime Contract Entrypoints/);
  assert.match(source, /LAYERING\.md/);
  assert.match(source, /CONTRACT_GAPS\.md/);
  assert.match(source, /docs\/contracts\/NODE_ADMISSION_LEVELS\.md/);
  assert.match(source, /RENDERER_BACKEND_CONTRACT\.md/);
  assert.match(source, /NATIVE_RENDERER_BACKEND_INTERFACE\.md/);
  assert.match(source, /This README does not claim renderer completion/);
});

function escapeRegex(value) {
  return value.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}
