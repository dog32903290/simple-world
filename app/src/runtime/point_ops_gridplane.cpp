// GridPlane command op — TiXL Operators/Lib/render/gizmo/GridPlane.cs (Guid 935e6597) + its .t3
// internal-children graph (Transform/DepthStencilState/RenderTargetBlendDescription/RasterizerState/
// VertexShader/PixelShader draw-GridPlane.hlsl). A SINGLE hand-crafted C++ DrawKind::GridPlane item —
// NOT decomposed into a compound of Transform+Draw+RenderState children like TiXL's own .t3 does (see
// render_command.h's DrawKind enum comment). Command SOURCE (no subtree/points/mesh input), like
// DrawLines/DrawMeshUnlit — a pure data-stamper (zero render code; cookRenderTarget's GridPlane case +
// gridplane_fill.cpp do the drawing).
//
// TRANSFORM (GridPlane.t3's internal "Transform" child 389a3707, transcribed): Scale is HARDCODED
// (100,100,1) in that child's own InputValues — NOT wired to GridPlane's own Scale input (that instead
// feeds the shader's grid-density Params.Scale, a same-named-but-different concept — RenderDrawItem::
// gridScale). Rotation = GridPlane.Rotation (.t3 default X=90,Y=0,Z=0 — tips the XY quad flat to
// become a ground plane). No Translation/Pivot wired (both stay 0,0,0). M = S·R exactly like
// cookTransform's math (point_ops_transform.cpp:101-109) with pivot=0/translation=0 collapsing its
// general T(-piv)·S·R·T(+piv)·T(trans) down to S·R. Reuses the SAME per-item groupObjectToWorld/
// hasGroup stamp (S2b) — an OUTER Group/Transform wrapping GridPlane's Command output composes
// correctly (point_ops_transform.cpp:114's existing·thisM accumulation, innermost-first).
//
// RENDER STATE (GridPlane.t3's DepthStencilState 4b8041c3 + BlendState/RenderTargetBlendDescription
// 6d9f2c25/7d616c65 + RasterizerState e9ca366b, transcribed into ONE FrozenRenderState — reuses Seam 2,
// zero new executor branches beyond the switch case itself):
//   DepthStencilState: EnableZWrite=false, EnableZTest=true (DepthFunc untouched → DX11/struct default
//     Less) → depthWrite=false, depthCompare=Less(default, unchanged). A THIRD depth posture next to
//     dsMesh (LessEqual+Write) and dsDisabled (Always+NoWrite): the grid TESTS against existing depth
//     but never WRITES it (a translucent overlay that never occludes itself or other translucent draws).
//   RenderTargetBlendDescription: BlendEnabled=true, SourceBlend=SourceAlpha, SourceAlphaBlend=
//     SourceAlpha, DestinationBlend/DestinationAlphaBlend=InverseSourceAlpha, BlendOp unset→Add (DX11
//     default) — the SAME alpha-over equation on BOTH RGB and alpha channels (unlike cookOutputMerger's
//     convenience-op alpha-channel constant srcA=One; GridPlane's raw BlendState sets SourceAlphaBlend
//     explicitly, so this op sets srcA itself rather than reusing that constant).
//   RasterizerState: CullMode=None, FrontCounterClockwise=true (DX11 defaults are Back/CW — both
//     flipped). fillMode/topology stay at FrozenRenderState's struct defaults (Solid/TriangleList,
//     untouched by GridPlane.t3 — the Draw child's VertexCount=6 matches TriangleList's 2×3 verts).
#include "runtime/point_ops.h"

#include <cstdio>
#include <cmath>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dx11_metal_state_map.h"  // Dx11Blend/Dx11BlendOp/Dx11Cull ordinals
#include "runtime/field_camera.h"          // Mat4/mat4Identity/mat4Mul
#include "runtime/point_graph.h"           // CmdCookCtx/registerCmdOp/cookParam/cookVecN + PointGraph
#include "runtime/render_command.h"        // RenderCommand/RenderDrawItem/DrawKind/FrozenRenderState

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

