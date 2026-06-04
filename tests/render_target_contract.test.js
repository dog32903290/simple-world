const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/RENDER_TARGET_CONTRACT.md");
const compositionPath = path.join(repoRoot, "vuo-compositions/myworld-render-target-proof.vuo");

function normalizeRenderTargetOptions(options) {
  const requestedResolution = options.requestedResolution ?? { width: 960, height: 540 };
  const resolution = options.resolution ?? { width: 0, height: 0 };
  const width = resolution.width === 0 ? requestedResolution.width : resolution.width;
  const height = resolution.height === 0 ? requestedResolution.height : resolution.height;

  if (width <= 0 || height <= 0 || width > 16384 || height > 16384) {
    return { valid: false, reason: "invalid resolution" };
  }

  return {
    valid: true,
    width,
    height,
    format: options.format === "Unknown" ? "R16G16B16A16_Float" : options.format ?? "R16G16B16A16_Float",
    multisampling: Math.min(Math.max(options.multisampling ?? 4, 1), 32),
    withDepthBuffer: options.withDepthBuffer ?? true,
    withNormalBuffer: options.withNormalBuffer ?? false,
  };
}

function makeTexture(bindFlags) {
  return { bindFlags: new Set(bindFlags) };
}

function createTextureView(texture, viewType) {
  if (texture == null) {
    return { ok: false, view: null, reason: "missing texture" };
  }

  if (viewType === "srv") {
    if (texture.bindFlags.has("DepthStencil")) {
      return { ok: true, view: "ShaderResourceView", format: "R32_Float" };
    }

    return { ok: texture.bindFlags.has("ShaderResource"), view: texture.bindFlags.has("ShaderResource") ? "ShaderResourceView" : null };
  }

  if (viewType === "uav") {
    return {
      ok: texture.bindFlags.has("UnorderedAccess"),
      view: texture.bindFlags.has("UnorderedAccess") ? "UnorderedAccessView" : null,
      reason: texture.bindFlags.has("UnorderedAccess") ? null : "missing UnorderedAccess bind flag",
    };
  }

  if (viewType === "rtv") {
    return {
      ok: texture.bindFlags.has("RenderTarget"),
      view: texture.bindFlags.has("RenderTarget") ? "RenderTargetView" : null,
      reason: texture.bindFlags.has("RenderTarget") ? null : "missing RenderTarget bind flag",
    };
  }

  throw new Error(`Unknown view type: ${viewType}`);
}

test("RenderTarget contract separates render ownership from stateless Texture2D and feedback state", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /RenderTarget := frame-domain render resource that owns Texture2D outputs/);
  assert.match(source, /TiXL donor node := Lib\.image\.generate\.basic\.RenderTarget/);
  assert.match(source, /visible node name := my_RenderTarget/);
  assert.match(source, /Vuo body proof := vuo\.scene\.render\.image2 -> VuoImage/);
  assert.match(source, /ColorForTextures \/ #9F008A/);
  assert.match(source, /not a stateless\s+image filter/);
  assert.match(source, /scene\/command -> offscreen image -> window/);
  assert.match(source, /does not make Vuo pretend to have DirectX11 SRV\/UAV\/RTV handles/);
});

test("RenderTarget contract records TiXL donor inputs, outputs, and cook behavior", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /Operators\/Lib\/image\/generate\/basic\/RenderTarget\.cs/);
  assert.match(source, /Operators\/Lib\/image\/generate\/basic\/RenderTarget\.t3/);
  assert.match(source, /\.help\/docs\/operators\/lib\/image\/generate\/basic\/RenderTarget\.md/);
  assert.match(source, /f9fe78c5-43a6-48ae-8e8c-6cdbbc330dd1/);
  assert.match(source, /Command: Command required/);
  assert.match(source, /Resolution: Int2 = \[0,0\]/);
  assert.match(source, /Multisampling: int = 4/);
  assert.match(source, /TextureFormat: Format = R16G16B16A16_Float/);
  assert.match(source, /ColorBuffer: Texture2D/);
  assert.match(source, /DepthBuffer: Texture2D/);
  assert.match(source, /NormalBuffer: Texture2D/);
  assert.match(source, /Saves the previous viewport/);
  assert.match(source, /Cooks `Command\.GetValue\(context\)`/);
  assert.match(source, /Resolves MSAA/);
  assert.match(source, /Generates mips/);
});

