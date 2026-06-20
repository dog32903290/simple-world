// DrawMeshUnlit command op + the FIRST-3D-mesh goldens — TiXL Operators/Lib/mesh/draw/DrawMeshUnlit
// (.t3 → Lib:shaders/3d/mesh/mesh-DrawUnlit.hlsl). Mesh in → Command out (DrawKind::Mesh): a depth-
// tested, genuinely-UNLIT triangle mesh. The op is a pure data-stamper (zero render code, like every
// other cmd op) — it borrows the cooked SwVertex+SwTriIndex buffers from CmdCookCtx and emits a 1-item
// RenderCommand; the executor (cookRenderTarget) attaches the depth buffer + rasterizes it.
//
// BACKWARD-TRACE (DrawMeshUnlit.t3, confirmed): VertexShader c79222cf + PixelShader f9014b64 both →
// mesh-DrawUnlit.hlsl vsMain/psMain. Draw vertexCount = GetSRVProperties(FaceIndices).Count ×
// MultiplyInt(3) = faceCount×3 (child 0e36b565 B=3) → drawPrimitives(Triangle, 0, faceCount*3). Color
// (5100a9db, .t3 default (1,1,1,1)) → BlendColors(Mode=Multiply, GetForegroundColor) → Params cbuffer;
// at the default white foreground this is Color verbatim (v1 fork: foreground=white, headless has no UI
// theme). psMain non-cubemap default branch (no Texture → albedo=white, UseVertexColor=false →
// vertexColor=1, UseCubeMap=false, BlurLevel=0, AlphaCutOff=0) = albedo·Color·vertexColor = Color.
//
// FORKS (named): in-code 1×1 white t2 folded into the shader (DrawMeshUnlit.t3 LoadImage white.png
// fallback → albedo=1, no decode/bind needed for the default no-Texture case); GetForegroundColor =
// white (no UI theme in headless); BlendMode/FillMode/Cull/AlphaCutOff/BlurLevel/UseVertexColor/
// UseCubeMap/Texture deferred (defaults only); DrawMesh (the PBR variant, mesh-Draw.hlsl ComputePbr +
// PointLights + IBL) deferred like raymarch3D; winding CCW-front (TiXL FrontCounterClockwise=true).
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"      // EvaluationContext
#include "runtime/field_camera.h"      // lookAtRH/perspectiveFovRH/mat4* + objectToClipSpace (host project)
#include "runtime/graph.h"             // Graph/Node
#include "runtime/mesh_op_registry.h"  // meshInjectBug
#include "runtime/point_graph.h"       // CmdCookCtx/registerCmdOp/cookVecN + PointGraph
#include "runtime/render_command.h"    // RenderCommand / RenderDrawItem / DrawKind
#include "runtime/tixl_point.h"        // (EvaluationContext)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// DrawMeshUnlit: Mesh in → Command out. Borrows the cooked mesh buffers (CmdCookCtx mesh*) and emits a
// 1-item DrawKind::Mesh chain. Unwired Mesh → empty chain (no item; the executor draws nothing — defined
// no-op, faithful to TiXL evaluating an empty MeshBuffers). The executor composes ObjectToClipSpace
// (default camera or a stamped Camera op) + attaches depth; this op carries ONLY the data.
RenderCommand cookDrawMeshUnlit(CmdCookCtx& c) {
  RenderCommand rc;
  if (!c.meshVtx || !c.meshIdx || c.meshFaceCount == 0) return rc;  // no mesh wired → empty (no draw)

  RenderDrawItem it{};
  it.kind = DrawKind::Mesh;
  it.meshVtx = c.meshVtx;
  it.meshIdx = c.meshIdx;
  it.meshIndexCount = c.meshFaceCount;  // FACE count; the executor draws ×3 (SV_VertexID-driven)
  float colDef[4] = {1.0f, 1.0f, 1.0f, 1.0f};  // TiXL Color .t3 default (1,1,1,1)
  float col[4];
  cookVecN(c, "Color", colDef, 4, col);
  it.color[0] = col[0]; it.color[1] = col[1]; it.color[2] = col[2]; it.color[3] = col[3];
  it.applyTransform = true;  // production always projects; the golden's -bug drops it (mis-project tooth)
  // hasCamera stays false → executor uses defaultLayerCameraForward (a Camera op upstream stamps it).
  rc.items.push_back(it);
  return rc;
}

void registerDrawMeshUnlitOp() { registerCmdOp("DrawMeshUnlit", cookDrawMeshUnlit); }