namespace {
constexpr float kPi = 3.14159265358979323846f;

// R = CreateFromQuaternion(CreateFromYawPitchRoll(yaw,pitch,roll)) — VERBATIM copy of
// point_ops_transform.cpp's yawPitchRollRowMajor (itself a verbatim copy of field_camera.cpp's
// groupObjectToWorld R block). Kept file-local rather than shared: a 20-line closed-form leaf, same
// posture as point_ops_transform.cpp's own file-local copy (parity authority: field_camera.cpp:261-276).
Mat4 yawPitchRollRowMajor(float yawDeg, float pitchDeg, float rollDeg) {
  float yaw = yawDeg * kPi / 180.0f, pitch = pitchDeg * kPi / 180.0f, roll = rollDeg * kPi / 180.0f;
  float sr = std::sin(roll * 0.5f), cr = std::cos(roll * 0.5f);
  float sp = std::sin(pitch * 0.5f), cp = std::cos(pitch * 0.5f);
  float sy = std::sin(yaw * 0.5f), cy = std::cos(yaw * 0.5f);
  float qx = cy * sp * cr + sy * cp * sr;
  float qy = sy * cp * cr - cy * sp * sr;
  float qz = cy * cp * sr - sy * sp * cr;
  float qw = cy * cp * cr + sy * sp * sr;
  float xx = qx * qx, yy = qy * qy, zz = qz * qz;
  float xy = qx * qy, wz = qz * qw, xz = qz * qx, wy = qy * qw, yz = qy * qz, wx = qx * qw;
  Mat4 R = mat4Identity();
  R.m[0] = 1.0f - 2.0f * (yy + zz); R.m[1] = 2.0f * (xy + wz);        R.m[2] = 2.0f * (xz - wy);
  R.m[4] = 2.0f * (xy - wz);        R.m[5] = 1.0f - 2.0f * (zz + xx); R.m[6] = 2.0f * (yz + wx);
  R.m[8] = 2.0f * (xz + wy);        R.m[9] = 2.0f * (yz - wx);        R.m[10] = 1.0f - 2.0f * (yy + xx);
  return R;
}

// GridPlane.t3's DepthStencilState+BlendState+RasterizerState children, folded to ONE FrozenRenderState
// (see file header for the field-by-field transcription). Shared by the cook op AND the golden below.
FrozenRenderState gridPlaneFrozenState() {
  FrozenRenderState st;  // struct defaults = DX11 defaults (cull Back/CW, depth Less/Write, blend off)
  st.cullMode = (uint32_t)Dx11Cull::None;
  st.frontCCW = true;
  st.depthWrite = false;  // EnableZWrite=false; depthCompare stays Less (EnableZTest=true, DepthFunc untouched)
  st.rt.enabled = true;
  st.rt.srcRGB = (uint32_t)Dx11Blend::SrcAlpha;
  st.rt.dstRGB = (uint32_t)Dx11Blend::InvSrcAlpha;
  st.rt.opRGB = (uint32_t)Dx11BlendOp::Add;
  st.rt.srcA = (uint32_t)Dx11Blend::SrcAlpha;     // GridPlane's own SourceAlphaBlend (not OutputMerger's One)
  st.rt.dstA = (uint32_t)Dx11Blend::InvSrcAlpha;  // GridPlane's own DestinationAlphaBlend
  st.rt.opA = (uint32_t)Dx11BlendOp::Add;
  return st;
}
}  // namespace

