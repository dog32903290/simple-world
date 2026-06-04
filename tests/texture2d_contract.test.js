const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/TEXTURE2D_CONTRACT.md");
const compositionPath = path.join(repoRoot, "vuo-compositions/myworld-texture2d-blend-proof.vuo");

test("Texture2D contract separates stateless image resources from render-target state", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /Texture2D := frame-local image resource/);
  assert.match(source, /Vuo body := VuoImage/);
  assert.match(source, /ColorForTextures \/ #9F008A/);
  assert.match(source, /does not cover render-target state/);
  assert.match(source, /RenderTarget/);
  assert.match(source, /FeedbackState/);
  assert.match(source, /Command/);
  assert.match(source, /Texture2D -> Texture2D/);
  assert.match(source, /VuoImage -> VuoImage/);
  assert.match(source, /FireOnDisplayRefresh:requestedFrame/);
  assert.match(source, /last valid output: forbidden unless the node is a state\/feedback node/);
});

test("Texture2D contract records the first TiXL Blend donor without claiming exact parity", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /Lib\.image\.use\.Blend/);
  assert.match(source, /Operators\/Lib\/image\/use\/Blend\.cs/);
  assert.match(source, /Operators\/Lib\/Assets\/shaders\/img\/fx\/Blend\.hlsl/);
  assert.match(source, /vuo\.image\.blend/);
  assert.match(source, /body-layer parity proof, not a full TiXL clone/);
  assert.match(source, /AlphaMode/);
  assert.match(source, /GenerateMips/);
  assert.match(source, /ScaleMode/);
});

test("Texture2D Vuo proof uses only stateless image source/filter/render nodes and requestedFrame clocking", () => {
  const source = fs.readFileSync(compositionPath, "utf8");

  assert.match(source, /Texture2D \/ VuoImage body-layer proof/);
  assert.match(source, /FireOnDisplayRefresh \[type="vuo\.event\.fireOnDisplayRefresh"/);
  assert.match(source, /MakeColorImage \[type="vuo\.image\.make\.color"/);
  assert.match(source, /MakeCheckerboardImage \[type="vuo\.image\.make\.checkerboard2"/);
  assert.match(source, /BlendImages \[type="vuo\.image\.blend"/);
  assert.match(source, /RenderImageToWindow \[type="vuo\.image\.render\.window2"/);
  assert.match(source, /fillcolor="#9F008A"/);
  assert.match(source, /FireOnDisplayRefresh:requestedFrame -> MakeColorImage:refresh/);
  assert.match(source, /FireOnDisplayRefresh:requestedFrame -> MakeCheckerboardImage:refresh/);
  assert.match(source, /FireOnDisplayRefresh:requestedFrame -> BlendImages:refresh/);
  assert.match(source, /FireOnDisplayRefresh:requestedFrame -> RenderImageToWindow:refresh/);
  assert.match(source, /MakeColorImage:image -> BlendImages:background/);
  assert.match(source, /MakeCheckerboardImage:image -> BlendImages:foreground/);
  assert.match(source, /BlendImages:blended -> RenderImageToWindow:image/);
  assert.match(source, /RenderTarget \/ feedback \/ DX11 views are outside this contract/);
  assert.doesNotMatch(source, /type="[^"]*feedback/i);
  assert.doesNotMatch(source, /FeedbackState \[/);
  assert.doesNotMatch(source, /RenderTarget \[/);
});
