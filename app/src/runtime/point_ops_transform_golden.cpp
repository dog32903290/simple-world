// Render-island transform-leaves golden — end-to-end proof that RotateAroundAxis / Shear / Transform
// (TiXL Lib/render/transform/*.cs, cooked by point_ops_{rotatearoundaxis,shear,transform}.cpp) push their
// transform onto context.ObjectToWorld and MOVE the child layer on BOTH the flat AND resident cook legs.
// NO new cook-core code: a HARNESS cut driving the already-landed S2 island (the SAME per-item group-stamp
// the S2b Group golden exercises — point_ops_group_golden.cpp is the direct template). Each op is a single-
// Command pass-through that stamps into RenderDrawItem.groupObjectToWorld; the executor right-multiplies it
// (point_ops_rendertarget.cpp Layer2d case: finalO2W = childO2W · groupObjectToWorld). Because all three
// write the SAME group slot, they COMPOSE with Group — so the goldens drive each op OVER a Group(translate)
// to make a pure-rotation / pure-shear visible (a centered symmetric quad is invariant under rotation about
// its own center; translating it first off-center makes the rotation/shear move it).
//
//     SolidRed(Texture2D) → Layer2d(Scale=0.3) → Group(Translate +0.5 x) → <OP> → RenderTarget(Texture2D)
//
// ── TiXL GROUND TRUTH + CLOSED-FORM EXPECTED (derived HOST-SIDE via the SAME mat4 path the VS reproduces) ──
//   The executor's per-item finalO2W = childO2W · groupObjectToWorld, where groupObjectToWorld accumulates
//   innermost-first: childO2W · groupT · <OP-matrix>. We project the quad ORIGIN (object 0,0,0) through
//   finalO2W · camera and probe the resulting NDC plateau (RED) plus the pre-OP location (BACKGROUND).
//
//   T1 RotateAroundAxis(Axis=(0,0,1),90°): CreateFromAxisAngle((0,0,1),90°)==CreateRotationZ(90°); a +90°
//     row-vector Z-rot maps (x,y)→(−y,x), so the Group center (+0.5,0)→(0,+0.5). probe (0,+0.5)=RED, probe
//     (+0.5,0)=BG. Host ref = layer2dObjectToWorld(rotZ=90) (== CreateRotationZ, INDEPENDENT of the op code).
//   T2 Shear(Translation.Y=1): m.M12=Y=1 couples input x→output y in v·M: (x,y)→(x,y+x), so (+0.5,0)→
//     (+0.5,+0.5). probe (+0.5,+0.5)=RED, probe (+0.5,0)=BG. (Trivial shear matrix built inline host-side.)
//   T3 Transform(Translation.x=+0.3): full-TRS default S/R + translate, stacks on Group(+0.5) → +0.8 total.
//     probe (+0.8,0)=RED; probe (+0.35,0)=BG — see the geometry note at the tooth (avoids the both-inside trap).
//
// ── injectBug ── a wrapper over EACH op runs the REAL cook then RESTORES groupObjectToWorld to the inner
//   Group's snapshot (drops ONLY this op's stamp) → the quad sits at the Group-only spot → every "moved"
//   probe reads BG → flip → RED, on flat AND resident (catches a resident path that lost the push). The bite
//   is the REAL executor multiply + REAL op stamp, not a flipped assertion.
// ── DISCIPLINE (Cut62-63) ── deep-interior projected-center plateaus only, never a quad edge; single-sample;
//   no depth (Layer2d depth-disabled); expected NDC DERIVED from the transform math.
//
// Zone: runtime leaf. SolidImage = TEST-ONLY harness fixture (the group_golden precedent); Layer2d / Group /
// RotateAroundAxis / Shear / Transform / RenderTarget are the REAL builtins under test (the OP under test is
// re-registered with a drop-flag wrapper).
#include "runtime/point_ops.h"

#include "runtime/field_camera.h"    // Mat4 / mat4Mul / groupObjectToWorld / layer2dObjectToWorld / objectToClipSpace / defaultLayerCameraForward / mat4TransformPointDivW / mat4Identity
#include "runtime/point_graph.h"     // PointGraph::cook/cookResident, registerBuiltinPointOps/registerCmdOp/registerTexOp, CmdCookCtx, TexCookCtx

