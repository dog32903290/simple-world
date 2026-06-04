const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/MATERIAL_PBR_CONTRACT.md");
const meshContractPath = path.join(repoRoot, "docs/runtime/MESH_DRAW_CONTRACT.md");
const dangerPath = path.join(repoRoot, "docs/runtime/DANGER_NODE_EXPERIMENTS.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/material_pbr_scope.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/material_pbr_scope_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/material_pbr_scope");

const defaultSrvs = {
  baseColorMap: "DefaultAlbedoColorSrv",
  emissiveColorMap: "DefaultEmissiveColorSrv",
  roughnessMetallicOcclusionMap: "DefaultRoughnessMetallicOcclusionSrv",
  normalMap: "DefaultNormalSrv",
};

function makeContext() {
  return {
    pbrMaterial: makeMaterial({ id: "Default" }),
    materials: [],
    contextTextures: { PrefilteredSpecular: "default-prefiltered-specular" },
    trace: [],
    diagnostics: [],
  };
}

function makeMaterial(options = {}) {
  const parameters = {
    baseColor: options.baseColor ?? [1, 1, 1, 1],
    emissiveColor: options.emissiveColor ?? [0, 0, 0, 1],
    roughness: options.roughness ?? 0.25,
    specular: options.specular ?? 1,
    metal: options.metal ?? 0,
  };

  return {
    id: options.id ?? "",
    parameterBuffer: `pbr:${options.id ?? "anonymous"}`,
    parameters,
    srvs: {
      baseColorMap: updateSrv("BaseColorMap", options.baseColorMap, "baseColorMap"),
      emissiveColorMap: updateSrv("EmissiveColorMap", options.emissiveColorMap, "emissiveColorMap"),
      roughnessMetallicOcclusionMap: updateSrv(
        "RoughnessMetallicOcclusionMap",
        options.roughnessMetallicOcclusionMap,
        "roughnessMetallicOcclusionMap",
      ),
      normalMap: updateSrv("NormalMap", options.normalMap, "normalMap"),
    },
  };
}

function updateSrv(inputName, texture, defaultKey) {
  if (texture == null || texture.disposed) {
    return { srv: defaultSrvs[defaultKey], fallback: true, diagnostics: [] };
  }

  if (texture.canCreateSrv === false) {
    return {
      srv: defaultSrvs[defaultKey],
      fallback: true,
      diagnostics: [`Failed to create SRV for ${inputName} texture`],
    };
  }

  return { srv: `srv:${texture.id}`, fallback: false, diagnostics: [] };
}

function setMaterial(context, options, subtree) {
  const previousMaterial = context.pbrMaterial;
  const material = makeMaterial(options);
  const diagnostics = Object.values(material.srvs).flatMap((slot) => slot.diagnostics);

  context.pbrMaterial = material;
  context.materials.push(material);
  context.trace.push(`materialScope.push:${material.id}`);

  const subtreeResult = subtree?.(context) ?? null;

  context.materials.pop();
  context.pbrMaterial = previousMaterial;
  context.trace.push(`materialScope.restore:${previousMaterial.id}`);
  context.diagnostics.push(...diagnostics);

  return { output: subtreeResult, reference: material, diagnostics };
}

function defineMaterials(context, materials, subgraph) {
  const previousCount = context.materials.length;
  const validMaterials = materials.filter(Boolean);

  context.materials.push(...validMaterials);
  context.trace.push(`defineMaterials.push:${validMaterials.map((m) => m.id).join(",")}`);

  const result = subgraph?.(context) ?? null;

  context.materials.splice(previousCount, validMaterials.length);
  context.trace.push(`defineMaterials.restore:${previousCount}`);
  return { output: result, added: validMaterials.length };
}

function setContextTexture(context, id, texture, subtree) {
  const hadPrevious = Object.prototype.hasOwnProperty.call(context.contextTextures, id);
  const previous = context.contextTextures[id];

  context.contextTextures[id] = texture ?? "WhitePixelTexture";
  context.trace.push(`contextTexture.push:${id}`);

  const result = subtree?.(context) ?? null;

  if (hadPrevious) {
    context.contextTextures[id] = previous;
  } else {
    delete context.contextTextures[id];
  }
  context.trace.push(`contextTexture.restore:${id}`);
  return result;
}

function getPbrParameters(context) {
  const material = context.pbrMaterial;
  return {
    PbrParameterBuffer: material.parameterBuffer,
    AlbedoColorMap: material.srvs.baseColorMap.srv,
    EmissiveColorMap: material.srvs.emissiveColorMap.srv,
    RoughnessMetallicOcclusionMap: material.srvs.roughnessMetallicOcclusionMap.srv,
    NormalMap: material.srvs.normalMap.srv,
    BrdfLookupMap: "PbrLookUpTextureSrv",
    PrefilteredSpecularMap: context.contextTextures.PrefilteredSpecular ?? null,
  };
}

