const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const setMaterialPath = path.join(repoRoot, "vuo-nodes/my.render.shading.setMaterial.c");
const drawProofPath = path.join(repoRoot, "vuo-nodes/my.mesh.draw.drawMeshPbrProof.c");
const compositionPath = path.join(repoRoot, "vuo-compositions/myworld-vuo-material-pbr-proof.vuo");

test("Vuo my_SetMaterial exposes TiXL naming and emits PbrMaterial contract text", () => {
  const source = fs.readFileSync(setMaterialPath, "utf8");

  assert.match(source, /"title"\s*:\s*"my_SetMaterial"/);
  assert.match(source, /Operators\/Lib\/render\/shading\/SetMaterial\.cs/);
  assert.match(source, /Category: Operators\/Lib\/render\/shading/);
  assert.match(source, /Primary output: PbrMaterial contract text/);
  assert.match(source, /VuoInputEvent\(\) update/);
  assert.match(source, /VuoInputData\(VuoText[^)]*"default":"glass"[^)]*\) MaterialId/);
  assert.match(source, /VuoInputData\(VuoColor[^)]*"r":0\.45[^)]*\) BaseColor/);
  assert.match(source, /VuoInputData\(VuoReal[^)]*"default":0\.25[^)]*\) Roughness/);
  assert.match(source, /VuoInputData\(VuoReal[^)]*"default":1\.0[^)]*\) Specular/);
  assert.match(source, /VuoInputData\(VuoReal[^)]*"default":0\.0[^)]*\) Metal/);
  assert.match(source, /VuoOutputData\(VuoText[^)]*"name":"Reference"[^)]*\) Reference/);
  assert.match(source, /\\"tixlType\\":\\"PbrMaterial\\"/);
  assert.match(source, /\\"node\\":\\"SetMaterial\\"/);
  assert.match(source, /\\"resources\\":\{\\"baseColorMap\\":\\"DefaultAlbedoColorSrv\\"/);
});

test("Vuo my_DrawMeshPbrProof is clearly a proof adapter and consumes material contract text", () => {
  const source = fs.readFileSync(drawProofPath, "utf8");

  assert.match(source, /"title"\s*:\s*"my_DrawMeshPbrProof"/);
  assert.match(source, /not full TiXL DrawMesh \/ MeshBuffers parity/);
  assert.match(source, /VuoInputEvent\(\) renderTick/);
  assert.match(source, /VuoInputData\(VuoText[^)]*"name":"Material"[^)]*\) Material/);
  assert.match(source, /VuoOutputData\(VuoImage[^)]*"name":"Image"[^)]*\) Image/);
  assert.match(source, /parseMaterialContract/);
  assert.match(source, /\\"baseColor\\":\{\\"r\\":/);
  assert.match(source, /VuoShader_setUniform_VuoColor\s+\(\(\*instance\)->shader, "baseColor"/);
  assert.match(source, /VuoImageRenderer_render/);
  assert.doesNotMatch(source, /"title"\s*:\s*"my_DrawMesh"/);
});

test("Vuo material PBR proof composition wires SetMaterial into DrawMeshPbrProof and render window", () => {
  const source = fs.readFileSync(compositionPath, "utf8");

  assert.match(source, /SetMaterial \[type="my\.render\.shading\.setMaterial"/);
  assert.match(source, /DrawMeshPbrProof \[type="my\.mesh\.draw\.drawMeshPbrProof"/);
  assert.match(source, /RenderImageToWindow \[type="vuo\.image\.render\.window2"/);
  assert.match(source, /FireOnDisplayRefresh:requestedFrame -> SetMaterial:update/);
  assert.match(source, /FireOnDisplayRefresh:requestedFrame -> DrawMeshPbrProof:renderTick/);
  assert.match(source, /SetMaterial:Reference -> DrawMeshPbrProof:Material/);
  assert.match(source, /DrawMeshPbrProof:Image -> RenderImageToWindow:image/);
  assert.match(source, /這不是完整 my_DrawMesh/);
});
