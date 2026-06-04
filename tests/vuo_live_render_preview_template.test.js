const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const templatePath = path.join(repoRoot, "vuo-compositions/my-live-render-preview-template.vuo");
const docPath = path.join(repoRoot, "docs/tixl-porting/live_render_preview_template.md");

test("live render preview template owns display refresh and window rendering", () => {
  const source = fs.readFileSync(templatePath, "utf8");

  assert.match(source, /my_LiveRenderPreview template/);
  assert.match(source, /FireOnDisplayRefresh \[type="vuo\.event\.fireOnDisplayRefresh"/);
  assert.match(source, /RenderImageToWindow \[type="vuo\.image\.render\.window2"/);
  assert.match(source, /FireOnDisplayRefresh:requestedFrame -> RenderImageToWindow:refresh/);
  assert.match(source, /MakeNoiseImage:image -> RenderImageToWindow:image/);
  assert.match(source, /FireOnDisplayRefresh:requestedFrame -> MakeNoiseImage:refresh/);
  assert.match(source, /FireOnDisplayRefresh:requestedFrame -> MakeNoiseImage:time/);
});

test("live render preview docs keep clock pressure out of field nodes", () => {
  const source = fs.readFileSync(docPath, "utf8");

  assert.match(source, /The template owns the frame clock/);
  assert.match(source, /Field\/generate nodes should stay semantic/);
  assert.match(source, /my_SphereSDF -> my_RaymarchField -> my_LiveRenderPreview Window/);
});
