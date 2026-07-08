// --selftest-pbr-shading — the KEYSTONE PBR golden (this battle's load-bearing tooth). A centered lit QuadMesh
// facing the camera, one SetPointLight, a SetMaterial → the Cook-Torrance DIRECT-lighting closed form hand-
// computed from TiXL's pbr.hlsl / pbr-render.hlsl (line-cited), compared against the rendered center pixel via
// readback. BOTH cook legs (flat + resident=production) — the S2c mirror gate.
//
// WHY THIS IS A CLOSED-FORM GOLDEN (GOLDEN_STANDARD three features):
//  1. Expected value INDEPENDENT-OF-IMPL: the oracle `computePbrHost()` below is transcribed DIRECTLY from
//     external/tixl pbr.hlsl:35-71 + pbr-render.hlsl:29-110 (every line cited) — NOT from sw's MSL, NOT from
//     an sw helper, NOT from a second dispatch. Pure host float math over the SAME inputs the shader gets.
//  2. Sampled at the DIVERGING MIDDLE: the light is OFF the view axis and the material is metal≈0.1 /
//     roughness≈0.35 / specular≈2 (not identity) so NdotL, NdotH, the Fresnel/NDF/geometry terms are all
//     non-trivial. Swap any BRDF term for a wrong one (drop specular, wrong NDF power, forget NdotL) → the
//     expected pixel moves → RED. The probe is the mesh CENTER (dead inside the quad, off every edge).
//  3. injectBug = a REAL cook-path corruption: pointLightScopeBugSkipPush() skips the SetPointLight push on
//     BOTH legs → DrawMeshPbr reads ZERO lights → directLighting = 0 → the center pixel collapses to the
//     emissive/ambient tail (near-black for this material) ≠ the lit oracle → RED on flat AND resident.
//
// The camera is the DEFAULT (eye at +Z, looking at origin), so the quad at world z=0 projects NDC≈world.xy
// (the same convention DrawMeshUnlit's golden uses) and worldPosition of the center = origin. That makes N, V,
// and the light geometry all exactly known → the oracle is exact, no fwidth/derivative/screen-space term
// (the deferred SpecularAA/flat-shading/IBL branches are all off — see mesh_draw_pbr.metal scope note).
#include "runtime/point_ops_pbrshading.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/field_camera.h"     // defaultLayerCameraForward / objectToClipSpace / mat4* (host projection)
#include "runtime/graph.h"            // Graph/Node/pinId
#include "runtime/graph_bridge.h"     // libFromGraph (flat Graph → SymbolLibrary, resident leg)
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / ResidentEvalGraph (production resident path)
#include "runtime/point_graph.h"      // PointGraph / registerBuiltinPointOps
#include "runtime/render_command.h"   // RenderCommand
#include "runtime/selftest_registry.h"  // REGISTER_SELFTESTS
#include "runtime/sw_pbr_scope.h"     // pointLightScopeBugSkipPush (the injectBug seam)
#include "runtime/tixl_point.h"       // EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

constexpr float kPI = 3.141592f;      // pbr.hlsl:11
constexpr float kEps = 0.00001f;      // pbr.hlsl:15
constexpr float kFdielectric = 0.04f; // pbr.hlsl:31

