const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/TEXTURE_VIEW_CONTRACT.md");
const renderTargetContractPath = path.join(repoRoot, "docs/runtime/RENDER_TARGET_CONTRACT.md");

function makeTexture({
  id,
  bindFlags = [],
  optionFlags = [],
  format = "R16G16B16A16_Float",
  arraySize = 1,
  disposed = false,
} = {}) {
  return {
    id: id ?? "texture",
    bindFlags: new Set(bindFlags),
    optionFlags: new Set(optionFlags),
    format,
    arraySize,
    disposed,
  };
}

function createTextureView(texture, viewKind, options = {}) {
  if (texture == null) {
    return { ok: false, view: null, action: viewKind === "srv" ? "dispose-current-view" : "no-new-view", reason: "missing texture" };
  }

  if (texture.disposed) {
    return { ok: false, view: null, action: "no-new-view", reason: "disposed texture" };
  }

  if (viewKind === "srv") {
    if (texture.bindFlags.has("DepthStencil")) {
      return { ok: true, view: "ShaderResourceView", format: "R32_Float", textureId: texture.id };
    }

    if (!texture.bindFlags.has("ShaderResource")) {
      return { ok: false, view: null, reason: "missing ShaderResource semantics" };
    }

    return { ok: true, view: "ShaderResourceView", format: texture.format, textureId: texture.id };
  }

  if (viewKind === "uav") {
    if (!texture.bindFlags.has("UnorderedAccess")) {
      return { ok: false, view: null, reason: "missing UnorderedAccess bind flag" };
    }

    return { ok: true, view: "UnorderedAccessView", textureId: texture.id };
  }

  if (viewKind === "rtv") {
    if (!texture.bindFlags.has("RenderTarget")) {
      return { ok: false, view: null, reason: "missing RenderTarget bind flag" };
    }

    if (texture.optionFlags.has("TextureCube")) {
      return { ok: true, view: "RenderTargetView", dimension: "Texture2DArray", firstArraySlice: 0, arraySize: 6, textureId: texture.id };
    }

    const requestedIndex = Math.max(0, Math.min(options.arrayIndex ?? 0, 10000));
    const resolvedIndex = Math.min(requestedIndex, Math.max(texture.arraySize - 1, 0));

    if (resolvedIndex > 0) {
      return {
        ok: true,
        view: "RenderTargetView",
        dimension: "Texture2DArray",
        firstArraySlice: resolvedIndex,
        arraySize: 1,
        textureId: texture.id,
      };
    }

    return { ok: true, view: "RenderTargetView", dimension: "Texture2D", textureId: texture.id };
  }

  if (viewKind === "dsv") {
    if (!texture.bindFlags.has("DepthStencil")) {
      return { ok: false, view: null, reason: "missing DepthStencil bind flag" };
    }

    return { ok: true, view: "DepthStencilView", format: "D32_Float", dimension: "Texture2D", textureId: texture.id };
  }

  throw new Error(`unknown view kind: ${viewKind}`);
}

test("TextureView contract separates GPU view identity from texture creation and rendering", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /TextureView := typed access identity for an existing GPU texture/);
  assert.match(source, /visible node names := my_SrvFromTexture2d, my_UavFromTexture2d, my_RtvFromTexture2d, my_DsvFromTexture2d/);
  assert.match(source, /ColorForTextures \/ #9F008A/);
  assert.match(source, /does not create\s+or render a texture/);
  assert.match(source, /Vuo cannot prove DX11\/Metal view identity/);
  assert.match(source, /my_RenderTarget/);
  assert.match(source, /my_KeepPreviousFrame/);
});

test("TextureView contract records all TiXL texture-view donor nodes and GUIDs", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /Operators\/Lib\/render\/_dx11\/api\/SrvFromTexture2d\.cs/);
  assert.match(source, /Operators\/Lib\/render\/_dx11\/api\/UavFromTexture2d\.cs/);
  assert.match(source, /Operators\/Lib\/render\/_dx11\/api\/RtvFromTexture2d\.cs/);
  assert.match(source, /Operators\/Lib\/render\/_dx11\/api\/DsvFromTexture2d\.cs/);
  assert.match(source, /c2078514-cf1d-439c-a732-0d7b31b5084a/);
  assert.match(source, /84e02044-3011-4a5e-b76a-c904d9b4557f/);
  assert.match(source, /57a1ee33-702a-41ad-a17e-b43033d58638/);
  assert.match(source, /4494473b-1868-460e-8ac3-b5d57c8a156e/);
  assert.match(source, /There are no official operator docs/);
});