function drawMeshPbr(context, mesh, options = {}) {
  const requestedMaterialId = options.useMaterialId ?? "";
  let selectedMaterial = context.pbrMaterial;
  let unresolvedMaterialId = null;

  if (requestedMaterialId) {
    const match = context.materials.find((material) => material.id === requestedMaterialId);
    if (match) {
      selectedMaterial = match;
    } else {
      unresolvedMaterialId = requestedMaterialId;
    }
  }

  const previousMaterial = context.pbrMaterial;
  context.pbrMaterial = selectedMaterial;
  const pbr = getPbrParameters(context);
  context.pbrMaterial = previousMaterial;

  return {
    ok: true,
    meshId: mesh.id,
    requestedMaterialId,
    selectedMaterialId: selectedMaterial.id,
    unresolvedMaterialId,
    shaderSource: "Lib:shaders/3d/mesh/mesh-Draw.hlsl",
    vertexShaderEntry: "vsMain",
    pixelShaderEntry: "psMain",
    constantBuffers: ["transforms", "context", "pointLights", pbr.PbrParameterBuffer],
    shaderResources: [
      pbr.AlbedoColorMap,
      pbr.EmissiveColorMap,
      pbr.RoughnessMetallicOcclusionMap,
      pbr.NormalMap,
      pbr.BrdfLookupMap,
      pbr.PrefilteredSpecularMap,
    ],
    commandOps: ["inputAssembler", "shaderStage", "rasterizer", "outputMerger", "draw"],
  };
}

test("Material/PBR contract records TiXL donor nodes and source evidence", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /Lib\.render\.shading\.SetMaterial/);
  assert.match(source, /Lib\.render\.shading\.DefineMaterials/);
  assert.match(source, /Lib\.mesh\.draw\.DrawMesh/);
  assert.match(source, /0ed2bee3-641f-4b08-8685-df1506e9af3c/);
  assert.match(source, /0bd77dd6-a93a-4e2e-b69b-bbeb73cb5ae9/);
  assert.match(source, /a3c5471e-079b-4d4b-886a-ec02d6428ff6/);
  assert.match(source, /Core\/Rendering\/Material\/PbrMaterial\.cs/);
  assert.match(source, /Core\/Rendering\/Material\/PbrContextSettings\.cs/);
});

test("SetMaterial fixture preserves defaults, SRV fallback, reference, and context restore", () => {
  const context = makeContext();
  const result = setMaterial(context, {
    id: "warm-metal",
    baseColor: [1, 0.6, 0.2, 1],
    roughness: 0.15,
    metal: 1,
    baseColorMap: { id: "albedo", canCreateSrv: false },
    normalMap: { id: "normal", disposed: true },
  }, (scopedContext) => drawMeshPbr(scopedContext, { id: "cube" }));

  assert.equal(context.pbrMaterial.id, "Default");
  assert.deepEqual(context.materials, []);
  assert.deepEqual(context.trace, [
    "materialScope.push:warm-metal",
    "materialScope.restore:Default",
  ]);
  assert.deepEqual(result.reference.parameters, {
    baseColor: [1, 0.6, 0.2, 1],
    emissiveColor: [0, 0, 0, 1],
    roughness: 0.15,
    specular: 1,
    metal: 1,
  });
  assert.equal(result.reference.srvs.baseColorMap.srv, "DefaultAlbedoColorSrv");
  assert.equal(result.reference.srvs.normalMap.srv, "DefaultNormalSrv");
  assert.deepEqual(result.diagnostics, ["Failed to create SRV for BaseColorMap texture"]);
  assert.equal(result.output.selectedMaterialId, "warm-metal");
});

test("DefineMaterials and DrawMesh fixture select named material without leaking context", () => {
  const context = makeContext();
  const copper = makeMaterial({ id: "copper", metal: 1, roughness: 0.35 });
  const glass = makeMaterial({ id: "glass", baseColor: [0.5, 0.8, 1, 0.35], roughness: 0.02 });

  const result = defineMaterials(context, [null, copper, glass], (scopedContext) => (
    drawMeshPbr(scopedContext, { id: "cube" }, { useMaterialId: "glass" })
  ));

  assert.equal(result.added, 2);
  assert.equal(result.output.selectedMaterialId, "glass");
  assert.equal(result.output.requestedMaterialId, "glass");
  assert.equal(result.output.unresolvedMaterialId, null);
  assert.deepEqual(context.materials, []);
  assert.deepEqual(context.trace, [
    "defineMaterials.push:copper,glass",
    "defineMaterials.restore:0",
  ]);
});

test("DrawMesh fixture leaves current material when UseMaterialId is empty or unresolved", () => {
  const context = makeContext();
  context.pbrMaterial = makeMaterial({ id: "current" });
  context.materials.push(makeMaterial({ id: "available" }));

  assert.deepEqual(drawMeshPbr(context, { id: "cube" }, { useMaterialId: "" }), {
    ok: true,
    meshId: "cube",
    requestedMaterialId: "",
    selectedMaterialId: "current",
    unresolvedMaterialId: null,
    shaderSource: "Lib:shaders/3d/mesh/mesh-Draw.hlsl",
    vertexShaderEntry: "vsMain",
    pixelShaderEntry: "psMain",
    constantBuffers: ["transforms", "context", "pointLights", "pbr:current"],
    shaderResources: [
      "DefaultAlbedoColorSrv",
      "DefaultEmissiveColorSrv",
      "DefaultRoughnessMetallicOcclusionSrv",
      "DefaultNormalSrv",
      "PbrLookUpTextureSrv",
      "default-prefiltered-specular",
    ],
    commandOps: ["inputAssembler", "shaderStage", "rasterizer", "outputMerger", "draw"],
  });

  const unresolved = drawMeshPbr(context, { id: "cube" }, { useMaterialId: "missing" });
  assert.equal(unresolved.selectedMaterialId, "current");
  assert.equal(unresolved.unresolvedMaterialId, "missing");
});

