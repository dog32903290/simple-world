const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/NATIVE_RESOURCE_LIFETIME_POLICY_PROOF.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/native_resource_lifetime_policy.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/native_resource_lifetime_policy_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/native_resource_lifetime_policy");

test("NativeResourceLifetimePolicy contract names aliasing fences and leak reports", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /NativeResourceLifetimePolicyProof answers:/);
  assert.match(source, /RenderGraph lifetimes -> allocator policy -> alias plan -> release fences -> leak report/);
  assert.match(source, /feedback history must not alias transient render targets/);
  assert.match(source, /not a real GPU heap allocator/);
});

test("NativeResourceLifetimePolicy fixture declares transient alias and persistent feedback pressure", () => {
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));

  assert.equal(graph.graphId, "fixture.native_resource_lifetime_policy");
  assert.equal(graph.frames.length, 3);
  assert.ok(graph.resources.some((resource) => resource.id === "gradient.temp" && resource.lifetime === "transient"));
  assert.ok(graph.resources.some((resource) => resource.id === "blur.temp" && resource.aliasGroup === "color.transient"));
  assert.ok(graph.resources.some((resource) => resource.id === "feedback.history" && resource.lifetime === "persistent"));
  assert.deepEqual(graph.expected.aliasPairs, [["gradient.temp", "blur.temp"]]);
  assert.deepEqual(graph.expected.nonAliasReasons.map((entry) => entry.reason), ["overlapping lifetime", "persistent across frames"]);
});

test("NativeResourceLifetimePolicy shell emits alias plan barriers release fences and leak report", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "native-resource-policy-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);

  const result = readArtifact(tmpDir, "native_resource_lifetime_policy_result.json");
  const aliasPlan = readArtifact(tmpDir, "alias_plan.json");
  const barrierLedger = readArtifact(tmpDir, "barrier_ledger.json");
  const releaseFences = readArtifact(tmpDir, "release_fences.json");
  const leakReport = readArtifact(tmpDir, "leak_report.json");
  const errors = readArtifact(tmpDir, "native_resource_lifetime_policy_errors.json");

  assert.deepEqual(errors, []);
  assert.equal(result.kind, "NativeResourceLifetimePolicyProof");
  assert.equal(result.ok, true);
  assert.equal(result.claims.aliasPlannerRan, true);
  assert.equal(result.claims.releaseFencesTracked, true);
  assert.equal(result.claims.leakReportClean, true);
  assert.equal(result.claims.realGpuHeapAllocator, false);

  assert.deepEqual(aliasPlan.aliases.map((entry) => entry.resources), [["gradient.temp", "blur.temp"]]);
  assert.equal(aliasPlan.aliases[0].heapSlot, "heap.color.transient.0");
  assert.ok(aliasPlan.nonAliases.some((entry) => entry.resources.includes("feedback.history") && entry.reason === "persistent across frames"));
  assert.ok(aliasPlan.nonAliases.some((entry) => entry.resources.includes("gradient.temp") && entry.resources.includes("feedback.history") && entry.reason === "overlapping lifetime"));
  assert.deepEqual(barrierLedger.barriers.map((entry) => entry.kind), ["write-after-read", "alias-rebind", "read-after-write"]);
  assert.ok(releaseFences.fences.some((entry) => entry.resourceId === "gradient.temp" && entry.releaseAfterPass === "feedback_pass"));
  assert.ok(releaseFences.fences.some((entry) => entry.resourceId === "feedback.history" && entry.releaseAfterFrame === 2));
  assert.deepEqual(leakReport.leaks, []);
  assert.deepEqual(leakReport.liveAtShutdown, []);
  assert.ok(!JSON.stringify({ result, aliasPlan, barrierLedger, releaseFences, leakReport }).includes("/Users/"));
});

test("NativeResourceLifetimePolicy refuses aliasing resources with overlapping lifetimes", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "native-resource-policy-bad-"));
  const badFixture = path.join(tmpDir, "bad.graph.json");
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  graph.graphId = "fixture.native_resource_lifetime_policy.bad";
  graph.resources.find((resource) => resource.id === "blur.temp").firstUse = "gradient_pass";
  fs.writeFileSync(badFixture, JSON.stringify(graph, null, 2));

  const run = spawnSync("python3", [scriptPath, badFixture, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readArtifact(tmpDir, "native_resource_lifetime_policy_result.json");
  const errors = readArtifact(tmpDir, "native_resource_lifetime_policy_errors.json");
  const aliasPlan = readArtifact(tmpDir, "alias_plan.json");

  assert.equal(result.ok, false);
  assert.equal(result.status, "alias_policy_failed");
  assert.equal(errors[0].code, "resource_policy.expected_alias_blocked");
  assert.deepEqual(errors[0].resources, ["gradient.temp", "blur.temp"]);
  assert.deepEqual(aliasPlan.aliases, []);
});

test("NativeResourceLifetimePolicy checked-in artifacts are path-clean and current", () => {
  const run = spawnSync("python3", [scriptPath, fixturePath, artifactDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);

  const result = readArtifact(artifactDir, "native_resource_lifetime_policy_result.json");
  const aliasPlan = readArtifact(artifactDir, "alias_plan.json");
  const leakReport = readArtifact(artifactDir, "leak_report.json");

  assert.equal(result.ok, true);
  assert.equal(result.claims.aliasPlannerRan, true);
  assert.deepEqual(aliasPlan.aliases.map((entry) => entry.resources), [["gradient.temp", "blur.temp"]]);
  assert.deepEqual(leakReport.leaks, []);
  assert.ok(!JSON.stringify({ result, aliasPlan, leakReport }).includes("/Users/"));
});

function readArtifact(dir, name) {
  return JSON.parse(fs.readFileSync(path.join(dir, name), "utf8"));
}