// ───────────────────────────────── GOLDENS ─────────────────────────────────
namespace {
constexpr float kPi = 3.14159265358979323846f;

MTL::Texture* makeTarget(MTL::Device* dev, uint32_t W, uint32_t H) {
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  return dev->newTexture(td);
}
int ndcXToPx(float ndcX, uint32_t W) { return (int)((ndcX * 0.5f + 0.5f) * (float)(W - 1) + 0.5f); }
int ndcYToPx(float ndcY, uint32_t H) {
  return (int)((1.0f - (ndcY * 0.5f + 0.5f)) * (float)(H - 1) + 0.5f);
}
// Host-project a world point through the DEFAULT camera (the SAME math the executor's VS reproduces:
// objectToClipSpace(identity, defaultW2C, defaultC2C) then divide-by-w) → NDC. aspect = W/H.
void projectDefault(float aspect, float wx, float wy, float wz, float outNdc[3]) {
  LayerCameraForward cam = defaultLayerCameraForward(aspect);
  Mat4 o2c = objectToClipSpace(mat4Identity(), cam.worldToCamera, cam.cameraToClipSpace);
  mat4TransformPointDivW(o2c, wx, wy, wz, outNdc);
}
}  // namespace

// DrawMeshUnlit GOLDEN — the FIRST 3D mesh on screen. Two teeth, both deterministic + host-projected:
//
//   TOOTH A (mesh renders flat color, END-TO-END through the graph): QuadMesh (default Scale=1, span
//     object [-0.5,0.5]²) → DrawMeshUnlit Color=(1,0,0,1) → RenderTarget (default camera + depth). The
//     quad sits at world z=0 facing the camera; under the default camera d·tan(fov/2)=1 → NDC = world.xy,
//     so the quad's screen footprint is NDC [-0.5,0.5]². Assert: an interior pixel (NDC 0.2,0.2, deep
//     inside) = (255,0,0) [= Color, since albedo=white]; a far-corner (NDC 0.9,0.9, outside the quad) =
//     clear background. injectBug = drop the ObjectToClipSpace mul (raw object pos) → the quad mis-
//     projects → the interior pixel reads background → RED. (Also catches wrong winding: a back-face-
//     culled quad VANISHES → interior reads background → RED.)
//
//   TOOTH B (depth occlusion — THE depth tooth, two overlapping quads): a NEAR quad at world z=+0.5
//     (red) and a FAR quad at world z=-0.5 (green), both centered, overlapping at screen center. Drawn
//     NEAR-first then FAR — so WITHOUT depth the last draw (green) would win the overlap. WITH depth
//     (LessEqual + ZWrite), the near quad (smaller clip z) occludes the far one → overlap = RED. Assert
//     the center pixel = RED. injectBug = disable the depth-stencil state (drives both quads through the
//     depth-DISABLED path) → draw order decides → green (last) wins → overlap ≠ red → RED. This proves
//     depth does OCCLUSION, not merely that a depth buffer is attached.
int runDrawMeshUnlitSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 256, H = 256;
  const float aspect = 1.0f;  // square

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-drawmeshunlit] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();  // QuadMesh self-registers; DrawPoints/DrawMeshUnlit/RenderTarget here

  bool allFaithful = true;
  auto readRGB = [](const std::vector<uint8_t>& px, uint32_t W, int x, int y, int& r, int& g, int& b) {
    size_t i = ((size_t)y * W + x) * 4;
    r = px[i]; g = px[i + 1]; b = px[i + 2];
  };

  // ── TOOTH A: flat-color mesh through the FULL graph (QuadMesh → DrawMeshUnlit → RenderTarget) ──
  {
    PointGraph pg(dev, lib, q, W, H);
    Graph g;
    Node mesh; mesh.id = 1; mesh.type = "QuadMesh";
    // QuadMesh spans object [0, scale]² (pivot 0.5 → offset 0; verts go 0..columnStep·(cols-1)=Scale).
    // Center it at the world ORIGIN via Center=(-0.5,-0.5,0) so its center is ON the camera axis → NDC
    // (0,0) = screen center (deterministic, z-independent), and its footprint is NDC [-0.5,0.5]².
    mesh.params["Segments.x"] = 1.0f; mesh.params["Segments.y"] = 1.0f; mesh.params["Scale"] = 1.0f;
    mesh.params["Center.x"] = -0.5f; mesh.params["Center.y"] = -0.5f;
    g.nodes.push_back(mesh);
    Node draw; draw.id = 2; draw.type = "DrawMeshUnlit";
    draw.params["Color.x"] = 1.0f; draw.params["Color.y"] = 0.0f;
    draw.params["Color.z"] = 0.0f; draw.params["Color.w"] = 1.0f;  // RED
    g.nodes.push_back(draw);
    Node rt; rt.id = 3; rt.type = "RenderTarget";
    rt.params["Resolution"] = 4.0f;  // Custom
    rt.params["CustomW"] = (float)W; rt.params["CustomH"] = (float)H;
    g.nodes.push_back(rt);
    g.connections.push_back({101, pinId(1, 1), pinId(2, 0)});  // QuadMesh.Data → DrawMeshUnlit.Mesh
    g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});  // DrawMeshUnlit.out → RenderTarget.command

    meshInjectBug() = false;  // not a mesh-data bug — the tooth is the projection (applyTransform)
    EvaluationContext ctx{};
    ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    int term = pg.defaultDrawTarget(g);
    pg.cook(g, ctx, nullptr, term);
    MTL::Texture* tex = pg.target();
    std::vector<uint8_t> px((size_t)W * H * 4, 0);
    if (tex) tex->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

    // Host-project: centered quad spans world [-0.5,0.5]² at z=0 → NDC [-0.5,0.5]² (default cam: NDC=world.xy).
    float c0[3]; projectDefault(aspect, 0.0f, 0.0f, 0.0f, c0);          // quad center → NDC (0,0) = screen center
    float ie[3]; projectDefault(aspect, 0.2f, 0.2f, 0.0f, ie);          // interior world (0.2,0.2)→NDC 0.2 (inside)
    float fc[3]; projectDefault(aspect, 0.9f, 0.9f, 0.0f, fc);          // world (0.9,0.9) OUTSIDE the quad → clear
    int cr, cg, cb, ir, ig, ib, fr, fg, fb;
    readRGB(px, W, ndcXToPx(c0[0], W), ndcYToPx(c0[1], H), cr, cg, cb);
    readRGB(px, W, ndcXToPx(ie[0], W), ndcYToPx(ie[1], H), ir, ig, ib);
    readRGB(px, W, ndcXToPx(fc[0], W), ndcYToPx(fc[1], H), fr, fg, fb);

    // faithful: center + interior = pure RED (255,0,0); far-corner = clear (black background). This is the
    // END-TO-END wiring proof (QuadMesh → DrawMeshUnlit → RenderTarget + depth). The projection BITE lives
    // in the A-mul block below (the graph op always emits applyTransform=true → can't drop the mul here),
    // so this block only contributes to the faithful leg (its injectBug result is informational).
    bool a = (cr > 250 && cg < 5 && cb < 5) && (ir > 250 && ig < 5 && ib < 5) &&
             (fr < 30 && fg < 30 && fb < 30);
    if (!injectBug) allFaithful = allFaithful && a;
    std::printf("[selftest-drawmeshunlit] A flat-color(graph): centerNDC=(%.2f,%.2f) center=(%d,%d,%d) "
                "interior=(%d,%d,%d) farCorner=(%d,%d,%d) -> %s\n",
                c0[0], c0[1], cr, cg, cb, ir, ig, ib, fr, fg, fb, a ? "faithful-ok" : "tripped");
  }

  // ── TOOTH A bite (explicit drop-mul, drives the REAL executor with applyTransform=false) ──
  // The graph op always emits applyTransform=true, so the projection tooth is proven here by stamping a
  // mesh item with applyTransform toggled. ★The drop-mul must GENUINELY move the asserted pixel — at the
  // default camera a z=0 quad projects to NDC=world.xy (a near-no-op for the mul, the camera-golden trap).
  // So we place the quad NEAR (Center.z=+1): the perspective w-divide then magnifies its footprint. With
  // the mul (faithful) the centered quad [-0.5,0.5]² at z=+1 projects to NDC ±0.5·(d/(d-1))=±0.854; raw
  // object pos (bug) keeps NDC ±0.5. A probe at NDC 0.7 sits BETWEEN: inside the projected quad (RED) but
  // OUTSIDE the raw one (background) → the bite is real, not hollow.
  {
    PointGraph pg(dev, lib, q, W, H);
    Graph g;
    Node mesh; mesh.id = 1; mesh.type = "QuadMesh";
    mesh.params["Segments.x"] = 1.0f; mesh.params["Segments.y"] = 1.0f; mesh.params["Scale"] = 1.0f;
    mesh.params["Center.x"] = -0.5f; mesh.params["Center.y"] = -0.5f; mesh.params["Center.z"] = 1.0f;
    g.nodes.push_back(mesh);
    meshInjectBug() = false;
    EvaluationContext ctx{};
    ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    pg.cook(g, ctx, nullptr, 1);  // mesh terminal → cooks the pair (debugCookedMesh readable)
    const MTL::Buffer* vb = nullptr; const MTL::Buffer* ib = nullptr;
    uint32_t vc = 0, fc = 0;
    bool got = pg.debugCookedMesh(1, vb, vc, ib, fc);

    auto renderMesh = [&](bool applyXf) -> std::vector<uint8_t> {
      MTL::Texture* tex = makeTarget(dev, W, H);
      RenderDrawItem it{};
      it.kind = DrawKind::Mesh;
      it.meshVtx = vb; it.meshIdx = ib; it.meshIndexCount = fc;
      it.color[0] = 1.0f; it.color[1] = 0.0f; it.color[2] = 0.0f; it.color[3] = 1.0f;  // RED
      it.applyTransform = applyXf;
      RenderCommand rc; rc.items.push_back(it);
      TexCookCtx c; c.dev = dev; c.lib = lib; c.queue = q; c.nodeId = 1; c.command = &rc; c.output = tex;
      cookRenderTarget(c);
      std::vector<uint8_t> px((size_t)W * H * 4, 0);
      tex->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
      tex->release();
      return px;
    };
    // Host-project the quad's x-half corner (world (0.5,0,1)) through the default camera → projected NDC.
    float corner[3]; projectDefault(aspect, 0.5f, 0.0f, 1.0f, corner);  // expect ≈ 0.854 (w-magnified)
    const float kProbe = 0.7f;  // between the raw footprint (±0.5) and the projected one (±0.854)
    int ppx = ndcXToPx(kProbe, W), ppy = ndcYToPx(0.0f, H);
    std::vector<uint8_t> pf = renderMesh(true);   // faithful: projected → probe inside → RED
    int fr, fg, fb; readRGB(pf, W, ppx, ppy, fr, fg, fb);
    bool faithfulRed = got && fr > 250 && fg < 5 && fb < 5 && corner[0] > kProbe;  // probe truly inside
    if (!injectBug) {
      allFaithful = allFaithful && faithfulRed;
      std::printf("[selftest-drawmeshunlit] A-mul faithful: projCornerNDC=%.3f probe@%.2f(%d,%d)=(%d,%d,%d) -> %s\n",
                  corner[0], kProbe, ppx, ppy, fr, fg, fb, faithfulRed ? "faithful-ok" : "tripped");
    } else {
      std::vector<uint8_t> pb = renderMesh(false);  // bug: raw object pos (NDC ±0.5) → probe outside
      int br, bg, bb; readRGB(pb, W, ppx, ppy, br, bg, bb);
      // the bite: faithful probe = RED (inside projected quad); dropped-mul probe = background (raw quad
      // only reaches ±0.5, the probe at 0.7 falls outside) → NOT red.
      bool bites = faithfulRed && (br < 40 && bg < 40 && bb < 40);
      allFaithful = allFaithful && !bites;  // injectBug must trip → allFaithful false
      std::printf("[selftest-drawmeshunlit] A-mul bug: faithfulProbe=(%d,%d,%d) bugProbe=(%d,%d,%d) "
                  "bites=%d (raw quad ±0.5 misses the 0.7 probe)\n", fr, fg, fb, br, bg, bb, bites ? 1 : 0);
    }
  }

  // ── TOOTH B: depth occlusion (two overlapping quads, near RED occludes far GREEN) ──
  {
    // Build TWO QuadMeshes via the registered op (Center.z places them at world z=±0.5).
    auto buildQuad = [&](float cz, const MTL::Buffer*& vb, const MTL::Buffer*& ib, uint32_t& fc,
                         PointGraph& pg) {
      Graph g;
      Node mesh; mesh.id = 1; mesh.type = "QuadMesh";
      // Center at world (0,0,cz): the quad's center is ON the camera axis → projects to NDC (0,0) =
      // screen center for ANY z (so both quads overlap exactly at screen center, the assert pixel).
      mesh.params["Segments.x"] = 1.0f; mesh.params["Segments.y"] = 1.0f; mesh.params["Scale"] = 1.0f;
      mesh.params["Center.x"] = -0.5f; mesh.params["Center.y"] = -0.5f; mesh.params["Center.z"] = cz;
      g.nodes.push_back(mesh);
      meshInjectBug() = false;
      EvaluationContext ctx{};
      ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
      pg.cook(g, ctx, nullptr, 1);
      uint32_t vc = 0;
      pg.debugCookedMesh(1, vb, vc, ib, fc);
    };
    PointGraph pgN(dev, lib, q, W, H), pgF(dev, lib, q, W, H);
    const MTL::Buffer *nvb = nullptr, *nib = nullptr, *fvb = nullptr, *fib = nullptr;
    uint32_t nfc = 0, ffc = 0;
    buildQuad(+0.5f, nvb, nib, nfc, pgN);  // near (red)
    buildQuad(-0.5f, fvb, fib, ffc, pgF);  // far  (green)

    // Draw NEAR (red) FIRST, then FAR (green). WITHOUT depth occlusion the last draw (green) wins the
    // overlap; WITH depth (LessEqual+ZWrite) the near quad (smaller clip z) occludes → overlap stays RED.
    RenderDrawItem nearIt{};
    nearIt.kind = DrawKind::Mesh; nearIt.meshVtx = nvb; nearIt.meshIdx = nib; nearIt.meshIndexCount = nfc;
    nearIt.color[0] = 1.0f; nearIt.color[1] = 0.0f; nearIt.color[2] = 0.0f; nearIt.color[3] = 1.0f;  // RED
    nearIt.applyTransform = true;
    RenderDrawItem farIt{};
    farIt.kind = DrawKind::Mesh; farIt.meshVtx = fvb; farIt.meshIdx = fib; farIt.meshIndexCount = ffc;
    farIt.color[0] = 0.0f; farIt.color[1] = 1.0f; farIt.color[2] = 0.0f; farIt.color[3] = 1.0f;  // GREEN
    farIt.applyTransform = true;

    auto renderPair = [&](bool depthDisabled) -> std::vector<uint8_t> {
      meshDepthDisableForTest() = depthDisabled;  // CPU executor hook: true → mesh draws depth-OFF
      MTL::Texture* tex = makeTarget(dev, W, H);
      RenderCommand rc; rc.items.push_back(nearIt); rc.items.push_back(farIt);
      TexCookCtx c; c.dev = dev; c.lib = lib; c.queue = q; c.nodeId = 1; c.command = &rc; c.output = tex;
      cookRenderTarget(c);
      std::vector<uint8_t> px((size_t)W * H * 4, 0);
      tex->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
      tex->release();
      meshDepthDisableForTest() = false;  // restore production state
      return px;
    };

    // The overlap pixel = screen center (both quads centered → both cover NDC≈0).
    std::vector<uint8_t> pOn = renderPair(false);   // depth ON: near(red) occludes far(green)
    int cr, cg, cb; readRGB(pOn, W, W / 2, H / 2, cr, cg, cb);
    bool depthOccludes = (cr > 200 && cg < 60);  // RED dominates (near red wins over far green)

    bool b;
    if (!injectBug) {
      b = depthOccludes;
      std::printf("[selftest-drawmeshunlit] B depth-occlusion: center=(%d,%d,%d) nearRedWins=%d -> %s\n",
                  cr, cg, cb, depthOccludes ? 1 : 0, b ? "faithful-ok" : "tripped");
    } else {
      // ★Tooth-B bite: disable the depth-stencil state. With no occlusion, the LAST-drawn far(green)
      // wins the overlap → center turns GREEN ≠ RED. This proves depth does OCCLUSION (the seam is load-
      // bearing), not merely that a depth buffer is attached.
      std::vector<uint8_t> pOff = renderPair(true);
      int dr, dg, db; readRGB(pOff, W, W / 2, H / 2, dr, dg, db);
      bool bites = depthOccludes && (dg > 200 && dr < 60);  // depth-on RED, depth-off GREEN = the tooth
      b = !bites;  // injectBug must trip → allFaithful false
      std::printf("[selftest-drawmeshunlit] B bug(depth-off): depthOn=(%d,%d,%d) depthOff=(%d,%d,%d) "
                  "bites=%d (order-wins-green replaces near-red)\n", cr, cg, cb, dr, dg, db, bites ? 1 : 0);
    }
    allFaithful = allFaithful && b;
  }

  lib->release(); q->release(); dev->release(); pool->release();

  if (injectBug) {
    if (allFaithful) {
      std::printf("[selftest-drawmeshunlit] FAIL: injectBug tripped no tooth\n");
      return 1;
    }
    std::printf("[selftest-drawmeshunlit] injectBug correctly RED (dropped projection mis-places the "
                "quad; depth-off would let draw-order green replace the near red)\n");
    return 1;
  }
  std::printf("[selftest-drawmeshunlit] %s\n", allFaithful ? "PASS" : "FAIL");
  return allFaithful ? 0 : 1;
}

}  // namespace sw
