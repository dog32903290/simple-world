// S2b GROUP-SRT render golden — the end-to-end proof that the Group op's transform-context push (TiXL
// Group.cs, cooked by point_ops_group.cpp's cookGroup stamp) moves/scales its child layers on BOTH the
// flat AND resident cook legs. NO new cook-core code: this is a HARNESS cut driving the already-landed
// Group op + the executor's per-item group right-multiply (point_ops_rendertarget.cpp Layer2d/Mesh case:
// finalO2W = childO2W · groupObjectToWorld) through a REAL graph:
//
//     SolidRed(Texture2D) → Layer2d(Scale=0.3) → Group(Translate=+0.5 x) → RenderTarget(Texture2D)
//
// ── TiXL GROUND TRUTH (the transform this golden derives expected pixels from) ──
//   Layer2d (Scale=0.3, Stretch mode, SQUARE target → scaleX=scaleY=0.3): a RED quad of object half-
//     extent 0.3. At the default camera (z=0, d·tan(fov/2)=1) NDC.x = worldX, so un-grouped the quad
//     spans NDC x ∈ [−0.3, +0.3].
//   Group (Translation=(+0.5,0,0), Scale=(1,1,1), Rotation=0): context.ObjectToWorld = T(+0.5). The
//     child's vertex world x shifts by +0.5 → the quad spans NDC x ∈ [+0.2, +0.8].
//
// ── CLOSED-FORM EXPECTED (derived HOST-SIDE via the SAME mat4TransformPointDivW the VS reproduces) ──
//   The quad's grouped NDC center = project (origin) through (Layer2d S·R·T)·(group T)·camera. We compute
//   the moved center host-side (NOT hardcoded) and probe deep-interior plateaus:
//     • probe @ groupedCenterNDC (≈ +0.5, 0) = RED  — the quad MOVED here under the group push.
//     • probe @ NDC (0,0)                    = BACKGROUND — the UN-grouped center; the quad left it.
//   WITHOUT the group push (the -bug) the quad stays at [−0.3,0.3]: NDC 0 = RED (should be background),
//   groupedCenter +0.5 = background (should be RED) → BOTH probes flip → RED. The bite is in the REAL
//   executor multiply + the REAL cookGroup stamp, not a flipped assertion.
//
// ── SECOND TOOTH — SCALE (order-independent corroborator, exercises a non-translate SRT term) ──
//   A SECOND graph: Group(Scale=(2,2,1)) over the SAME Scale=0.3 quad → grouped half-extent = 0.3·2 =
//   0.6. Probe @ NDC 0.45: INSIDE grouped (0.45<0.6), OUTSIDE un-grouped (0.45>0.3). Faithful → RED;
//   -bug (no group) → background. Proves the group SCALE term (not just translate) reaches the executor.
//
// ── injectBug (-bug leg) ──
//   The group bite is structural (an identity Group is a LEGIT identity, not a bug). So the -bug drives a
//   CPU op flag g_groupDropPush read by a test wrapper cookGroupForTest: when set the op runs the REAL
//   gather + IsEnabled but DROPS the SRT stamp (hasGroup stays false) → the executor skips the multiply →
//   the quad never moves/scales → RED. A CPU op flag (parallel to executeCollectFirstOnlyForTest), NOT a
//   shader test seam (constitution rule). The resident leg runs the identical drop, so a resident path
//   that lost the group push is caught here (the S2c resident-black-hole precedent).
//
// ── DISCIPLINE (Cut62-63 golden rules) ──
//   Probe DEEP interior plateaus (the moved center, the un-grouped center, a mid-radius point), NEVER a
//   quad edge (no fwidth). Single-sample. No depth (Layer2d draws depth-disabled). Expected NDC DERIVED
//   from the transform math, not pinned to arbitrary constants.
//
// Zone: runtime leaf. SolidImage is a TEST-ONLY harness fixture (the layercompose/StubRenderTarget
// precedent) installed via setDynamicSpecs + registerTexOp; Layer2d / Group / RenderTarget are the REAL
// builtins under test (Group re-registered with the cookGroupForTest wrapper for the drop flag).
#include "runtime/point_ops.h"