test("RenderTarget fixture normalizes inherited resolution, format fallback, and sample clamp", () => {
  assert.deepEqual(
    normalizeRenderTargetOptions({
      resolution: { width: 0, height: 0 },
      requestedResolution: { width: 1280, height: 720 },
      format: "Unknown",
      multisampling: 99,
      withNormalBuffer: true,
    }),
    {
      valid: true,
      width: 1280,
      height: 720,
      format: "R16G16B16A16_Float",
      multisampling: 32,
      withDepthBuffer: true,
      withNormalBuffer: true,
    },
  );

  assert.deepEqual(
    normalizeRenderTargetOptions({
      resolution: { width: -1, height: 720 },
    }),
    { valid: false, reason: "invalid resolution" },
  );
});

test("TextureView fixture refuses to invent missing GPU bind capabilities", () => {
  const colorTexture = makeTexture(["RenderTarget", "ShaderResource"]);
  const computeTexture = makeTexture(["ShaderResource", "UnorderedAccess"]);
  const depthTexture = makeTexture(["DepthStencil", "ShaderResource"]);
  const plainTexture = makeTexture(["ShaderResource"]);

  assert.deepEqual(createTextureView(colorTexture, "srv"), { ok: true, view: "ShaderResourceView" });
  assert.deepEqual(createTextureView(colorTexture, "rtv"), { ok: true, view: "RenderTargetView", reason: null });
  assert.deepEqual(createTextureView(computeTexture, "uav"), { ok: true, view: "UnorderedAccessView", reason: null });
  assert.deepEqual(createTextureView(depthTexture, "srv"), { ok: true, view: "ShaderResourceView", format: "R32_Float" });
  assert.deepEqual(createTextureView(plainTexture, "uav"), {
    ok: false,
    view: null,
    reason: "missing UnorderedAccess bind flag",
  });
  assert.deepEqual(createTextureView(plainTexture, "rtv"), {
    ok: false,
    view: null,
    reason: "missing RenderTarget bind flag",
  });
});

test("RenderTarget Vuo proof uses offscreen scene rendering, not fake DX11 TextureView nodes", () => {
  const source = fs.readFileSync(compositionPath, "utf8");

  assert.match(source, /RenderTarget host-layer proof/);
  assert.match(source, /FireOnDisplayRefresh \[type="vuo\.event\.fireOnDisplayRefresh"/);
  assert.match(source, /MakeUnlitColorShader \[type="vuo\.shader\.make\.color\.unlit"/);
  assert.match(source, /Make3DTransform \[type="vuo\.transform\.make"/);
  assert.match(source, /MakeCube \[type="vuo\.scene\.make\.cube\.1"/);
  assert.match(source, /MakeCamera \[type="vuo\.scene\.make\.camera\.perspective\.target"/);
  assert.match(source, /MakeSceneList \[type="vuo\.list\.make\.2\.VuoSceneObject"/);
  assert.match(source, /RenderSceneToImage \[type="vuo\.scene\.render\.image2"/);
  assert.match(source, /RenderImageToWindow \[type="vuo\.image\.render\.window2"/);
  assert.match(source, /fillcolor="#9F008A"/);
  assert.match(source, /MakeCube:cube -> MakeSceneList:1/);
  assert.match(source, /MakeCamera:object -> MakeSceneList:2/);
  assert.match(source, /MakeSceneList:list -> RenderSceneToImage:objects/);
  assert.match(source, /RenderSceneToImage:image -> RenderImageToWindow:image/);
  assert.match(source, /It does not prove DX11 Srv\/Uav\/Rtv TextureView parity/);
  assert.doesNotMatch(source, /SrvFromTexture2d|UavFromTexture2d|RtvFromTexture2d/);
  assert.doesNotMatch(source, /vuo\.image\.feedback/);
});