test("TextureView contract keeps each node to one access force", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /Texture2D -> ShaderResourceView/);
  assert.match(source, /Texture2D -> UnorderedAccessView/);
  assert.match(source, /Texture2D \+ ArrayIndex -> RenderTargetView/);
  assert.match(source, /Texture2D -> DepthStencilView/);
  assert.match(source, /read-only shader sampling view|sampled\/read by a shader/);
  assert.match(source, /read\/write compute view|read\/written by a compute/);
  assert.match(source, /color render output/);
  assert.match(source, /depth\/stencil output/);
  assert.match(source, /Known TiXL source wart/);
});

test("TextureView fixture enforces bind flags instead of inventing capabilities", () => {
  const readable = makeTexture({ id: "readable", bindFlags: ["ShaderResource"] });
  const depth = makeTexture({ id: "depth", bindFlags: ["DepthStencil", "ShaderResource"], format: "R32_Typeless" });
  const writable = makeTexture({ id: "writable", bindFlags: ["ShaderResource", "UnorderedAccess"] });
  const target = makeTexture({ id: "target", bindFlags: ["ShaderResource", "RenderTarget"], arraySize: 4 });
  const cubeTarget = makeTexture({ id: "cube", bindFlags: ["RenderTarget"], optionFlags: ["TextureCube"], arraySize: 6 });
  const plain = makeTexture({ id: "plain", bindFlags: ["ShaderResource"] });

  assert.deepEqual(createTextureView(readable, "srv"), {
    ok: true,
    view: "ShaderResourceView",
    format: "R16G16B16A16_Float",
    textureId: "readable",
  });
  assert.deepEqual(createTextureView(depth, "srv"), {
    ok: true,
    view: "ShaderResourceView",
    format: "R32_Float",
    textureId: "depth",
  });
  assert.deepEqual(createTextureView(writable, "uav"), {
    ok: true,
    view: "UnorderedAccessView",
    textureId: "writable",
  });
  assert.deepEqual(createTextureView(target, "rtv", { arrayIndex: 99 }), {
    ok: true,
    view: "RenderTargetView",
    dimension: "Texture2DArray",
    firstArraySlice: 3,
    arraySize: 1,
    textureId: "target",
  });
  assert.deepEqual(createTextureView(cubeTarget, "rtv"), {
    ok: true,
    view: "RenderTargetView",
    dimension: "Texture2DArray",
    firstArraySlice: 0,
    arraySize: 6,
    textureId: "cube",
  });
  assert.deepEqual(createTextureView(depth, "dsv"), {
    ok: true,
    view: "DepthStencilView",
    format: "D32_Float",
    dimension: "Texture2D",
    textureId: "depth",
  });
  assert.deepEqual(createTextureView(plain, "uav"), {
    ok: false,
    view: null,
    reason: "missing UnorderedAccess bind flag",
  });
  assert.deepEqual(createTextureView(plain, "rtv"), {
    ok: false,
    view: null,
    reason: "missing RenderTarget bind flag",
  });
  assert.deepEqual(createTextureView(plain, "dsv"), {
    ok: false,
    view: null,
    reason: "missing DepthStencil bind flag",
  });
});

test("TextureView fixture records missing and disposed texture failure policies", () => {
  assert.deepEqual(createTextureView(null, "srv"), {
    ok: false,
    view: null,
    action: "dispose-current-view",
    reason: "missing texture",
  });
  assert.deepEqual(createTextureView(null, "uav"), {
    ok: false,
    view: null,
    action: "no-new-view",
    reason: "missing texture",
  });
  assert.deepEqual(createTextureView(makeTexture({ disposed: true }), "rtv"), {
    ok: false,
    view: null,
    action: "no-new-view",
    reason: "disposed texture",
  });
});

test("RenderTarget contract now points TextureView work to the dedicated contract", () => {
  const source = fs.readFileSync(renderTargetContractPath, "utf8");

  assert.match(source, /docs\/runtime\/TEXTURE_VIEW_CONTRACT\.md/);
  assert.match(source, /my_DsvFromTexture2d/);
  assert.match(source, /not Vuo body nodes/);
});