#include "runtime/field_camera.h"    // Mat4 / mat4Mul / groupObjectToWorld / layer2dObjectToWorld / objectToClipSpace / defaultLayerCameraForward / mat4TransformPointDivW
#include "runtime/point_graph.h"     // PointGraph::cook/cookResident, registerBuiltinPointOps/registerCmdOp/registerTexOp, CmdCookCtx, TexCookCtx, cookVecN

#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"                // Graph/Node/NodeSpec/PortSpec/pinId/setDynamicSpecs/findSpec
#include "runtime/graph_bridge.h"         // libFromGraph (flat Graph → SymbolLibrary)
#include "runtime/render_command.h"       // RenderCommand / RenderDrawItem
#include "runtime/resident_eval_graph.h"  // buildEvalGraph (production resident path)
#include "runtime/tixl_point.h"           // EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {
// Test-only op flag: when set, cookGroupForTest runs the REAL cookGroup then DROPS the SRT stamp (the
// group push is lost) → the executor's per-item multiply is skipped → the child quad does not move/scale.
// OFF in production. A CPU op flag (parallel to executeCollectFirstOnlyForTest), NOT a shader bug-branch.
bool g_groupDropPush = false;

// The test wrapper around the REAL cookGroup (point_ops_group.cpp) — registered over "Group" for the
// golden so the production cookGroup stays flag-free. The drop clears the stamp the real op just applied
// (the gather + IsEnabled stay real, only the transform is lost — exactly the bug the executor must catch).
RenderCommand cookGroupForTest(CmdCookCtx& c) {
  RenderCommand rc = cookGroup(c);
  if (g_groupDropPush)
    for (RenderDrawItem& it : rc.items) {
      it.hasGroup = false;
      float id[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
      for (int i = 0; i < 16; ++i) it.groupObjectToWorld[i] = id[i];
    }
  return rc;
}

// Solid-RED Texture2D source (the layercompose harness-fixture precedent): a clear pass fills the driver-
// pre-sized output with opaque RED. (Clear pass, not replaceRegion — works for any storage mode.)
void cookSolidImage(TexCookCtx& c) {
  if (!c.output) return;
  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(c.output);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(1.0, 0.0, 0.0, 1.0));  // RED, opaque
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
}

NodeSpec atomicSpec(const char* type, std::vector<PortSpec> ports) {
  NodeSpec s; s.type = type; s.title = type; s.ports = std::move(ports); s.evaluate = nullptr;
  return s;
}

