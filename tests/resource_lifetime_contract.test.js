const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/RESOURCE_LIFETIME_CONTRACT.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/resource_lifetime.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/resource_lifetime_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/resource_lifetime");

test("ResourceLifetime contract names allocation, reuse, reallocation, dispose, and view invalidation", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /ResourceLifetime answers:/);
  assert.match(source, /when is a render resource allocated, reused, reallocated, disposed/);
  assert.match(source, /RenderGraph resources -> ResourceLifetime -> Texture2DHandle \/ TextureViewHandle/);
  assert.match(source, /native_resource_api\.py/);
  assert.match(source, /Reallocation invalidates old views/);
  assert.match(source, /source texture disposed/);
});

test("ResourceLifetime fixture declares allocate, reuse, resize, and dispose pressure", () => {
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));

  assert.equal(graph.graphId, "fixture.resource_lifetime");
  assert.equal(graph.frames.length, 3);
  assert.equal(graph.frames[0].resources[0].resolution.width, 3840);
  assert.equal(graph.frames[2].resources[0].resolution.width, 1920);
  assert.deepEqual(graph.expected.actions.map((entry) => entry.action), [
    "allocate",
    "allocate",
    "reuse",
    "reallocate",
  ]);
  assert.equal(graph.expected.invalidatedView, "main.color.srv");
});

test("ResourceLifetime shell emits registry, trace, and invalidated old views", () => {
  const run = spawnSync("python3", [scriptPath, fixturePath, artifactDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);

  const trace = readArtifact("resource_lifetime_trace.json");
  const registry = readArtifact("resource_registry.json");
  const invalidation = readArtifact("view_invalidation_ledger.json");
  const errors = readArtifact("resource_lifetime_errors.json");

  assert.deepEqual(errors, []);
  assert.deepEqual(
    trace
      .filter((entry) => entry.op.startsWith("resource.") && entry.op !== "resource.dispose")
      .map((entry) => ({ frameIndex: entry.frameIndex, resourceId: entry.resourceId, action: entry.op.replace("resource.", "") })),
    [
      { frameIndex: 0, resourceId: "main.color", action: "allocate" },
      { frameIndex: 0, resourceId: "postfx.color", action: "allocate" },
      { frameIndex: 1, resourceId: "main.color", action: "reuse" },
      { frameIndex: 2, resourceId: "main.color", action: "reallocate" },
    ],
  );
  assert.ok(trace.find((entry) => entry.op === "resource.dispose" && entry.resourceId === "postfx.color"));
  assert.equal(registry.resources["main.color"].width, 1920);
  assert.equal(registry.resources["main.color"].height, 1080);
  assert.equal(registry.resources["main.color"].disposed, false);
  assert.equal(registry.resources["postfx.color"].disposed, true);
  assert.equal(registry.views["main.color.srv"].ok, true);
  assert.ok(invalidation.invalidatedViews.find((entry) => entry.viewId === "main.color.srv" && entry.frameIndex === 2));
  assert.ok(invalidation.invalidatedViews.find((entry) => entry.viewId === "postfx.color.srv" && entry.reason === "source texture disposed"));
});

test("native_resource_api exposes allocate_or_reuse and detects reallocation pressure", () => {
  const source = fs.readFileSync(path.join(repoRoot, "docs/runtime/scripts/native_resource_api.py"), "utf8");

  assert.match(source, /def needs_reallocate/);
  assert.match(source, /def allocate_or_reuse_texture/);

  const run = spawnSync("python3", ["-c", `
import json
from docs.runtime.scripts.native_resource_api import TextureResourceRegistry
registry = TextureResourceRegistry()
payload = {"id": "a", "width": 100, "height": 100, "format": "R16G16B16A16_Float", "bindFlags": ["RenderTarget", "ShaderResource"]}
first = registry.allocate_or_reuse_texture(payload)[0]
second = registry.allocate_or_reuse_texture(payload)[0]
payload["width"] = 200
third = registry.allocate_or_reuse_texture(payload)[0]
print(json.dumps([first, second, third, registry.resources["a"].width]))
`], {
    cwd: repoRoot,
    encoding: "utf8",
  });
  assert.equal(run.status, 0, run.stderr);
  assert.deepEqual(JSON.parse(run.stdout), ["allocate", "reuse", "reallocate", 200]);
});

function readArtifact(name) {
  return JSON.parse(fs.readFileSync(path.join(artifactDir, name), "utf8"));
}