struct V3 { float x, y, z; };
static V3 sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static V3 add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static V3 scl(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
static float dot3(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static float len3(V3 a) { return std::sqrt(dot3(a, a)); }
static V3 nrm3(V3 a) { float l = len3(a); return l > 0 ? scl(a, 1.0f / l) : a; }
static float sat(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }

// ── The BRDF pieces, transcribed VERBATIM from pbr.hlsl (independent-of-impl oracle) ──
static float ndfGGX(float cosLh, float roughness) {  // pbr.hlsl:35-42
  float alpha = roughness * roughness, alphaSq = alpha * alpha;
  float denom = (cosLh * cosLh) * (alphaSq - 1.0f) + 1.0f;
  return alphaSq / (kPI * denom * denom);
}
static float gaG1(float cosTheta, float k) { return cosTheta / (cosTheta * (1.0f - k) + k); }  // :45-48
static float gaSchlickGGX(float cosLi, float cosLo, float roughness) {  // :51-56
  float r = roughness + 1.0f, k = (r * r) / 8.0f;
  return gaG1(cosLi, k) * gaG1(cosLo, k);
}
static V3 fresnel(V3 F0, float cosTheta) {  // :59-62  F0 + (1-F0)*pow(1-cosTheta,5)
  float f = std::pow(1.0f - cosTheta, 5.0f);
  return {F0.x + (1 - F0.x) * f, F0.y + (1 - F0.y) * f, F0.z + (1 - F0.z) * f};
}

// The full direct-lighting ComputePbr for ONE light (pbr-render.hlsl:29-110), host replica. Returns the
// linear RGB of the center pixel (albedo = baseColor.rgb since vertexColor=white; N,V,worldPos given).
// ★albedo vs BaseColor (the semantics trap): frag.albedo = BaseColorMap.Sample() (mesh-Draw.hlsl:217) = the
// TEXTURE, whose no-map default is WHITE (1,1,1) — NOT the material BaseColor. `*= colorRGB` (vertexColor,
// white). BaseColor (the material param) multiplies only ONCE, at the FINAL litColor (pbr-render.hlsl:106).
// So the diffuse/F0/kd all use albedo=WHITE; baseColor tints the whole result at the end.
struct PbrInputs {
  V3 N, V, worldPos;
  V3 albedo;        // = BaseColorMap sample (WHITE, no texture) * vertexColor (WHITE) — NOT baseColor
  V3 baseColor;     // the material BaseColor param (applied once at the final litColor)
  float roughness, metal, specular;
  V3 lightPos, lightColor; float intensity, range, decay;
  V3 emissive;
};
static V3 computePbrHost(const PbrInputs& in, bool lightActive) {
  V3 N = in.N, V = in.V;
  V3 F0 = {kFdielectric + (in.albedo.x - kFdielectric) * in.metal,   // lerp(Fdielectric,albedo,metal); :43
           kFdielectric + (in.albedo.y - kFdielectric) * in.metal,
           kFdielectric + (in.albedo.z - kFdielectric) * in.metal};
  V3 direct = {0, 0, 0};
  if (lightActive) {  // the single light (pbr-render.hlsl:47-71)
    V3 Lvec = sub(in.lightPos, in.worldPos);
    float dist = len3(Lvec);
    V3 L = scl(Lvec, 1.0f / std::fmax(dist, 1e-4f));
    float intensity = in.intensity / (std::pow(dist / in.range, in.decay) + 1.0f);
    V3 Lrad = scl(in.lightColor, intensity);
    float NdotV = sat(dot3(N, V)), NdotL = sat(dot3(N, L));
    V3 H = nrm3(add(L, V));
    float NdotH = sat(dot3(N, H));
    V3 F = fresnel(F0, sat(dot3(H, V)));
    float D = ndfGGX(NdotH, in.roughness), G = gaSchlickGGX(NdotL, NdotV, in.roughness);
    V3 kd = {(1 - F.x) * (1 - in.metal), (1 - F.y) * (1 - in.metal), (1 - F.z) * (1 - in.metal)};  // lerp(1-F,0,metal)
    V3 diffuse = {kd.x * in.albedo.x, kd.y * in.albedo.y, kd.z * in.albedo.z};
    float specScale = 1.0f / std::fmax(kEps, 4.0f * NdotL * NdotV);
    V3 spec = {F.x * D * G * specScale * in.specular, F.y * D * G * specScale * in.specular,
               F.z * D * G * specScale * in.specular};
    V3 brdf = add(diffuse, spec);
    direct = scl((V3){brdf.x * Lrad.x, brdf.y * Lrad.y, brdf.z * Lrad.z}, NdotL);
  }
  // litColor = (direct + 0[ambient dropped]) * BaseColor * Color(white); + emissive (pbr-render.hlsl:106-108).
  V3 lit = {direct.x * in.baseColor.x + in.emissive.x, direct.y * in.baseColor.y + in.emissive.y,
            direct.z * in.baseColor.z + in.emissive.z};
  // No scoped fog in this golden (ambient default distance=10000 → fog≈0 at z=0) → no fog lerp effect.
  return lit;
}

int px(float v) { int c = (int)std::lround(sat(v) * 255.0f); return c < 0 ? 0 : (c > 255 ? 255 : c); }

// Cook the lit-quad graph and read the CENTER pixel. Returns false on a miss. The graph:
// QuadMesh(centered, facing +z) → DrawMeshPbr → SetMaterial → SetPointLight → RenderTarget.
// (SetMaterial/SetPointLight WRAP the draw as Command subtrees, pushing the scope the draw reads.)
bool cookLit(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* q, int W, int H, int whichPath,
             uint8_t& r, uint8_t& g, uint8_t& b) {
  PointGraph pg(dev, lib, q, W, H);
  Graph gr;
  // A centered QuadMesh at world z=0, Normal=ForwardLH=(0,0,1) (faces the default camera). A REAL production
  // mesh op (statically registered, resident-proven) so BOTH cook legs gather it identically — the same
  // fixture DrawMeshUnlit's golden uses. QuadMesh's .t3-parity default Pivot=(0,0) → offset=stretch*scale*
  // (pivot-0.5)=(-0.5,-0.5) ALREADY centers a Scale=1 quad at the origin (world span [-0.5,0.5]² at z=0,
  // no Center compensation needed) → the center pixel's worldPos = origin, N=(0,0,1) (the exact geometry
  // the host oracle assumes).
  Node mesh; mesh.id = 1; mesh.type = "QuadMesh";
  mesh.params["Segments.x"] = 1.0f; mesh.params["Segments.y"] = 1.0f; mesh.params["Scale"] = 1.0f;
  gr.nodes.push_back(mesh);
  Node draw; draw.id = 2; draw.type = "DrawMeshPbr"; gr.nodes.push_back(draw);
  Node setMat; setMat.id = 3; setMat.type = "SetMaterial";
  setMat.params["BaseColor.x"] = 0.80f; setMat.params["BaseColor.y"] = 0.60f;
  setMat.params["BaseColor.z"] = 0.40f; setMat.params["BaseColor.w"] = 1.0f;
  setMat.params["Roughness"] = 0.35f; setMat.params["Specular"] = 2.0f; setMat.params["Metal"] = 0.10f;
  setMat.params["EmissiveColor.x"] = 0.0f; setMat.params["EmissiveColor.y"] = 0.0f;
  setMat.params["EmissiveColor.z"] = 0.0f; setMat.params["EmissiveColor.w"] = 1.0f;
  gr.nodes.push_back(setMat);
  Node setLight; setLight.id = 4; setLight.type = "SetPointLight";
  setLight.params["Position.x"] = 1.5f; setLight.params["Position.y"] = 1.0f; setLight.params["Position.z"] = 2.0f;
  setLight.params["Intensity"] = 2.5f;  // tuned so NO channel saturates → all three RGB test the BRDF (no clamp-hidden error)
  setLight.params["Color.x"] = 1.0f; setLight.params["Color.y"] = 1.0f; setLight.params["Color.z"] = 1.0f;
  setLight.params["Color.w"] = 1.0f;
  setLight.params["Range"] = 4.0f; setLight.params["Decay"] = 2.0f;
  gr.nodes.push_back(setLight);
  Node rt; rt.id = 5; rt.type = "RenderTarget"; rt.params["Resolution"] = 4.0f;
  rt.params["CustomW"] = (float)W; rt.params["CustomH"] = (float)H; gr.nodes.push_back(rt);
  // Wiring: mesh → draw.Mesh ; draw → setMat.SubTree ; setMat → setLight.SubTree ; setLight → RT.command.
  gr.connections.push_back({101, pinId(1, 1), pinId(2, 0)});  // TriMesh.Data → DrawMeshPbr.Mesh
  gr.connections.push_back({102, pinId(2, 1), pinId(3, 0)});  // DrawMeshPbr.out → SetMaterial.SubTree
  gr.connections.push_back({103, pinId(3, 0), pinId(4, 0)});  // SetMaterial.out → SetPointLight.SubTree
  gr.connections.push_back({104, pinId(4, 0), pinId(5, 0)});  // SetPointLight.out → RenderTarget.command

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  if (whichPath == 0) {
    pg.cook(gr, ctx, nullptr, /*terminal=*/5);
  } else {
    // RESIDENT leg = PRODUCTION (the S2c gate): cook through buildEvalGraph/cookResident so the resident
    // command-cook's mirrored PBR scope-set is exercised. A resident-only miss = a prod-only unlit black-hole.
    SymbolLibrary slib = libFromGraph(gr);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    pg.cookResident(rg, ctx, nullptr, /*RenderTarget path=*/"5");
  }
  MTL::Texture* tex = pg.target();
  if (!tex) return false;
  std::vector<uint8_t> pxv((size_t)W * H * 4, 0);
  tex->getBytes(pxv.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  size_t i = ((size_t)(H / 2) * W + (W / 2)) * 4;
  r = pxv[i]; g = pxv[i + 1]; b = pxv[i + 2];
  return true;
}

}  // namespace

int runPbrShadingSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const int W = 256, H = 256;
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-pbr-shading] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();  // QuadMesh / DrawMeshPbr / SetMaterial / SetPointLight / RenderTarget self-register

  // Host oracle: worldPos of the CENTER pixel = origin (the quad is centered at z=0, the camera axis
  // hits the origin). N = (0,0,1) (the quad normal ForwardLH). V = normalize(eye - origin); the default camera eye
  // is at (0,0,defaultCameraDistance) → V = (0,0,1). The light is off-axis (1.5,1,2).
  PbrInputs in{};
  in.N = {0, 0, 1};
  in.V = {0, 0, 1};
  in.worldPos = {0, 0, 0};
  in.albedo = {1.0f, 1.0f, 1.0f};       // BaseColorMap default (white) * vertexColor (white) — the texture, not BaseColor
  in.baseColor = {0.80f, 0.60f, 0.40f}; // the material BaseColor param (the SetMaterial value; applied at the final litColor)
  in.roughness = 0.35f; in.metal = 0.10f; in.specular = 2.0f;
  in.lightPos = {1.5f, 1.0f, 2.0f}; in.lightColor = {1, 1, 1};
  in.intensity = 2.5f; in.range = 4.0f; in.decay = 2.0f;
  in.emissive = {0, 0, 0};
  V3 litOracle = computePbrHost(in, /*lightActive=*/true);
  int er = px(litOracle.x), eg = px(litOracle.y), eb = px(litOracle.z);

  pointLightScopeBugSkipPush() = injectBug;  // ★bug = skip the light push on BOTH legs → directLighting = 0
  auto near8 = [](int a, int e) { return std::abs(a - e) <= 2; };
  const char* legName[2] = {"flat", "resident"};
  bool allMatch = true, allTripped = true;  // faithful: both match; bug: both go dark (both legs = S2c gate)
  bool anyMiss = false;
  for (int leg = 0; leg < 2; ++leg) {
    uint8_t r = 0, g = 0, b = 0;
    bool ok = cookLit(dev, lib, q, W, H, leg, r, g, b);
    if (!ok) { anyMiss = true; }
    bool matches = ok && near8(r, er) && near8(g, eg) && near8(b, eb);
    bool darkNoLight = ok && (r < 20 && g < 20 && b < 20);
    allMatch = allMatch && matches;
    allTripped = allTripped && darkNoLight;
    std::printf("[selftest-pbr-shading] %s center=(%d,%d,%d) oracle=(%d,%d,%d) matches=%d dark=%d\n",
                legName[leg], r, g, b, er, eg, eb, matches ? 1 : 0, darkNoLight ? 1 : 0);
  }
  pointLightScopeBugSkipPush() = false;  // process hygiene
  bool matches = allMatch && !anyMiss;  // faithful pass = BOTH legs matched (S2c: resident is production)

  lib->release(); q->release(); dev->release(); pool->release();

  if (injectBug) {
    // -bug: the light push was skipped on BOTH legs → the lit oracle must NOT be reproduced (pixel dark).
    if (matches) {
      std::printf("[selftest-pbr-shading] injectBug did not trip: still matched the lit oracle (the light "
                  "scope is not actually feeding DrawMeshPbr — the seam is hollow)\n");
      return 0;  // did-not-trip -> 0, NO-BITE list catches it (GOLDEN_STANDARD 特徵3)
    }
    std::printf("[selftest-pbr-shading] injectBug correctly RED (skipped SetPointLight push → zero lights → "
                "directLighting=0 → center pixel dark, not the lit Cook-Torrance value)\n");
    return 1;
  }
  std::printf("[selftest-pbr-shading] %s\n", matches ? "PASS" : "FAIL");
  return matches ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/340, {"pbr-shading", runPbrShadingSelfTest});

}  // namespace sw
