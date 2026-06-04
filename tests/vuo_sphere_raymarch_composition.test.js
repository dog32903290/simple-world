const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const compositionPath = path.join(repoRoot, "vuo-compositions/myworld-sphere-raymarch-proof.vuo");

test("Vuo proof composition connects my_SphereSDF to my_RaymarchField to Vuo render window", () => {
  const source = fs.readFileSync(compositionPath, "utf8");

  assert.match(source, /SphereSDF \[type="my\.field\.generate\.sdf\.sphereSdf"/);
  assert.match(source, /RaymarchField \[type="my\.field\.render\.raymarchField"/);
  assert.match(source, /my_SphereSDF/);
  assert.match(source, /my_RaymarchField/);
  assert.match(source, /fillcolor="#D142B3"/);
  assert.match(source, /fillcolor="#22B8C2"/);
  assert.match(source, /RenderImageToWindow \[type="vuo.image.render.window2"/);
  assert.match(source, /FireOnDisplayRefresh \[type="vuo.event.fireOnDisplayRefresh"/);
  assert.match(source, /<renderTick>renderTick\\l/);
  assert.match(source, /<update>Update\\l/);
  assert.match(source, /FireOnDisplayRefresh:requestedFrame -> SphereSDF:update/);
  assert.match(source, /FireOnDisplayRefresh:requestedFrame -> RaymarchField:renderTick/);
  assert.match(source, /FireOnDisplayRefresh:requestedFrame -> RenderImageToWindow:refresh/);
  assert.match(source, /SphereSDF:result -> RaymarchField:sdfField/);
  assert.match(source, /RaymarchField:drawCommand -> RenderImageToWindow:image/);
  assert.doesNotMatch(source, /my\.field\.render\.sphereRaymarch/);
});
