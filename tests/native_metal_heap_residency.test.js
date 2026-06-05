const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/NATIVE_METAL_HEAP_RESIDENCY_PROOF.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/native_metal_heap_residency.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/native_metal_heap_residency_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/native_metal_heap_residency");

test("NativeMetalHeapResidency contract names real Metal heap allocation without replacing policy ledger", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /NativeMetalHeapResidencyProof answers:/);
  assert.match(source, /allocator policy -> MTLHeap descriptor -> heap-backed textures -> residency ledger/);
  assert.match(source, /real Metal heap-backed texture allocation/);
  assert.match(source, /not a complete heap allocator/);
});

test("NativeMetalHeapResidency fixture requests heap-backed transient and persistent textures", () => {
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));

  assert.equal(graph.graphId, "fixture.native_metal_heap_residency");
  assert.equal(graph.heap.storageMode, "private");
  assert.deepEqual(graph.expected.heapBackedResources, ["gradient.temp", "blur.temp", "feedback.history"]);
  assert.ok(graph.resources.some((resource) => resource.id === "feedback.history" && resource.lifetime === "persistent"));
});

test("NativeMetalHeapResidency shell emits real heap residency artifacts", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "native-metal-heap-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);

  const result = readArtifact(tmpDir, "native_metal_heap_residency_result.json");
  const residency = readArtifact(tmpDir, "heap_residency_ledger.json");
  const probe = readArtifact(tmpDir, "metal_heap_probe.json");
  const release = readArtifact(tmpDir, "heap_release_ledger.json");
  const errors = readArtifact(tmpDir, "native_metal_heap_residency_errors.json");

  assert.deepEqual(errors, []);
  assert.equal(result.kind, "NativeMetalHeapResidencyProof");
  assert.equal(result.ok, true);
  assert.equal(result.status, "metal_heap_ready");
  assert.equal(result.claims.realMetalHeapAllocator, true);
  assert.equal(result.claims.heapBackedTextures, true);
  assert.equal(result.claims.residencyLedgerClean, true);
  assert.equal(result.claims.policyLedgerStillSeparate, true);

  assert.equal(probe.actualMetalDeviceCreated, true);
  assert.equal(probe.actualHeapCreated, true);
  assert.equal(probe.actualHeapBackedTexturesCreated, true);
  assert.ok(probe.heapSize >= probe.minimumAlignedSize);
  assert.deepEqual(residency.resources.map((entry) => entry.id), ["gradient.temp", "blur.temp", "feedback.history"]);
  assert.ok(residency.resources.every((entry) => entry.heapBacked === true));
  assert.ok(residency.resources.every((entry) => entry.heapId === "heap.color.product.0"));
  assert.deepEqual(release.liveAtShutdown, []);
  assert.ok(!JSON.stringify({ result, residency, probe, release }).includes("/Users/"));
});

test("NativeMetalHeapResidency refuses resources that do not request heap residency", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "native-metal-heap-bad-"));
  const badFixture = path.join(tmpDir, "bad.graph.json");
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  graph.graphId = "fixture.native_metal_heap_residency.bad";
  graph.resources.find((resource) => resource.id === "blur.temp").heapResidency = "standalone";
  fs.writeFileSync(badFixture, JSON.stringify(graph, null, 2));

  const run = spawnSync("python3", [scriptPath, badFixture, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readArtifact(tmpDir, "native_metal_heap_residency_result.json");
  const errors = readArtifact(tmpDir, "native_metal_heap_residency_errors.json");

  assert.equal(result.ok, false);
  assert.equal(result.status, "heap_residency_contract_failed");
  assert.equal(errors[0].code, "native_metal_heap.resource_not_heap_resident");
  assert.equal(errors[0].resourceId, "blur.temp");
});

test("NativeMetalHeapResidency checked-in artifacts are path-clean and current", () => {
  const run = spawnSync("python3", [scriptPath, fixturePath, artifactDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);

  const result = readArtifact(artifactDir, "native_metal_heap_residency_result.json");
  const residency = readArtifact(artifactDir, "heap_residency_ledger.json");
  const release = readArtifact(artifactDir, "heap_release_ledger.json");

  assert.equal(result.ok, true);
  assert.equal(result.claims.realMetalHeapAllocator, true);
  assert.deepEqual(residency.resources.map((entry) => entry.id), ["gradient.temp", "blur.temp", "feedback.history"]);
  assert.deepEqual(release.liveAtShutdown, []);
  assert.ok(!JSON.stringify({ result, residency, release }).includes("/Users/"));
});

function readArtifact(dir, name) {
  return JSON.parse(fs.readFileSync(path.join(dir, name), "utf8"));
}
