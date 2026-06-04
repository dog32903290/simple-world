const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/MESH_DRAW_CONTRACT.md");
const commandContractPath = path.join(repoRoot, "docs/runtime/COMMAND_STREAM_CONTRACT.md");

function validateMeshBuffers(mesh) {
  if (mesh == null) {
    return { ok: false, reason: "Undefined Mesh?" };
  }

  if (!mesh.vertexBuffer?.buffer || !mesh.vertexBuffer?.srv) {
    return { ok: false, reason: "Vertex buffer undefined" };
  }

  if (!mesh.indexBuffer?.buffer || !mesh.indexBuffer?.srv) {
    return { ok: false, reason: "Indices buffer undefined" };
  }

  return {
    ok: true,
    vertices: mesh.vertexBuffer,
    indices: mesh.indexBuffer,
    chunkDefs: mesh.chunkDefs ?? null,
  };
}

function buildDrawMeshUnlitCommand(mesh, options = {}) {
  const meshCheck = validateMeshBuffers(mesh);
  if (!meshCheck.ok) {
    return {
      ok: false,
      reason: meshCheck.reason,
      commandOps: [],
      lastValidPolicy: "forbidden",
    };
  }

  const topology = options.topology ?? "TriangleList";
  if (topology !== "TriangleList") {
    return {
      ok: false,
      reason: `unsupported topology: ${topology}`,
      commandOps: [],
      conversionPolicy: "separate visible mesh transform node required",
    };
  }

  const hasVertexShader = options.hasVertexShader ?? true;
  const hasPixelShader = options.hasPixelShader ?? true;
  if (!hasVertexShader || !hasPixelShader) {
    return {
      ok: false,
      reason: "Trying to issue draw call, but pixel and/or vertex shader are null.",
      commandOps: ["inputAssembler", "shaderStage", "rasterizer", "outputMerger"],
    };
  }

  return {
    ok: true,
    meshId: mesh.id,
    shaderSource: "Lib:shaders/3d/mesh/mesh-DrawUnlit.hlsl",
    vertexShaderEntry: "vsMain",
    commandOps: ["inputAssembler", "shaderStage", "rasterizer", "outputMerger", "draw"],
    renderState: {
      blendMode: options.blendMode ?? 0,
      fillMode: options.fillMode ?? 3,
      culling: options.culling ?? "Back",
      enableZTest: options.enableZTest ?? true,
      enableZWrite: options.enableZWrite ?? true,
    },
    material: {
      color: options.color ?? [1, 1, 1, 1],
      texture: options.texture ?? null,
      useVertexColor: options.useVertexColor ?? false,
      alphaCutOff: options.alphaCutOff ?? 0,
    },
  };
}

function makeValidMesh() {
  return {
    id: "cube",
    topology: "TriangleList",
    vertexBuffer: { buffer: "vb", srv: "vbSrv", uav: "vbUav" },
    indexBuffer: { buffer: "ib", srv: "ibSrv", uav: "ibUav" },
  };
}

test("Mesh draw contract names DrawMeshUnlit as the first mesh-to-command donor", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /MeshDraw := MeshBuffers \+ material controls -> ordered Command compound/);
  assert.match(source, /TiXL donor node := Lib\.mesh\.draw\.DrawMeshUnlit/);
  assert.match(source, /visible node name := my_DrawMeshUnlit/);
  assert.match(source, /Vuo can render scene objects but cannot prove TiXL MeshBuffers \/ Command compound parity/);
});

test("Mesh draw contract records TiXL source evidence and default public inputs", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /Operators\/Lib\/mesh\/draw\/DrawMeshUnlit\.cs/);
  assert.match(source, /Operators\/Lib\/mesh\/draw\/DrawMeshUnlit\.t3/);
  assert.match(source, /4499dcb1-c936-49ed-861b-2ad8ae58cb28/);
  assert.match(source, /Mesh: MeshBuffers, required/);
  assert.match(source, /Color: Vector4 = \[1,1,1,1\]/);
  assert.match(source, /BlendMode: int = 0/);
  assert.match(source, /FillMode: int = 3/);
  assert.match(source, /Culling: CullMode = Back/);
  assert.match(source, /EnableZTest: bool = true/);
  assert.match(source, /EnableZWrite: bool = true/);
  assert.match(source, /UseVertexColor: bool = false/);
});