test("SetEnvironment fixture scopes PrefilteredSpecular for GetPbrParameters", () => {
  const context = makeContext();
  context.pbrMaterial = makeMaterial({ id: "env-test" });

  const result = setContextTexture(context, "PrefilteredSpecular", "stage-prefiltered", (scopedContext) => (
    drawMeshPbr(scopedContext, { id: "cube" })
  ));

  assert.equal(result.shaderResources.at(-1), "stage-prefiltered");
  assert.equal(context.contextTextures.PrefilteredSpecular, "default-prefiltered-specular");
  assert.deepEqual(context.trace, [
    "contextTexture.push:PrefilteredSpecular",
    "contextTexture.restore:PrefilteredSpecular",
  ]);
});

test("Material/PBR contract names current closure and native renderer blockers", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /texture SRV fallback policy/);
  assert.match(source, /material list push\/restore policy/);
  assert.match(source, /DrawMesh UseMaterialId selection policy/);
  assert.match(source, /PBR binding trace shape/);
  assert.match(source, /exact PbrMaterial constant-buffer layout parity/);
  assert.match(source, /cubemap prefilter \/ DDS environment parity/);
  assert.match(source, /Vuo cannot prove TiXL PbrMaterial \/ SRV \/ constant-buffer parity/);
  assert.match(source, /material_scope_trace\.json/);
  assert.match(source, /mesh_pbr_draw_command\.json/);
});

test("Material/PBR CLI proof emits scope trace, PBR draw command, and fallback diagnostics", () => {
  const run = spawnSync("python3", [scriptPath, fixturePath, artifactDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr);

  const trace = JSON.parse(fs.readFileSync(path.join(artifactDir, "material_scope_trace.json"), "utf8"));
  const command = JSON.parse(fs.readFileSync(path.join(artifactDir, "mesh_pbr_draw_command.json"), "utf8"));
  const errors = JSON.parse(fs.readFileSync(path.join(artifactDir, "pbr_binding_errors.json"), "utf8"));

  assert.deepEqual(trace.map((entry) => entry.op), [
    "defineMaterials.push",
    "contextTexture.push",
    "drawMesh.pbrBinding",
    "contextTexture.restore",
    "defineMaterials.restore",
  ]);
  assert.deepEqual(trace[0].materials, ["copper", "glass"]);
  assert.equal(trace[3].restoredTexture, "default-prefiltered-specular");

  assert.equal(command.ok, true);
  assert.equal(command.meshId, "cube");
  assert.equal(command.topology, "TriangleList");
  assert.deepEqual(command.vertexBuffer, { buffer: "cube.vertexBuffer", srv: "cube.vertexSrv" });
  assert.deepEqual(command.indexBuffer, { buffer: "cube.indexBuffer", srv: "cube.indexSrv" });
  assert.equal(command.requestedMaterialId, "glass");
  assert.equal(command.selectedMaterialId, "glass");
  assert.equal(command.shaderSource, "Lib:shaders/3d/mesh/mesh-Draw.hlsl");
  assert.equal(command.vertexShaderEntry, "vsMain");
  assert.equal(command.pixelShaderEntry, "psMain");
  assert.deepEqual(command.constantBuffers, ["transforms", "context", "pointLights", "pbr:glass"]);
  assert.deepEqual(command.shaderResources, [
    "DefaultAlbedoColorSrv",
    "DefaultEmissiveColorSrv",
    "DefaultRoughnessMetallicOcclusionSrv",
    "DefaultNormalSrv",
    "PbrLookUpTextureSrv",
    "studio_small_08_prefiltered",
  ]);
  assert.deepEqual(command.commandOps, ["inputAssembler", "shaderStage", "rasterizer", "outputMerger", "draw"]);

  assert.deepEqual(errors, [{
    code: "material_pbr.srv_create_failed",
    input: "RoughnessMetallicOcclusionMap",
    textureId: "bad_rmo",
    fallback: "DefaultRoughnessMetallicOcclusionSrv",
  }]);
});

test("Mesh and danger docs point to Material/PBR as the next command lane", () => {
  const meshSource = fs.readFileSync(meshContractPath, "utf8");
  const dangerSource = fs.readFileSync(dangerPath, "utf8");

  assert.match(meshSource, /MATERIAL_PBR_CONTRACT\.md/);
  assert.match(dangerSource, /MATERIAL_PBR_CONTRACT\.md/);
  assert.match(dangerSource, /tests\/material_pbr_contract\.test\.js/);
});
