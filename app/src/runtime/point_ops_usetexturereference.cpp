// UseTextureReference tex op — the TiXL "wireless" texture link consumer. Owns the TEXREF stash both
// tex walkers publish into, + the --selftest-usetexturereference golden (flat AND resident legs).
//
// TiXL authority: external/tixl/Operators/Lib/image/use/UseTextureReference.cs
//   The op OWNS a RenderTargetReference (:33) and returns it on Reference plus the reference's
//   ColorTexture/DepthTexture on the Texture/DepthTexture outputs (:23-31, DirtyFlagTrigger.Animated
//   → re-served every frame). The reference is FILLED elsewhere: a RenderTarget whose TextureReference
//   input is wired to it writes reference.ColorTexture = ColorTexture after rendering
//   (RenderTarget.cs:143-148; RenderTargetReference = {Color,Depth,Normal}Texture,
//   Core/DataTypes/RenderTargetReference.cs:3-8). Net effect: RenderTarget's texture is available
//   ANYWHERE in the graph without a texture wire.
//
// ★MECHANISM (sw): the reference object becomes a leaf-owned STASH keyed by the UseTextureReference
//   node's cook key (flat "#<id>" / resident path — the wire target's identity). Both tex walkers
//   (point_graph_tex_cook.cpp / point_graph_resident_tex_cook.cpp), after cooking any node with a
//   WIRED "TexRef" input port, publish that node's output texture under the wire's SOURCE key. This
//   op's cook then looks up its OWN key and routes the stashed texture out via TexCookCtx::
//   redirectTexture (NO copy — the walker returns the referenced texture, exactly TiXL's
//   reference-object semantics). Stash entries RETAIN their texture (release on overwrite) so a
//   republish after an ensureTex realloc can never leave a dangling pointer (worst case: one stale
//   frame — same observable as TiXL's reference holding last frame's texture object).
//
// NAMED FORKS:
//   • fork-utr-depth-normal-dropped: DepthTexture/NormalTexture outputs are not shipped — sw's
//     RenderTarget executor exposes no depth/normal attachment surface yet. Texture (color) is the
//     load-bearing output; add the siblings when the executor grows depth exposure.
//   • fork-utr-empty-black: before the referenced RenderTarget first cooks (or with nothing wired),
//     TiXL serves a null texture (consumers draw nothing). sw clears its own output to transparent
//     black and serves that — same visible result, never a garbage texture.
//   • Cook ORDER within a frame is wire-pull order on both sides: if the consumer pulls this op
//     BEFORE the referenced RenderTarget cooked this frame, it serves the previous cook's texture
//     (TiXL identical — the reference holds whatever the RT last wrote).
#include "runtime/point_ops.h"

#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"                     // Graph / Node / NodeSpec / PortSpec / pinId / findSpec
#include "runtime/graph_bridge.h"              // libFromGraph (flat Graph → SymbolLibrary)
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp registrar (spec+cook+selftest sinks)
#include "runtime/point_graph.h"               // TexCookCtx / PointGraph / registerBuiltinPointOps
#include "runtime/resident_eval_graph.h"       // buildEvalGraph (production resident path)
#include "runtime/tixl_point.h"                // EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

namespace {
// The stash: UseTextureReference node key → the last texture a TexRef-wired producer published.
// Entries RETAIN (header MECHANISM); process-lifetime, like the PSO/asset caches.
std::map<std::string, MTL::Texture*>& texRefStash() {
  static std::map<std::string, MTL::Texture*> m;
  return m;
}
}  // namespace

// Publish (called by BOTH tex walkers after cooking a node with a wired "TexRef" input port).
void textureReferencePublish(const std::string& refKey, MTL::Texture* tex) {
  if (refKey.empty() || !tex) return;
  auto& m = texRefStash();
  auto it = m.find(refKey);
  if (it != m.end()) {
    if (it->second == tex) return;
    if (it->second) it->second->release();  // release the previous retained entry
    it->second = tex;
  } else {
    m[refKey] = tex;
  }
  tex->retain();  // keep the published texture alive across ensureTex reallocs (one-stale-frame max)
}

// Test-only walker flag (the publish tooth): true → both walkers SKIP the publish, so the golden's
// referenced-texture probe reads the empty-black fallback instead of the RenderTarget's color → RED.
bool& useTextureReferenceBugSkipPublish() {
  static bool v = false;
  return v;
}

int runUseTextureReferenceSelfTest(bool injectBug);  // defined below (external linkage for the registrar)

