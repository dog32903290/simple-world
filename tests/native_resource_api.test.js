const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const apiPath = path.join(repoRoot, "docs/runtime/scripts/native_resource_api.py");
const laneScriptPath = path.join(repoRoot, "docs/runtime/scripts/native_runtime_lane.py");
const laneArtifactPath = path.join(repoRoot, "docs/runtime/artifacts/native_runtime_lane/texture_views.json");

function runPython(source) {
  const run = spawnSync("python3", ["-c", source], {
    cwd: repoRoot,
    encoding: "utf8",
  });
  assert.equal(run.status, 0, run.stderr);
  return JSON.parse(run.stdout);
}

test("native_resource_api exposes Texture2D registry and TextureView creation", () => {
  const source = fs.readFileSync(apiPath, "utf8");

  assert.match(source, /class Texture2DHandle/);
  assert.match(source, /class TextureViewHandle/);
  assert.match(source, /class TextureResourceRegistry/);
  assert.match(source, /def create_texture_view/);
  assert.match(source, /def allocate_render_target_resources/);
  assert.match(source, /TextureView identity API/);
  assert.match(source, /not a GPU backend/);
});

test("native_resource_api enforces bind flags, depth SRV format, and array slice clamp", () => {
  const result = runPython(`
import json
from docs.runtime.scripts.native_resource_api import Texture2DHandle, create_texture_view

target = Texture2DHandle(id="target", width=320, height=180, format="R16G16B16A16_Float", bindFlags=("ShaderResource", "RenderTarget"), arraySize=4)
depth = Texture2DHandle(id="depth", width=320, height=180, format="D32_Float", bindFlags=("DepthStencil", "ShaderResource"))
plain = Texture2DHandle(id="plain", width=320, height=180, format="R8G8B8A8_UNorm", bindFlags=("ShaderResource",))
cube = Texture2DHandle(id="cube", width=256, height=256, format="R16G16B16A16_Float", bindFlags=("RenderTarget",), optionFlags=("TextureCube",), arraySize=6)

payload = {
  "srv": create_texture_view(target, "srv").to_json(),
  "depthSrv": create_texture_view(depth, "srv").to_json(),
  "uavFail": create_texture_view(plain, "uav").to_json(),
  "rtvSlice": create_texture_view(target, "rtv", arrayIndex=99).to_json(),
  "cubeRtv": create_texture_view(cube, "rtv").to_json(),
  "dsv": create_texture_view(depth, "dsv").to_json(),
}
print(json.dumps(payload))
`);

  assert.deepEqual(result.srv, {
    ok: true,
    textureId: "target",
    type: "SRV",
    format: "R16G16B16A16_Float",
  });
  assert.deepEqual(result.depthSrv, {
    ok: true,
    textureId: "depth",
    type: "SRV",
    format: "R32_Float",
  });
  assert.deepEqual(result.uavFail, {
    ok: false,
    textureId: "plain",
    type: "UAV",
    reason: "missing UnorderedAccess bind flag",
  });
  assert.deepEqual(result.rtvSlice, {
    ok: true,
    textureId: "target",
    type: "RTV",
    dimension: "Texture2DArray",
    firstArraySlice: 3,
    arraySize: 1,
  });
  assert.deepEqual(result.cubeRtv, {
    ok: true,
    textureId: "cube",
    type: "RTV",
    dimension: "Texture2DArray",
    firstArraySlice: 0,
    arraySize: 6,
  });
  assert.deepEqual(result.dsv, {
    ok: true,
    textureId: "depth",
    type: "DSV",
    format: "D32_Float",
    dimension: "Texture2D",
  });
});

test("native_resource_api registry invalidates views when a texture is disposed", () => {
  const result = runPython(`
import json
from docs.runtime.scripts.native_resource_api import TextureResourceRegistry

registry = TextureResourceRegistry()
registry.register_texture({
  "id": "rt.color",
  "width": 320,
  "height": 180,
  "format": "R16G16B16A16_Float",
  "bindFlags": ["RenderTarget", "ShaderResource"]
})
registry.create_view("rt.color", "srv")
registry.create_view("rt.color", "rtv")
before = registry.to_json()
registry.dispose_texture("rt.color")
after = registry.to_json()
print(json.dumps({"before": before, "after": after}))
`);

  assert.equal(result.before.resources["rt.color"].disposed, false);
  assert.equal(result.before.views["rt.color.srv"].ok, true);
  assert.equal(result.after.resources["rt.color"].disposed, true);
  assert.equal(result.after.views["rt.color.srv"].ok, false);
  assert.equal(result.after.views["rt.color.srv"].reason, "source texture disposed");
  assert.equal(result.after.views["rt.color.rtv"].ok, false);
});

test("native runtime lane uses native_resource_api instead of local TextureView helpers", () => {
  const source = fs.readFileSync(laneScriptPath, "utf8");

  assert.match(source, /from native_resource_api import/);
  assert.doesNotMatch(source, /def create_texture_views/);
  assert.doesNotMatch(source, /def allocate_render_target/);
});

test("native runtime lane artifact keeps API-shaped texture view identity", () => {
  const views = JSON.parse(fs.readFileSync(laneArtifactPath, "utf8"));

  assert.equal(views.views["rt.color.srv"].type, "SRV");
  assert.equal(views.views["rt.color.rtv"].dimension, "Texture2D");
  assert.equal(views.views["rt.depth.srv"].format, "R32_Float");
  assert.equal(views.views["rt.depth.dsv"].dimension, "Texture2D");
});