// Install dynamic specs for the whole S2b graph. Layer2d / Group / RenderTarget are real builtins with no
// static NodeSpec (the existing Layer2d/Execute/layercompose goldens supply specs the same way); SolidImage
// is the test source op. PortSpec positional init:
//   {id, name, dataType, isInput, def, minV, maxV, widget, labels, pinless, vecArity, multiInput, strDef}
void installGroupSpecs() {
  std::map<std::string, NodeSpec> dyn;
  dyn["SolidImage"] = atomicSpec("SolidImage", {{"out", "out", "Texture2D", false}});
  dyn["Layer2d"] = atomicSpec(
      "Layer2d",
      {{"Image", "Image", "Texture2D", true},
       {"out", "out", "Command", false},
       {"Scale", "Scale", "Float", true, 1.0f, 0.0f, 10.0f, Widget::Slider, {}, true},
       {"ScaleMode", "ScaleMode", "Float", true, 0.0f, 0.0f, 5.0f, Widget::Enum, {}, true},
       {"BlendMode", "BlendMode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum, {}, true}});
  // Group: ONE MultiInput Command input + UniformScale + IsEnabled + a Command output, plus the THREE
  // vec3 params (Scale/Rotation/Translation). A vec param is N CONSECUTIVE Float ports with ids
  // "<base>.x/.y/.z" (graph.h:22-25 — the head ends in .x, vecArity on the head; resolveNodeParams emits
  // out["<base>.x"] etc. so cookVecN(c,"<base>") finds them). NOT one port with vecArity=3 (that would
  // leave the components unresolved → the bug the first golden run caught).
  dyn["Group"] = atomicSpec(
      "Group",
      {{"Command", "Command", "Command", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, true},
       {"out", "out", "Command", false},
       {"Scale.x", "Scale", "Float", true, 1.0f, 0.0f, 10.0f, Widget::Vec, {}, true, 3},
       {"Scale.y", "Scale.y", "Float", true, 1.0f, 0.0f, 10.0f, Widget::Vec, {}, true, 1},
       {"Scale.z", "Scale.z", "Float", true, 1.0f, 0.0f, 10.0f, Widget::Vec, {}, true, 1},
       {"Rotation.x", "Rotation", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 3},
       {"Rotation.y", "Rotation.y", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
       {"Rotation.z", "Rotation.z", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
       {"Translation.x", "Translation", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
       {"Translation.y", "Translation.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
       {"Translation.z", "Translation.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
       {"UniformScale", "UniformScale", "Float", true, 1.0f, 0.0f, 10.0f, Widget::Slider, {}, true},
       {"IsEnabled", "IsEnabled", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool, {}, true}});
  dyn["RenderTarget"] = atomicSpec(
      "RenderTarget",
      {{"command", "command", "Command", true},
       {"out", "out", "Texture2D", false},
       {"Resolution", "Resolution", "Float", true, 4.0f, 0.0f, 4.0f, Widget::Enum, {}, true},
       {"CustomW", "CustomW", "Float", true, 256.0f, 1.0f, 4096.0f, Widget::Slider, {}, true},
       {"CustomH", "CustomH", "Float", true, 256.0f, 1.0f, 4096.0f, Widget::Slider, {}, true}});
  setDynamicSpecs(std::move(dyn));
}

int outPortIdx(const char* type) {
  const NodeSpec* s = findSpec(type);
  if (!s) return -1;
  for (size_t i = 0; i < s->ports.size(); ++i)
    if (!s->ports[i].isInput) return (int)i;
  return -1;
}
int inPortIdx(const char* type, const char* dataType) {
  const NodeSpec* s = findSpec(type);
  if (!s) return -1;
  for (size_t i = 0; i < s->ports.size(); ++i)
    if (s->ports[i].isInput && s->ports[i].dataType == dataType) return (int)i;
  return -1;
}

// Build the S2b graph: SolidRed → Layer2d(Scale=quadScale) → Group(SRT) → RenderTarget.
// Node ids: 1=SolidRed 2=Layer2d 3=Group 4=RenderTarget (terminal).
Graph buildGroupGraph(float quadScale, float tx, float groupScaleXY, uint32_t W, uint32_t H) {
  Graph g;
  Node sa; sa.id = 1; sa.type = "SolidImage"; g.nodes.push_back(sa);
  Node l; l.id = 2; l.type = "Layer2d";
  l.params["Scale"] = quadScale;
  l.params["ScaleMode"] = 4.0f;  // Stretch (Layer2dScaleMode::Stretch=4): square src+target → scaleX·=1
  l.params["BlendMode"] = 0.0f;  // Normal
  g.nodes.push_back(l);
  Node grp; grp.id = 3; grp.type = "Group";
  grp.params["Translation.x"] = tx;          // +x translate (cookVecN keys vec by .x/.y/.z)
  grp.params["Scale.x"] = groupScaleXY;      // group scale x
  grp.params["Scale.y"] = groupScaleXY;      // group scale y
  grp.params["Scale.z"] = 1.0f;              // group scale z
  grp.params["UniformScale"] = 1.0f;
  g.nodes.push_back(grp);
  Node rt; rt.id = 4; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f;  // Custom
  rt.params["CustomW"] = (float)W; rt.params["CustomH"] = (float)H;
  g.nodes.push_back(rt);

  const int solidOut = outPortIdx("SolidImage");
  const int layerTexIn = inPortIdx("Layer2d", "Texture2D");
  const int layerOut = outPortIdx("Layer2d");
  const int grpCmdIn = inPortIdx("Group", "Command");
  const int grpOut = outPortIdx("Group");
  const int rtCmdIn = inPortIdx("RenderTarget", "Command");

  g.connections.push_back({101, pinId(1, solidOut), pinId(2, layerTexIn)});  // SolidRed → Layer2d
  g.connections.push_back({102, pinId(2, layerOut), pinId(3, grpCmdIn)});    // Layer2d → Group.Command
  g.connections.push_back({103, pinId(3, grpOut), pinId(4, rtCmdIn)});       // Group → RenderTarget
  return g;
}

// Read the RED channel of pg.target() at NDC (x,y). NDC.y=+1 → row 0 (top), per the executor raster.
int readTargetR(PointGraph& pg, uint32_t W, uint32_t H, float ndcX, float ndcY) {
  MTL::Texture* tex = pg.target();
  if (!tex || (uint32_t)tex->width() != W || (uint32_t)tex->height() != H) return -1;
  std::vector<uint8_t> px((size_t)W * H * 4, 0);
  tex->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  int x = (int)((ndcX * 0.5f + 0.5f) * (float)(W - 1) + 0.5f);
  int y = (int)((1.0f - (ndcY * 0.5f + 0.5f)) * (float)(H - 1) + 0.5f);
  x = x < 0 ? 0 : (x >= (int)W ? (int)W - 1 : x);
  y = y < 0 ? 0 : (y >= (int)H ? (int)H - 1 : y);
  return px[((size_t)y * W + x) * 4 + 0];
}

// Cook one graph through flat (whichPath 0) or resident (1) and return the RED at the two NDC probes.
bool cookGroupGraph(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* q, const Graph& g,
                    int whichPath, uint32_t W, uint32_t H, float ndcAx, float ndcAy, float ndcBx,
                    float ndcBy, int& rA, int& rB) {
  PointGraph pg(dev, lib, q, W, H);
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  if (whichPath == 0) {
    pg.cook(g, ctx, nullptr, /*terminal RenderTarget=*/4);
  } else {
    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    pg.cookResident(rg, ctx, nullptr, /*RenderTarget path=*/"4");
  }
  rA = readTargetR(pg, W, H, ndcAx, ndcAy);
  rB = readTargetR(pg, W, H, ndcBx, ndcBy);
  return rA >= 0 && rB >= 0;
}
}  // namespace

int runGroupSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 256, H = 256;  // SQUARE → viewAspect 1 → Stretch maps NDC 1:1

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-group] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();                     // Layer2d + Group + RenderTarget (real builtins)
  registerCmdOp("Group", cookGroupForTest);      // OVERRIDE Group with the test wrapper (honors the drop flag)
  registerTexOp("SolidImage", cookSolidImage);   // the test source op
  installGroupSpecs();

  const float quadScale = 0.3f;  // Layer2d Scale → quad object half-extent 0.3 (Stretch, square)
  const float tx = 0.5f;         // Group translate +x
  const float groupScale = 2.0f; // Group scale (tooth 2)

  // HOST-derived expected grouped center NDC (translate leg): project the quad origin through
  // (Layer2d S·R·T)·(group T)·camera — the SAME math the executor composes. At the default camera the
  // origin maps to (groupTx, 0) since d·tan=1. We compute it explicitly (NOT hardcoded) to honor the rule.
  const float aspect = 1.0f;
  const LayerCameraForward camFwd = defaultLayerCameraForward(aspect);
  auto projectedCenterX = [&](float groupTx) -> float {
    Mat4 childO2W = layer2dObjectToWorld(quadScale, quadScale, 0.0f, 0.0f, 0.0f, 0.0f);
    Mat4 grp = groupObjectToWorld(1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, groupTx, 0.0f, 0.0f);
    Mat4 o2w = mat4Mul(childO2W, grp);  // executor's finalO2W = childO2W · groupSRT
    Mat4 o2c = objectToClipSpace(o2w, camFwd.worldToCamera, camFwd.cameraToClipSpace);
    float ndc[3];
    mat4TransformPointDivW(o2c, 0.0f, 0.0f, 0.0f, ndc);  // quad CENTER (object origin)
    return ndc[0];
  };
  const float groupedCenterNdc = projectedCenterX(tx);  // ≈ +0.5

  g_groupDropPush = injectBug;  // ★the group-push DROP bug (the executor multiply is starved)

  bool allFaithful = true;
  const char* pathName[2] = {"flat", "resident"};
  for (int path = 0; path < 2; ++path) {
    // ── TOOTH 1: GROUP TRANSLATE. Probe A = grouped center (RED), probe B = un-grouped center NDC 0
    //    (BACKGROUND). -bug drops the push → quad stays at NDC 0 → A background, B red → BOTH flip → RED.
    {
      Graph g = buildGroupGraph(quadScale, tx, /*groupScale=*/1.0f, W, H);
      int rMoved = -1, rOrigin = -1;
      bool ok = cookGroupGraph(dev, lib, q, g, path, W, H, groupedCenterNdc, 0.0f, 0.0f, 0.0f,
                               rMoved, rOrigin);
      bool faithful = ok && (rMoved > 200) && (rOrigin < 40);  // quad MOVED to the grouped center
      allFaithful = allFaithful && faithful;
      std::printf("[selftest-group] %s T1 translate: movedNDC=%.3f movedR=%d(>200) originR=%d(<40) -> %s\n",
                  pathName[path], groupedCenterNdc, rMoved, rOrigin, faithful ? "faithful-ok" : "tripped");
    }
    // ── TOOTH 2: GROUP SCALE=2. Probe @ NDC 0.45: inside grouped (half 0.6), outside un-grouped (half
    //    0.3). Probe @ NDC 0.85: outside both (background) — the deep-exterior plateau. -bug → quad half
    //    0.3 → NDC 0.45 background → flip → RED. Proves the SCALE term (not just translate) reaches the executor.
    {
      Graph g = buildGroupGraph(quadScale, /*tx=*/0.0f, groupScale, W, H);
      int r45 = -1, r85 = -1;
      bool ok = cookGroupGraph(dev, lib, q, g, path, W, H, 0.45f, 0.0f, 0.85f, 0.0f, r45, r85);
      bool faithful = ok && (r45 > 200) && (r85 < 40);  // inside the scaled-up quad, outside far
      allFaithful = allFaithful && faithful;
      std::printf("[selftest-group] %s T2 scale=2: NDC0.45 R=%d(>200) NDC0.85 R=%d(<40) -> %s\n",
                  pathName[path], r45, r85, faithful ? "faithful-ok" : "tripped");
    }
  }

  g_groupDropPush = false;  // reset (process hygiene)
  setDynamicSpecs({});
  lib->release(); q->release(); dev->release(); pool->release();

  if (injectBug) {
    if (allFaithful) {
      std::printf("[selftest-group] FAIL: injectBug tripped no tooth (group push survived the drop)\n");
      return 1;
    }
    std::printf("[selftest-group] injectBug correctly RED (group push dropped → quad never moved/scaled "
                "→ translate + scale probes flipped on flat AND resident)\n");
    return 1;
  }
  std::printf("[selftest-group] %s\n", allFaithful ? "PASS" : "FAIL");
  return allFaithful ? 0 : 1;
}

}  // namespace sw