namespace {

// UseTextureReference cook: serve the stashed referenced texture (redirect, no copy); nothing
// published yet → clear own output to transparent black (fork-utr-empty-black).
void cookUseTextureReference(TexCookCtx& c) {
  auto& m = texRefStash();
  auto it = m.find(c.cookKey);
  if (it != m.end() && it->second) {
    c.redirectTexture = it->second;  // the walker returns the referenced RenderTarget texture
    return;
  }
  if (!c.output || !c.queue) return;
  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(c.output);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 0.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
}

NodeSpec useTextureReferenceSpec() {
  NodeSpec s;
  s.type = "UseTextureReference";
  s.title = "Use Texture Reference";
  // TiXL outputs (UseTextureReference.cs:6-13): Reference (the RenderTargetReference — here the
  // TexRef marker wire into RenderTarget.TextureReference) + Texture (the referenced color).
  // DepthTexture dropped (fork-utr-depth-normal-dropped).
  PortSpec ref; ref.id = "Reference"; ref.name = "Reference"; ref.dataType = "TexRef"; ref.isInput = false;
  PortSpec tex; tex.id = "Texture"; tex.name = "Texture"; tex.dataType = "Texture2D"; tex.isInput = false;
  s.ports = {ref, tex};
  s.category = "image.use";
  return s;
}

// ───────────────────────────────────────── GOLDEN ─────────────────────────────────────────
// --selftest-usetexturereference (BOTH legs). Graph:
//   1=UseTextureReference ; 2=RenderTarget_A (ClearColor=RED, 64×64, no command → clear-only) with
//   TextureReference ← 1.Reference ; 3=Layer2d_A (Image ← 2.out, tiny scale 0.05 → corner dot) ;
//   4=Layer2d_B (Image ← 1.Texture, full-frame) ; 5=Execute [wire0=3, wire1=4] ; 6=RenderTarget_MAIN
//   (256×256 terminal).
//   Cook order inside one frame: Execute cooks Layer2d_A FIRST (pulls RT_A → RT_A cooks → the walker
//   PUBLISHES its RED texture under node 1's key), then Layer2d_B (pulls UseTextureReference → the
//   stash hit REDIRECTS RT_A's texture) → the full-frame layer paints the referenced RED.
//   ASSERT: the probe sits OFF-CENTER at NDC (0.5,0.5) — covered ONLY by full-frame Layer_B; Layer_A's
//   tiny pull-quad shrinks around the CENTER and cannot reach it (a center probe would go hollow-green
//   off Layer_A's own RED). RED there can only be the wireless link; severed → transparent fallback.
// -bug: useTextureReferenceBugSkipPublish() → walkers skip the publish → UseTextureReference serves
//   the empty-black fallback → Layer2d_B paints transparent black → center NOT red → RED (both legs;
//   the tooth corrupts the REAL publish seam, not the expected value).
const ImageFilterOp g_useTextureReferenceOp(useTextureReferenceSpec(), "UseTextureReference",
                                            cookUseTextureReference, "usetexturereference",
                                            runUseTextureReferenceSelfTest);

// Layer2d has no production NodeSpec row (its goldens install one dynamically — the layercompose
// harness shape); UseTextureReference / RenderTarget / Execute use their PRODUCTION rows.
void installUtrSpecs() {
  std::map<std::string, NodeSpec> dyn;
  NodeSpec l; l.type = "Layer2d"; l.title = "Layer2d";
  l.ports = {{"Image", "Image", "Texture2D", true},
             {"out", "out", "Command", false},
             {"Scale", "Scale", "Float", true, 1.0f, 0.0f, 10.0f, Widget::Slider, {}, true},
             {"ScaleMode", "ScaleMode", "Float", true, 0.0f, 0.0f, 5.0f, Widget::Enum, {}, true},
             {"BlendMode", "BlendMode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum, {}, true}};
  dyn["Layer2d"] = l;
  setDynamicSpecs(std::move(dyn));
}

int inPortUtr(const char* type, const char* id) {
  const NodeSpec* s = findSpec(type);
  if (!s) return -1;
  for (size_t i = 0; i < s->ports.size(); ++i)
    if (s->ports[i].isInput && s->ports[i].id == id) return (int)i;
  return -1;
}
int outPortUtr(const char* type, const char* id) {
  const NodeSpec* s = findSpec(type);
  if (!s) return -1;
  for (size_t i = 0; i < s->ports.size(); ++i)
    if (!s->ports[i].isInput && s->ports[i].id == id) return (int)i;
  return -1;
}

Graph buildUtrGraph(uint32_t W, uint32_t H) {
  Graph g;
  Node utr; utr.id = 1; utr.type = "UseTextureReference"; g.nodes.push_back(utr);
  Node rta; rta.id = 2; rta.type = "RenderTarget";
  rta.params["Resolution"] = 4.0f; rta.params["CustomW"] = 64.0f; rta.params["CustomH"] = 64.0f;
  rta.params["ClearColor.x"] = 1.0f; rta.params["ClearColor.y"] = 0.0f;
  rta.params["ClearColor.z"] = 0.0f; rta.params["ClearColor.w"] = 1.0f;  // solid RED
  g.nodes.push_back(rta);
  auto mkLayer = [&](int id, float scale) {
    Node l; l.id = id; l.type = "Layer2d";
    l.params["Scale"] = scale;
    l.params["ScaleMode"] = 3.0f;   // Stretch
    l.params["BlendMode"] = 0.0f;   // Normal
    return l;
  };
  g.nodes.push_back(mkLayer(3, 0.05f));  // Layer2d_A: tiny (forces RT_A to cook + publish)
  g.nodes.push_back(mkLayer(4, 1.0f));   // Layer2d_B: full-frame (paints the referenced texture)
  Node ex; ex.id = 5; ex.type = "Execute"; ex.params["IsEnabled"] = 1.0f; g.nodes.push_back(ex);
  Node rt; rt.id = 6; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f; rt.params["CustomW"] = (float)W; rt.params["CustomH"] = (float)H;
  g.nodes.push_back(rt);

  const int utrRefOut = outPortUtr("UseTextureReference", "Reference");
  const int utrTexOut = outPortUtr("UseTextureReference", "Texture");
  const int rtTexRefIn = inPortUtr("RenderTarget", "TextureReference");
  const int rtOut = outPortUtr("RenderTarget", "out");
  const int layerImgIn = inPortUtr("Layer2d", "Image");
  const int layerOut = outPortUtr("Layer2d", "out");
  const int exCmdIn = inPortUtr("Execute", "Command");
  const int exOut = outPortUtr("Execute", "out");
  const int rtCmdIn = inPortUtr("RenderTarget", "command");

  g.connections.push_back({101, pinId(1, utrRefOut), pinId(2, rtTexRefIn)});  // the wireless link
  g.connections.push_back({102, pinId(2, rtOut), pinId(3, layerImgIn)});      // RT_A → Layer_A (pull)
  g.connections.push_back({103, pinId(1, utrTexOut), pinId(4, layerImgIn)});  // UTR.Texture → Layer_B
  g.connections.push_back({104, pinId(3, layerOut), pinId(5, exCmdIn)});      // wire0 = Layer_A (first!)
  g.connections.push_back({105, pinId(4, layerOut), pinId(5, exCmdIn)});      // wire1 = Layer_B
  g.connections.push_back({106, pinId(5, exOut), pinId(6, rtCmdIn)});         // Execute → MAIN
  return g;
}

}  // namespace

int runUseTextureReferenceSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 256, H = 256;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-usetexturereference] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();  // Layer2d / Execute / RenderTarget cooks (UseTextureReference self-registers)
  installUtrSpecs();          // Layer2d dynamic spec (no production row — the layercompose posture)