#include <array>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"                // Graph/Node/NodeSpec/PortSpec/pinId/setDynamicSpecs/findSpec
#include "runtime/graph_bridge.h"         // libFromGraph
#include "runtime/render_command.h"       // RenderCommand / RenderDrawItem
#include "runtime/resident_eval_graph.h"  // buildEvalGraph
#include "runtime/tixl_point.h"           // EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
// The REAL op cooks under test (defined in the leaf .cpp files; declared here, their only consumer via the
// drop wrapper — kept out of the at-cap point_ops.h header).
RenderCommand cookRotateAroundAxis(CmdCookCtx& c);
RenderCommand cookShear(CmdCookCtx& c);
RenderCommand cookTransform(CmdCookCtx& c);
namespace {
// Test-only op flag: when set, the wrapper runs the REAL op cook then DROPS the stamp it just applied
// (restores groupObjectToWorld to what the INNER Group left = the pre-op location) → the executor's extra
// multiply for THIS op is starved → the quad sits at the Group-only spot. OFF in production. The inner
// Group's stamp is preserved by re-running cookGroup-equivalent? No — simpler: the wrapper captures the
// items BEFORE the op and, on drop, restores each item's group matrix from that snapshot.
bool g_dropOpStamp = false;

// Build a drop wrapper for an op `realCook`: cook the input snapshot first (the items the op RECEIVES carry
// the inner Group's stamp via c.inputCommand), then run realCook; on drop, copy the input items' group
// matrices back over the output (undo only THIS op's contribution, keep the inner Group's).
template <RenderCommand (*RealCook)(CmdCookCtx&)>
RenderCommand cookOpDropWrapper(CmdCookCtx& c) {
  // Snapshot the incoming (inner-Group-stamped) group matrices, keyed by position (the op preserves order
  // and count — all three ops only re-stamp, never add/drop items).
  std::vector<std::array<float, 16>> before;
  std::vector<char> beforeHas;
  if (c.inputCommand) {
    before.reserve(c.inputCommand->items.size());
    beforeHas.reserve(c.inputCommand->items.size());
    for (const RenderDrawItem& it : c.inputCommand->items) {
      std::array<float, 16> g{};
      for (int i = 0; i < 16; ++i) g[i] = it.groupObjectToWorld[i];
      before.push_back(g);
      beforeHas.push_back(it.hasGroup ? 1 : 0);
    }
  }
  RenderCommand rc = RealCook(c);
  if (g_dropOpStamp && rc.items.size() == before.size()) {
    for (size_t k = 0; k < rc.items.size(); ++k) {
      for (int i = 0; i < 16; ++i) rc.items[k].groupObjectToWorld[i] = before[k][i];
      rc.items[k].hasGroup = beforeHas[k] != 0;
    }
  }
  return rc;
}

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

// Install dynamic specs for the harness graph. Layer2d / Group / the transform ops / RenderTarget are real
// builtins; SolidImage is the test source. PortSpec positional:
//   {id, name, dataType, isInput, def, minV, maxV, widget, labels, pinless, vecArity, multiInput, strDef}
void installSpecs() {
  std::map<std::string, NodeSpec> dyn;
  dyn["SolidImage"] = atomicSpec("SolidImage", {{"out", "out", "Texture2D", false}});
  dyn["Layer2d"] = atomicSpec(
      "Layer2d",
      {{"Image", "Image", "Texture2D", true},
       {"out", "out", "Command", false},
       {"Scale", "Scale", "Float", true, 1.0f, 0.0f, 10.0f, Widget::Slider, {}, true},
       {"ScaleMode", "ScaleMode", "Float", true, 0.0f, 0.0f, 5.0f, Widget::Enum, {}, true},
       {"BlendMode", "BlendMode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum, {}, true}});
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
  dyn["RotateAroundAxis"] = atomicSpec(
      "RotateAroundAxis",
      {{"command", "command", "Command", true},
       {"out", "out", "Command", false},
       {"Angle", "Angle", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Slider, {}, true},
       {"Axis.x", "Axis", "Float", true, 1.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 3},
       {"Axis.y", "Axis.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
       {"Axis.z", "Axis.z", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1}});
  dyn["Shear"] = atomicSpec(
      "Shear",
      {{"command", "command", "Command", true},
       {"out", "out", "Command", false},
       {"Translation.x", "Translation", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
       {"Translation.y", "Translation.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
       {"Translation.z", "Translation.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1}});
  dyn["Transform"] = atomicSpec(
      "Transform",
      {{"command", "command", "Command", true},
       {"out", "out", "Command", false},
       {"Translation.x", "Translation", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
       {"Translation.y", "Translation.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
       {"Translation.z", "Translation.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
       {"Rotation.x", "Rotation", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 3},
       {"Rotation.y", "Rotation.y", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
       {"Rotation.z", "Rotation.z", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
       {"Scale.x", "Scale", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
       {"Scale.y", "Scale.y", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
       {"Scale.z", "Scale.z", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
       {"UniformScale", "UniformScale", "Float", true, 1.0f, 0.0f, 10.0f, Widget::Slider, {}, true},
       {"Pivot.x", "Pivot", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
       {"Pivot.y", "Pivot.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
       {"Pivot.z", "Pivot.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1}});
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

// Build the harness graph: SolidRed → Layer2d(Scale=0.3) → Group(Translate +0.5 x) → <opType>(params) →
// RenderTarget. Node ids: 1=SolidRed 2=Layer2d 3=Group 4=Op 5=RenderTarget (terminal).
Graph buildGraph(const char* opType, const std::map<std::string, float>& opParams, float groupTx,
                 uint32_t W, uint32_t H) {
  Graph g;
  Node sa; sa.id = 1; sa.type = "SolidImage"; g.nodes.push_back(sa);
  Node l; l.id = 2; l.type = "Layer2d";
  l.params["Scale"] = 0.3f; l.params["ScaleMode"] = 4.0f; l.params["BlendMode"] = 0.0f;  // Stretch/Normal
  g.nodes.push_back(l);
  Node grp; grp.id = 3; grp.type = "Group";
  grp.params["Translation.x"] = groupTx; grp.params["UniformScale"] = 1.0f;
  g.nodes.push_back(grp);
  Node op; op.id = 4; op.type = opType; op.params = opParams; g.nodes.push_back(op);
  Node rt; rt.id = 5; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f; rt.params["CustomW"] = (float)W; rt.params["CustomH"] = (float)H;
  g.nodes.push_back(rt);

  const int solidOut = outPortIdx("SolidImage");
  const int layerTexIn = inPortIdx("Layer2d", "Texture2D");
  const int layerOut = outPortIdx("Layer2d");
  const int grpCmdIn = inPortIdx("Group", "Command");
  const int grpOut = outPortIdx("Group");
  const int opCmdIn = inPortIdx(opType, "Command");
  const int opOut = outPortIdx(opType);
  const int rtCmdIn = inPortIdx("RenderTarget", "Command");

  g.connections.push_back({101, pinId(1, solidOut), pinId(2, layerTexIn)});  // SolidRed → Layer2d
  g.connections.push_back({102, pinId(2, layerOut), pinId(3, grpCmdIn)});    // Layer2d → Group
  g.connections.push_back({103, pinId(3, grpOut), pinId(4, opCmdIn)});       // Group → Op
  g.connections.push_back({104, pinId(4, opOut), pinId(5, rtCmdIn)});        // Op → RenderTarget
  return g;
}

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

bool cookAndProbe(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* q, const Graph& g, int path,
                  uint32_t W, uint32_t H, float ax, float ay, float bx, float by, int& rA, int& rB) {
  PointGraph pg(dev, lib, q, W, H);
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  if (path == 0) {
    pg.cook(g, ctx, nullptr, /*terminal=*/5);
  } else {
    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    pg.cookResident(rg, ctx, nullptr, /*RenderTarget path=*/"5");
  }
  rA = readTargetR(pg, W, H, ax, ay);
  rB = readTargetR(pg, W, H, bx, by);
  return rA >= 0 && rB >= 0;
}

// Project the quad ORIGIN through finalO2W = (Layer2d S·R·T) · opMatrix · camera and return its NDC (x,y).
// `opMatrix` is the COMPOSED group matrix the executor will apply (groupT · <op>), built host-side from
// INDEPENDENT reference matrices (not the op's own static code).
void projectCenter(const Mat4& opMatrix, float& ndcX, float& ndcY) {
  const LayerCameraForward camFwd = defaultLayerCameraForward(1.0f);
  Mat4 childO2W = layer2dObjectToWorld(0.3f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f);
  Mat4 o2w = mat4Mul(childO2W, opMatrix);  // executor's finalO2W = childO2W · groupObjectToWorld
  Mat4 o2c = objectToClipSpace(o2w, camFwd.worldToCamera, camFwd.cameraToClipSpace);
  float ndc[3];
  mat4TransformPointDivW(o2c, 0.0f, 0.0f, 0.0f, ndc);
  ndcX = ndc[0]; ndcY = ndc[1];
}

Mat4 translate(float tx, float ty, float tz) {
  Mat4 T = mat4Identity(); T.m[12] = tx; T.m[13] = ty; T.m[14] = tz; return T;
}
}  // namespace

int runTransformOpsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 256, H = 256;  // SQUARE → viewAspect 1 → Stretch maps NDC 1:1

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-transformops] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();  // Layer2d/Group/RotateAroundAxis/Shear/Transform/RenderTarget (real builtins)
  registerTexOp("SolidImage", cookSolidImage);
  // OVERRIDE each op under test with a drop wrapper (honors g_dropOpStamp; production cooks stay flag-free).
  registerCmdOp("RotateAroundAxis", cookOpDropWrapper<cookRotateAroundAxis>);
  registerCmdOp("Shear", cookOpDropWrapper<cookShear>);
  registerCmdOp("Transform", cookOpDropWrapper<cookTransform>);
  installSpecs();

  const float groupTx = 0.5f;  // Group translate +x (the inner stamp the op composes over)

  g_dropOpStamp = injectBug;
  bool allFaithful = true;
  const char* pathName[2] = {"flat", "resident"};

  for (int path = 0; path < 2; ++path) {
    // ── TOOTH 1: RotateAroundAxis(Z, 90°) over Group(+0.5 x). center (+0.5,0) → (0,+0.5). ──
    {
      // opMatrix = groupT(+0.5) · rotZ(90). rotZ via layer2dObjectToWorld(1,1,90,0,0,0) (== CreateRotationZ).
      Mat4 rotZ = layer2dObjectToWorld(1.0f, 1.0f, 90.0f, 0.0f, 0.0f, 0.0f);
      Mat4 opM = mat4Mul(translate(groupTx, 0.0f, 0.0f), rotZ);
      float mx, my, bx, by;
      projectCenter(opM, mx, my);                                  // moved (rotated) center ≈ (0,+0.5)
      projectCenter(translate(groupTx, 0.0f, 0.0f), bx, by);       // pre-op (Group-only) ≈ (+0.5,0)
      std::map<std::string, float> p{{"Angle", 90.0f}, {"Axis.x", 0.0f}, {"Axis.y", 0.0f}, {"Axis.z", 1.0f}};
      Graph g = buildGraph("RotateAroundAxis", p, groupTx, W, H);
      int rMoved = -1, rBefore = -1;
      bool ok = cookAndProbe(dev, lib, q, g, path, W, H, mx, my, bx, by, rMoved, rBefore);
      bool faithful = ok && (rMoved > 200) && (rBefore < 40);
      allFaithful = allFaithful && faithful;
      std::printf("[selftest-transformops] %s T1 RotateAroundAxis Z90: moved(%.2f,%.2f)R=%d(>200) "
                  "before(%.2f,%.2f)R=%d(<40) -> %s\n", pathName[path], mx, my, rMoved, bx, by, rBefore,
                  faithful ? "faithful-ok" : "tripped");
    }
    // ── TOOTH 2: Shear(Translation.y=1) over Group(+0.5 x). M12=Y=1 → (x,y)→(x,y+x): (+0.5,0)→(+0.5,+0.5). ──
    {
      Mat4 shear = mat4Identity(); shear.m[1] = 1.0f;  // M12 = Translation.Y = 1 (the shear matrix)
      Mat4 opM = mat4Mul(translate(groupTx, 0.0f, 0.0f), shear);
      float mx, my, bx, by;
      projectCenter(opM, mx, my);                                  // sheared center ≈ (+0.5,+0.5)
      projectCenter(translate(groupTx, 0.0f, 0.0f), bx, by);       // pre-op ≈ (+0.5,0)
      std::map<std::string, float> p{{"Translation.x", 0.0f}, {"Translation.y", 1.0f}, {"Translation.z", 0.0f}};
      Graph g = buildGraph("Shear", p, groupTx, W, H);
      int rMoved = -1, rBefore = -1;
      bool ok = cookAndProbe(dev, lib, q, g, path, W, H, mx, my, bx, by, rMoved, rBefore);
      bool faithful = ok && (rMoved > 200) && (rBefore < 40);
      allFaithful = allFaithful && faithful;
      std::printf("[selftest-transformops] %s T2 Shear Y=1: moved(%.2f,%.2f)R=%d(>200) "
                  "before(%.2f,%.2f)R=%d(<40) -> %s\n", pathName[path], mx, my, rMoved, bx, by, rBefore,
                  faithful ? "faithful-ok" : "tripped");
    }
    // ── TOOTH 3: Transform(Translation.x=+0.3) over Group(+0.5 x). total +0.8 x. ──
    {
      Mat4 opM = mat4Mul(translate(groupTx, 0.0f, 0.0f), translate(0.3f, 0.0f, 0.0f));  // +0.8 total
      float mx, my, bx, by;
      projectCenter(opM, mx, my);                                  // ≈ (+0.8,0)
      projectCenter(translate(groupTx, 0.0f, 0.0f), bx, by);       // pre-op ≈ (+0.5,0)
      std::map<std::string, float> p{{"Translation.x", 0.3f}, {"Translation.y", 0.0f},
                                     {"Translation.z", 0.0f}, {"UniformScale", 1.0f}};
      (void)bx; (void)by;
      // DISCRIMINATING probe choice: the Group-only quad spans NDC x ∈ [+0.2,+0.8] (center +0.5, half 0.3);
      // the Transform-moved quad spans [+0.5,+1.1] (center +0.8). The window that is INSIDE Group-only but
      // OUTSIDE moved is [+0.2,+0.5). Probe its MIDDLE (+0.35,0) — a deep plateau in both directions, never
      // a quad edge (the +0.2 / +0.5 boundaries are avoided). probe A = the moved center (+0.8,0) = RED;
      // probe B = (+0.35,0) = BACKGROUND once Transform's translate reached the executor (the quad VACATED
      // it). (Using +0.5 would be a bad probe: inside BOTH quads — no discrimination. This geometry choice
      // is load-bearing.)
      Graph g = buildGraph("Transform", p, groupTx, W, H);
      int rMoved = -1, rVacated = -1;
      bool ok = cookAndProbe(dev, lib, q, g, path, W, H, mx, my, /*B=*/0.35f, 0.0f, rMoved, rVacated);
      bool faithful = ok && (rMoved > 200) && (rVacated < 40);  // moved spot red, vacated +0.35 spot background
      allFaithful = allFaithful && faithful;
      std::printf("[selftest-transformops] %s T3 Transform +0.3x: moved(%.2f,%.2f)R=%d(>200) "
                  "vacated(0.35,0.00)R=%d(<40) -> %s\n", pathName[path], mx, my, rMoved, rVacated,
                  faithful ? "faithful-ok" : "tripped");
    }
  }

  g_dropOpStamp = false;
  setDynamicSpecs({});
  lib->release(); q->release(); dev->release(); pool->release();

  if (injectBug) {
    if (allFaithful) {
      std::printf("[selftest-transformops] FAIL: injectBug tripped no tooth (op stamp survived the drop)\n");
      return 1;
    }
    std::printf("[selftest-transformops] injectBug correctly RED (op stamp dropped → quad never "
                "rotated/sheared/translated → probes flipped on flat AND resident)\n");
    return 1;
  }
  std::printf("[selftest-transformops] %s\n", allFaithful ? "PASS" : "FAIL");
  return allFaithful ? 0 : 1;
}

}  // namespace sw