// GridPlane: no input → Command out (a Command SOURCE, like DrawLines/DrawMeshUnlit). Always emits
// exactly one DrawKind::GridPlane item (TiXL has no IsEnabled/unwired-skip on this op).
RenderCommand cookGridPlane(CmdCookCtx& c) {
  RenderCommand rc;
  RenderDrawItem it{};
  it.kind = DrawKind::GridPlane;
  float colorDef[4] = {1.0f, 1.0f, 1.0f, 0.25f};  // GridPlane.Color (.t3 default)
  cookVecN(c, "Color", colorDef, 4, it.color);
  it.size = cookParam(c, "Size", 10.0f);       // GridPlane.Size (.t3 default 10.0)
  it.gridScale = cookParam(c, "Scale", 1.0f);  // GridPlane.Scale (.t3 default 1.0)
  float rotDef[3] = {90.0f, 0.0f, 0.0f};       // GridPlane.Rotation (.t3 default X=90 → ground plane)
  float rot[3];
  cookVecN(c, "Rotation", rotDef, 3, rot);

  Mat4 S = mat4Identity();
  S.m[0] = 100.0f; S.m[5] = 100.0f; S.m[10] = 1.0f;  // Transform child's hardcoded Scale (see file header)
  Mat4 R = yawPitchRollRowMajor(/*yaw=*/rot[1], /*pitch=*/rot[0], /*roll=*/rot[2]);
  Mat4 m = mat4Mul(S, R);
  it.hasGroup = true;
  for (int i = 0; i < 16; ++i) it.groupObjectToWorld[i] = m.m[i];

  it.hasRenderState = true;
  it.frozen = gridPlaneFrozenState();

  rc.items.push_back(it);
  return rc;
}

void registerGridPlaneOp() { registerCmdOp("GridPlane", cookGridPlane); }

