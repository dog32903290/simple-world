// runtime/point_ops_pbrshading — the Command-rail PBR shading ops (decls + rationale in point_ops_pbrshading.h).
//
// Two op FAMILIES:
//   (1) CONTEXT-STACK WRITERS — SetMaterial / SetPointLight / SetFog / UseMaterial / DefineMaterials. Each has
//       a Command SubGraph input; the driver PUSHES the resolved currency (sw_pbr_scope.h) around the SubGraph
//       cook and pops after (mirror of the S3a var scope / C1 camera scope — the push/restore is in the driver,
//       point_graph_command_cook.cpp + its resident twin). The op cook itself only FORWARDS the cooked subtree
//       items (cc.inputCommand), exactly like SetVarCmd / SetRequestedResolution.
//   (2) THE READER — DrawMeshPbr: Mesh in → Command out (DrawKind::MeshPbr). It borrows the cooked mesh buffers
//       (like DrawMeshUnlit) and stamps the lit item's material/lights/fog from the surfaced cc.pbr* (the
//       driver filled those from the live scope at cc-fill time — the single fill site, both legs).
//
// TiXL: SetMaterial.cs / SetPointLight.cs / SetFog.cs / UseMaterial.cs / DefineMaterials.cs (push/pop) +
// DrawMesh.cs (reads context.PbrMaterial/PointLights/FogParameters). The BRDF math is in mesh_draw_pbr.metal.
#include "runtime/point_ops_pbrshading.h"

#include <algorithm>
#include <cstdint>

#include "runtime/point_graph.h"       // CmdCookCtx / registerCmdOp / cookVecN
#include "runtime/render_command.h"    // RenderCommand / RenderDrawItem / DrawKind / MeshPbrItem
#include "runtime/sw_pbr_scope.h"      // kMaxPbrLights (the light-array cap the op clamps to)

namespace sw {

// ── (1) WRITERS: forward the cooked SubGraph. The scope push/restore already happened in the driver. ──
static RenderCommand forwardSubtree(CmdCookCtx& c) {
  RenderCommand rc;
  if (c.inputCommand) rc.items = c.inputCommand->items;
  return rc;
}
RenderCommand cookSetMaterial(CmdCookCtx& c) { return forwardSubtree(c); }
RenderCommand cookSetPointLight(CmdCookCtx& c) { return forwardSubtree(c); }
RenderCommand cookSetFog(CmdCookCtx& c) { return forwardSubtree(c); }
RenderCommand cookUseMaterial(CmdCookCtx& c) { return forwardSubtree(c); }
RenderCommand cookDefineMaterials(CmdCookCtx& c) { return forwardSubtree(c); }

// ── (2) READER: DrawMeshPbr — Mesh in → Command out (DrawKind::MeshPbr), lit item stamped from cc.pbr*. ──
// Mirrors cookDrawMeshUnlit: unwired Mesh → empty chain (executor draws nothing — faithful to an empty
// MeshBuffers). The item carries the geometry borrow + the live material/lights/fog (or the defaults the
// driver left when no scope op is present).
RenderCommand cookDrawMeshPbr(CmdCookCtx& c) {
  RenderCommand rc;
  if (!c.meshVtx || !c.meshIdx || c.meshFaceCount == 0) return rc;  // no mesh wired → empty (no draw)

  RenderDrawItem it{};
  it.kind = DrawKind::MeshPbr;
  it.meshVtx = c.meshVtx;
  it.meshIdx = c.meshIdx;
  it.meshIndexCount = c.meshFaceCount;  // FACE count; the executor draws ×3 (SV_VertexID-driven)
  // Color: DrawMesh's litColor *= Color (pbr-render.hlsl:106). sw's DrawMeshPbr has no explicit Color input in
  // the minimal path → the .t3 default white (the tint is a no-op; material BaseColor carries the color).
  float colDef[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  float col[4];
  cookVecN(c, "Color", colDef, 4, col);
  it.color[0] = col[0]; it.color[1] = col[1]; it.color[2] = col[2]; it.color[3] = col[3];
  it.applyTransform = true;  // production always projects; the golden's -bug drops it (mis-project tooth)

  // PBR currency: stamp the live scope surfaced onto cc by the driver (pbrHasMaterial/pbrHasFog/pbrLights).
  // Defaults (no scope op) = SwPbrParameters default ctor + zero lights + ambient fog (fogActive=false).
  it.hasPbr = true;
  if (c.pbrHasMaterial) it.pbr.material = c.pbrMaterial;  // else the default gray material (SwPbrParameters ctor)
  int lc = c.pbrLightCount;
  if (lc > kMaxPbrLights) lc = kMaxPbrLights;
  if (lc < 0) lc = 0;
  it.pbr.lightCount = lc;
  for (int i = 0; i < lc; ++i) it.pbr.lights[i] = c.pbrLights[i];
  it.pbr.fogActive = c.pbrHasFog;
  if (c.pbrHasFog) it.pbr.fog = c.pbrFog;  // else the executor uses swFogAmbientDefault()
  rc.items.push_back(it);
  return rc;
}

void registerPbrShadingOps() {
  registerCmdOp("SetMaterial", cookSetMaterial);
  registerCmdOp("SetPointLight", cookSetPointLight);
  registerCmdOp("SetFog", cookSetFog);
  registerCmdOp("UseMaterial", cookUseMaterial);
  registerCmdOp("DefineMaterials", cookDefineMaterials);
  registerCmdOp("DrawMeshPbr", cookDrawMeshPbr);
}

}  // namespace sw