test("Mesh draw contract exposes the internal compound route instead of hiding it", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /my__MeshBufferComponents/);
  assert.match(source, /my_InputAssemblerStage/);
  assert.match(source, /my_SetPixelAndVertexShaderStage/);
  assert.match(source, /my_Rasterizer/);
  assert.match(source, /my_OutputMergerStage/);
  assert.match(source, /my_Draw/);
  assert.match(source, /my_Execute/);
  assert.match(source, /my_RenderTarget/);
  assert.match(source, /Lib:shaders\/3d\/mesh\/mesh-DrawUnlit\.hlsl/);
  assert.match(source, /vsMain/);
});

test("Mesh buffer fixture preserves TiXL _MeshBufferComponents failure behavior", () => {
  assert.deepEqual(validateMeshBuffers(null), { ok: false, reason: "Undefined Mesh?" });
  assert.deepEqual(validateMeshBuffers({ vertexBuffer: null, indexBuffer: { buffer: "ib", srv: "srv" } }), {
    ok: false,
    reason: "Vertex buffer undefined",
  });
  assert.deepEqual(validateMeshBuffers({ vertexBuffer: { buffer: "vb", srv: "srv" }, indexBuffer: { buffer: "ib" } }), {
    ok: false,
    reason: "Indices buffer undefined",
  });
  assert.deepEqual(validateMeshBuffers(makeValidMesh()), {
    ok: true,
    vertices: { buffer: "vb", srv: "vbSrv", uav: "vbUav" },
    indices: { buffer: "ib", srv: "ibSrv", uav: "ibUav" },
    chunkDefs: null,
  });
});

test("DrawMeshUnlit fixture builds an inspectable command route for a valid mesh", () => {
  assert.deepEqual(buildDrawMeshUnlitCommand(makeValidMesh(), {
    color: [1, 0, 0.63, 1],
    texture: "textureA",
    useVertexColor: true,
  }), {
    ok: true,
    meshId: "cube",
    shaderSource: "Lib:shaders/3d/mesh/mesh-DrawUnlit.hlsl",
    vertexShaderEntry: "vsMain",
    commandOps: ["inputAssembler", "shaderStage", "rasterizer", "outputMerger", "draw"],
    renderState: {
      blendMode: 0,
      fillMode: 3,
      culling: "Back",
      enableZTest: true,
      enableZWrite: true,
    },
    material: {
      color: [1, 0, 0.63, 1],
      texture: "textureA",
      useVertexColor: true,
      alphaCutOff: 0,
    },
  });
});

test("DrawMeshUnlit fixture fails before draw when mesh, topology, or shader stage is invalid", () => {
  assert.deepEqual(buildDrawMeshUnlitCommand(null), {
    ok: false,
    reason: "Undefined Mesh?",
    commandOps: [],
    lastValidPolicy: "forbidden",
  });
  assert.deepEqual(buildDrawMeshUnlitCommand(makeValidMesh(), { topology: "LineList" }), {
    ok: false,
    reason: "unsupported topology: LineList",
    commandOps: [],
    conversionPolicy: "separate visible mesh transform node required",
  });
  assert.deepEqual(buildDrawMeshUnlitCommand(makeValidMesh(), { hasPixelShader: false }), {
    ok: false,
    reason: "Trying to issue draw call, but pixel and/or vertex shader are null.",
    commandOps: ["inputAssembler", "shaderStage", "rasterizer", "outputMerger"],
  });
});

test("Command contract points to Mesh draw as the next lane", () => {
  const source = fs.readFileSync(commandContractPath, "utf8");

  assert.match(source, /Mesh draw command/);
  assert.match(source, /Material\/shader stage binding/);
});

test("Mesh draw risk closure matrix makes current fixes and native-renderer blocker explicit", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /Risk Closure Matrix/);
  assert.match(source, /closed now:[\s\S]*missing mesh \/ missing vertex buffer \/ missing index buffer \/ missing SRV/);
  assert.match(source, /closed now:[\s\S]*unsupported topology policy/);
  assert.match(source, /closed now:[\s\S]*missing VS\/PS before draw/);
  assert.match(source, /blocked until native renderer:[\s\S]*Vuo parity for MeshBuffers \/ BufferWithViews \/ InputAssemblerStage/);
  assert.match(source, /Topology conversion must be a separate visible mesh\s+transform node/);
  assert.match(source, /it must not include\s+`draw`/);
  assert.match(source, /Vuo remains a host visual proof, not a parity proof/);
  assert.match(source, /mesh_draw_command\.json/);
  assert.match(source, /mesh_draw_trace\.json/);
  assert.match(source, /mesh_draw_errors\.json/);
});
