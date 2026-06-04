const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const harnessPath = path.join(repoRoot, "tools/vuo_harness.py");

test("Vuo harness exposes a real SDK-backed CLI proof lane", () => {
  const source = fs.readFileSync(harnessPath, "utf8");

  assert.match(source, /VUO_SDK_ROOT = REPO \/ "external\/vuo-downloads\/vuo-sdk-local\/framework"/);
  assert.match(source, /VUO_COMPILE = VUO_SDK_ROOT \/ "vuo-compile"/);
  assert.match(source, /VUO_LINK = VUO_SDK_ROOT \/ "vuo-link"/);
  assert.match(source, /CLI_ARTIFACT_ROOT = REPO \/ "artifacts\/vuo_cli"/);
  assert.match(source, /sub\.add_parser\("cli-status"\)/);
  assert.match(source, /sub\.add_parser\("cli-build"\)/);
  assert.match(source, /sub\.add_parser\("cli-run"\)/);
  assert.match(source, /sub\.add_parser\("cli-proof"\)/);
});

test("Vuo CLI build fails loudly when SDK or composition is missing and logs each backend stage", () => {
  const source = fs.readFileSync(harnessPath, "utf8");

  assert.match(source, /"error": "vuo_sdk_missing"/);
  assert.match(source, /"error": "composition_not_found"/);
  assert.match(source, /"error": "vuo_compile_failed"/);
  assert.match(source, /"error": "vuo_link_failed"/);
  assert.match(source, /run_and_log\(\s*\[str\(VUO_COMPILE\), "--output", str\(bitcode\), str\(composition\)\]/);
  assert.match(source, /run_and_log\(\s*\[str\(VUO_LINK\), "--output", str\(executable\), str\(bitcode\)\]/);
  assert.match(source, /\.compile\.stdout\.log/);
  assert.match(source, /\.compile\.stderr\.log/);
  assert.match(source, /\.link\.stdout\.log/);
  assert.match(source, /\.link\.stderr\.log/);
  assert.match(source, /extract_loaded_user_nodes\(log_text\)/);
});

test("Vuo CLI run records executable output, screenshot artifact, and GPU runtime evidence", () => {
  const source = fs.readFileSync(harnessPath, "utf8");

  assert.match(source, /def cli_run_executable/);
  assert.match(source, /subprocess\.Popen\(/);
  assert.match(source, /\.run\.stdout\.log/);
  assert.match(source, /\.run\.stderr\.log/);
  assert.match(source, /\.run\.png/);
  assert.match(source, /capture_runner_window_png\(process\.pid, executable\.stem, screenshot_path/);
  assert.match(source, /image_info\(path\)/);
  assert.match(source, /"createdOpenGlContext": "Created OpenGL context" in text/);
  assert.match(source, /"sawMetalDevice": "VuoMetal" in text or "Metal device" in text/);
  assert.match(source, /"sawSceneRenderer": "VuoSceneRenderer_renderInternal" in text/);
  assert.match(source, /"sawTextureWarning": "GLD_TEXTURE_INDEX_2D is unloadable" in text/);
});

test("Vuo CLI run captures the runner window by CoreGraphics window id for multi-display setups", () => {
  const source = fs.readFileSync(harnessPath, "utf8");

  assert.match(source, /capture_runner_window_png\(process\.pid, executable\.stem, screenshot_path/);
  assert.match(source, /wait_for_runner_window/);
  assert.match(source, /get_cg_windows_for_pid/);
  assert.match(source, /CGWindowListCopyWindowInfo/);
  assert.match(source, /kCGWindowOwnerPID/);
  assert.match(source, /kCGWindowNumber/);
  assert.match(source, /screencapture", "-x"/);
  assert.match(source, /cmd\.extend\(\["-l", str\(window_id\)\]\)/);
  assert.match(source, /runner_window_not_found/);
});

test("Vuo CLI run reports stuck runner shutdown as JSON instead of raising a traceback", () => {
  const source = fs.readFileSync(harnessPath, "utf8");

  assert.match(source, /def stop_process/);
  assert.match(source, /process\.terminate\(\)/);
  assert.match(source, /process\.kill\(\)/);
  assert.match(source, /runner_stop_timeout/);
  assert.match(source, /"stop": stop_result/);
});

test("Vuo CLI run rejects black visual artifacts instead of treating screenshots as proof", () => {
  const source = fs.readFileSync(harnessPath, "utf8");

  assert.match(source, /visual_output_mostly_black/);
  assert.match(source, /png_visual_info\(path\)/);
  assert.match(source, /decode_png_pixels\(path\)/);
  assert.match(source, /zlib\.decompress/);
  assert.match(source, /"mostlyBlack": average_luma < 0\.01 and bright_ratio < 0\.001/);
  assert.match(source, /crop_content_pixels/);
  assert.match(source, /"contentCrop": \{"left": 0\.05, "top": 0\.12, "right": 0\.95, "bottom": 0\.95\}/);
  assert.match(source, /"fullWindow"/);
  assert.match(source, /def unfilter_png_row/);
  assert.match(source, /def paeth/);
});