// ───────────────────────────────── GOLDEN ─────────────────────────────────
// Closed-form derivation at a PROBE pixel deliberately OFF the p=(0,0) knife-edge (draw-GridPlane.hlsl
// line numbers cited throughout). The probe sits on the quad's vertical centerline (object x=0 exactly,
// world x=0) but off-center vertically (world y=0.5, a small but UNAMBIGUOUS offset — landing exactly at
// world y=0 would leave p.y=0 exactly, which lands ON the `p.y<0` branch boundary and is genuinely
// ambiguous under floating-point noise; a real first-attempt bug this file's history records).
//
//   p.x = (texCoord.x-0.5)/scaleUV.x. At object x=0, quadVertex.x=0 → texCoord.x=0.5 exactly (hlsl:56) →
//   p.x=0 EXACTLY, for ANY scaleUV.x (hlsl:83) — robust regardless of camera/resolution.
//
//   axisZ (hlsl:97) multiplies by abs(p.x)=abs(0)=0 before the smoothstep → axisZ=smoothstep(-0.1,0.1,0)
//   =0.5 EXACTLY (t=0.5, smoothstep's symmetric midpoint) — robust, independent of angleX. axisX (hlsl:96)
//   multiplies by abs(p.y)*(0.3+angleY*100) ≥ 0 ALWAYS (product of non-negative terms) → its smoothstep
//   input ≥0 → axisX ≥ smoothstep(-0.1,0.1,0) = 0.5 ALWAYS, for ANY p.y (any sign, any magnitude, any
//   angleY). So redOrBlue=(axisX<axisZ=0.5)?0:1 is ALWAYS 1 (hlsl:99) — axisX can never be STRICTLY below
//   0.5. isAxis=(min(axisZ=0.5,axisX≥0.5)<0.75)?1:0=1 ALWAYS too (min is always 0.5<0.75) (hlsl:104) —
//   BOTH robust for ANY p.y, not just this probe's specific offset.
//
//   axisColor=lerp(A,B,redOrBlue=1)=B=(p.y<0?(0.2,0.2,0.9):(0.5,0.5,0.7)) (hlsl:100-103) — THIS is the one
//   genuinely sign-dependent term, which is why the probe is placed at a p.y CLEARLY away from 0 (world
//   y=0.5 → p.y=-0.5, derived below) rather than exactly 0. color=lerp((1,1,1),axisColor,1)=axisColor
//   (hlsl:105) since isAxis=1 always (just shown).
//
//   p.y at THIS probe: scaleUV=(sum-of-rows(inverse(S(100,100,1))).xy)/Size = (0.01,0.01)/2 = (0.005,
//   0.005) (S is diagonal → its inverse is diag(0.01,0.01,1,1); gridplane_fill.cpp's "mul ones-row").
//   object y=world y/100=0.005 → quadVertex.y=objY/(Size·0.5)=0.005/1=0.005 → texCoord.y=quadVertex.y·
//   (-0.5)+0.5=0.4975 → p.y=(0.4975-0.5)/0.005=-0.5 — CLEARLY negative (not a boundary value) → axisColor
//   =(0.2,0.2,0.9), color=(0.2,0.2,0.9) EXACTLY, closed-form, no fwidth dependency.
//
//   lineIntensity=max(linesX,linesY) (hlsl:94): linesX uses p.x=0 for ALL THREE spacings, and lines()'s
//   smoothstep ratio is scale-invariant (lineWidth2=0.3·angle/spacing, feather2=5·lineWidth2, so
//   low=-4L/high=6L, t=4L/10L=0.4 for ANY L>0) → gridLines(0,·,·)=1-smoothstep(0.4)=1-0.352=0.648 EXACTLY
//   for all 3 spacings (hlsl:68-76) → linesX=0.648·(smallGrid+grid10+1) ∈ [0.648,1.944] robustly
//   (smallGrid,grid10∈[0,1] by smoothstep's range, no geometry engineering). linesY (at p.y=-0.5, NOT 0)
//   has no such closed form, but EACH gridLines() call is individually ∈[0,1] (same range property) so
//   linesY ≤ 1·1+1·1+1 = 3 (loose). lineIntensity=max(linesX≥0.648, linesY≤3) ∈ [0.648, 3] (hlsl:91-94).
//   alpha=lineIntensity·(isAxis?2:1)·Color.w = lineIntensity·2·Color.w (hlsl:106).
//
//   Color.w=0.1 (golden's own choice — small enough that even the LOOSE upper bound keeps alpha<1, so
//   blend-vs-opaque genuinely differ; DrawMeshUnlit precedent for picking clean test colors, not the .t3
//   default 0.25): alpha ∈ [0.648·2·0.1, 3·2·0.1] = [0.1296, 0.6] — safely inside (0,1) both ends.
//
//   FAITHFUL (SourceAlpha/InvSourceAlpha blend over BLACK): outRGB=color·alpha (dst=0). R=G=0.2·alpha
//   ∈[0.02592,0.12]→8-bit∈[6.6,30.6]; B=0.9·alpha∈[0.11664,0.54]→8-bit∈[29.7,137.7].
//
// injectBug: drop the Seam-2 stamp (hasRenderState=false) — the item takes pickPSO's LEGACY unstamped
// path (blend=false → blending DISABLED). The SHADER still computes rgb=color=(0.2,0.2,0.9) EXACTLY
// (Color.rgb=(1,1,1) doesn't attenuate it) and alpha=lineIntensity·2·0.1 as before, but with blend OFF the
// ROP writes rgb DIRECTLY (alpha only lands in the target's alpha channel, RGB is unaffected by it) →
// R=G=0.2, B=0.9 → 8-bit=(51,51,229) EXACTLY, INDEPENDENT of alpha/lineIntensity's actual value — always
// clearly ABOVE the faithful upper bound (30.6/137.7), a robust, closed-form-separated bite proving the
// Seam-2 reuse is load-bearing (not just present).
int runGridPlaneSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 65, H = 65;  // odd (unused now that the probe is forward-projected, kept for parity
                                   // with the render-target goldens' square-odd convention)

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-gridplane] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerGridPlaneOp();

  // Faithful item: Rotation=(0,0,0) (R=Identity, quad faces the default camera directly — the psMain
  // math is orientation-independent, so any rotation is equally valid; 0 keeps the geometry trivial).
  auto makeItem = [&](bool stamped) -> RenderDrawItem {
    RenderDrawItem it{};
    it.kind = DrawKind::GridPlane;
    it.color[0] = it.color[1] = it.color[2] = 1.0f; it.color[3] = 0.1f;  // see derivation: Color.w=0.1
    it.size = 2.0f;       // object half-extent = 1 (world half-extent = 100 with the S(100) transform)
    it.gridScale = 1.0f;
    Mat4 S = mat4Identity(); S.m[0] = 100.0f; S.m[5] = 100.0f; S.m[10] = 1.0f;
    it.hasGroup = true;
    for (int i = 0; i < 16; ++i) it.groupObjectToWorld[i] = S.m[i];  // R(0,0,0)=Identity, so M=S
    if (stamped) { it.hasRenderState = true; it.frozen = gridPlaneFrozenState(); }
    return it;
  };
  // The probe: forward-project world (0,0.5,0) through the SAME default camera the executor uses (mirror
  // of DrawMeshUnlit's projectDefault) to find the exact pixel — deterministic, no fwidth dependency.
  auto ndcXToPx = [](float ndcX, uint32_t Wt) { return (int)((ndcX * 0.5f + 0.5f) * (float)(Wt - 1) + 0.5f); };
  auto ndcYToPx = [](float ndcY, uint32_t Ht) {
    return (int)((1.0f - (ndcY * 0.5f + 0.5f)) * (float)(Ht - 1) + 0.5f);
  };
  LayerCameraForward cam = defaultLayerCameraForward(1.0f);  // aspect=1 (square target)
  Mat4 probeO2c = objectToClipSpace(mat4Identity(), cam.worldToCamera, cam.cameraToClipSpace);
  float ndc[3];
  mat4TransformPointDivW(probeO2c, 0.0f, 0.5f, 0.0f, ndc);
  int probeX = ndcXToPx(ndc[0], W), probeY = ndcYToPx(ndc[1], H);

  auto renderProbe = [&](bool stamped, int& r, int& g, int& b) {
    MTL::TextureDescriptor* td =
        MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
    td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
    td->setStorageMode(MTL::StorageModeShared);
    MTL::Texture* tex = dev->newTexture(td);
    RenderCommand rc; rc.items.push_back(makeItem(stamped));
    TexCookCtx c; c.dev = dev; c.lib = lib; c.queue = q; c.nodeId = 1; c.command = &rc; c.output = tex;
    cookRenderTarget(c);
    std::vector<uint8_t> px((size_t)W * H * 4, 0);
    tex->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
    size_t i = ((size_t)probeY * W + (size_t)probeX) * 4;
    r = px[i]; g = px[i + 1]; b = px[i + 2];
    tex->release();
  };

  int fr, fg, fb;
  renderProbe(/*stamped=*/true, fr, fg, fb);
  // Faithful bound: R,G ∈ [6.6,30.6] (loose margin [2,45]); B ∈ [29.7,137.7] (loose margin [15,160]).
  bool toothA = fr >= 2 && fr <= 45 && std::abs(fr - fg) <= 6 && fb >= 15 && fb <= 160;
  std::printf("[selftest-gridplane] faithful probe(%d,%d)=(%d,%d,%d) want R,G in[2,45] B in[15,160] -> %s\n",
              probeX, probeY, fr, fg, fb, toothA ? "faithful-ok" : "tripped");

  bool pass;
  if (!injectBug) {
    pass = toothA;
  } else {
    int br, bg, bb;
    renderProbe(/*stamped=*/false, br, bg, bb);
    // Unstamped legacy path is opaque (blend disabled) — the SAME (0.2,0.2,0.9) fragment writes
    // UNattenuated by alpha (0.2·255=51, 0.9·255≈229 EXACTLY — no fwidth dependency at all, unlike the
    // faithful leg's alpha-scaled range), clearly ABOVE the faithful upper bound (R,G<=45, B<=160).
    bool bites = toothA && br > 46 && bb > 190;
    pass = !bites;  // injectBug must trip → pass=false when the tooth genuinely bit
    std::printf("[selftest-gridplane] bug(unstamped) probe=(%d,%d,%d) bites=%d (Seam-2 blend dropped -> "
                "opaque (0.2,0.2,0.9) unattenuated, brighter than the faithful ceiling)\n", br, bg, bb,
                bites ? 1 : 0);
  }

  lib->release(); q->release(); dev->release(); pool->release();

  if (injectBug) {
    if (pass) { std::printf("[selftest-gridplane] FAIL: injectBug tripped no tooth\n"); return 1; }
    std::printf("[selftest-gridplane] injectBug correctly RED (Seam-2 render-state stamp is load-bearing)\n");
    return 1;
  }
  std::printf("[selftest-gridplane] %s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

}  // namespace sw