  useTextureReferenceBugSkipPublish() = injectBug;  // ★the publish tooth (REAL walker seam)

  bool allFaithful = true;
  const char* pathName[2] = {"flat", "resident"};
  for (int path = 0; path < 2; ++path) {
    Graph g = buildUtrGraph(W, H);
    PointGraph pg(dev, lib, q, W, H);
    EvaluationContext ctx{};
    ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    if (path == 0) {
      pg.cook(g, ctx, nullptr, /*terminal MAIN=*/6);
    } else {
      SymbolLibrary slib = libFromGraph(g);
      ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
      pg.cookResident(rg, ctx, nullptr, /*MAIN path=*/"6");
    }
    int r = 0, gg = 0, b = 0;
    MTL::Texture* tex = pg.target();
    bool sized = tex && (uint32_t)tex->width() == W && (uint32_t)tex->height() == H;
    if (sized) {
      std::vector<uint8_t> px((size_t)W * H * 4, 0);
      tex->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
      // ★Probe OFF-CENTER at NDC (0.5, 0.5): covered ONLY by the full-frame Layer_B (the referenced
      // texture). Layer_A's tiny pull-quad shrinks AROUND the center (NDC [-0.05,0.05]) and can never
      // reach this pixel — so a RED read can only come from the wireless link (no hollow green).
      const int pxX = (int)((0.5f * 0.5f + 0.5f) * (float)(W - 1) + 0.5f);
      const int pxY = (int)((1.0f - (0.5f * 0.5f + 0.5f)) * (float)(H - 1) + 0.5f);
      size_t i = ((size_t)pxY * W + pxX) * 4;
      r = px[i]; gg = px[i + 1]; b = px[i + 2];
    }
    const bool probeRed = r > 200 && gg < 40 && b < 40;
    allFaithful = allFaithful && sized && probeRed;
    std::printf("[selftest-usetexturereference] %s: probe@NDC(0.5,0.5)=(%d,%d,%d) want RED -> %s\n",
                pathName[path], r, gg, b, (sized && probeRed) ? "faithful-ok" : "tripped");
  }

  useTextureReferenceBugSkipPublish() = false;  // reset (process hygiene)
  setDynamicSpecs({});                          // drop the injected Layer2d spec
  // Drop this run's stash entries so a later selftest in the same process starts clean.
  for (auto& kv : texRefStash())
    if (kv.second) kv.second->release();
  texRefStash().clear();
  lib->release(); q->release(); dev->release(); pool->release();

  if (injectBug) {
    if (allFaithful) {
      std::printf("[selftest-usetexturereference] injectBug did not trip (publish skip changed no "
                  "pixel)\n");
      return 0;  // dead tooth → exit 0 so --bite's NO-BITE list catches it (GOLDEN_STANDARD P1)
    }
    std::printf("[selftest-usetexturereference] injectBug correctly RED (walkers skipped the TEXREF "
                "publish → the wireless link served black, both legs)\n");
    return 1;
  }
  std::printf("[selftest-usetexturereference] %s\n", allFaithful ? "PASS" : "FAIL");
  return allFaithful ? 0 : 1;
}

}  // namespace sw
