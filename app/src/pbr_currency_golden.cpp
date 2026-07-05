// pbr_currency_golden — DATA-LAYER goldens for the pbr-lighting island's value currencies
// (SwPointLight / SwFogParameters / SwPbrParameters). Three --selftest entries:
//
//   --selftest-pointlight    (runPointLightSelfTest):  swResolvePointLight vs SetPointLight.cs:17-21.
//   --selftest-fog           (runFogSelfTest):         swResolveFog        vs SetFog.cs:20-25 + the
//                                                      ambient-default guard (FogSettings.cs:15-20).
//   --selftest-pbrmaterial   (runPbrMaterialSelfTest): swResolvePbrParameters vs SetMaterial.cs:34-38 +
//                                                      the fresh-material default (PbrMaterial.cs:83-88).
//
// WHY DATA-LAYER (the honest boundary): these are the value halves of the SetX context-push ops. The
// context stacks (EvaluationContext.PointLights / FogParameters / Materials) and their PBR-shader
// consumer are the BLOCKED render-pass island (census ops-render.md:106-114, CLONE_MAP.md:117). There is
// NO consumer to round-trip through and NO cook seam to corrupt — so per GOLDEN_STANDARD:36-38 (no seam →
// the golden documents the technical reason) the -bug leg is a STRUCTURAL corruption of the resolver's
// input→field mapping (swap two fields), which a want-flip cannot fake: it proves the golden is bound to
// the field ORDER of the resolver, not just to a scalar. The expected values are hand-derived from the
// TiXL .cs (impl-independent), probing OFF the identity (non-zero/non-default inputs) so a mis-mapped
// resolver (wrong field, dropped input) flips RED. Pure host math — no Metal, no platform dep.
#include <cmath>
#include <cstdio>

#include "runtime/sw_fog.h"           // SwFogParameters / swResolveFog / swFogAmbientDefault
#include "runtime/sw_pbr_material.h"  // SwPbrParameters / swResolvePbrParameters
#include "runtime/sw_point_light.h"   // SwPointLight / swResolvePointLight

namespace sw {

namespace {

bool nearf(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) < eps; }
bool near3(simd::float3 a, simd::float3 b) { return nearf(a.x, b.x) && nearf(a.y, b.y) && nearf(a.z, b.z); }
bool near4(simd::float4 a, simd::float4 b) {
  return nearf(a.x, b.x) && nearf(a.y, b.y) && nearf(a.z, b.z) && nearf(a.w, b.w);
}

}  // namespace

// ── SetPointLight value currency ─────────────────────────────────────────────────────────────────────
// TiXL SetPointLight.cs:17-21: new PointLight(Position, Intensity, Color, Range, Decay). Probe OFF the
// op's all-zero input defaults (SetPointLight.cs:33-45) so every field carries a DISTINCT non-zero value
// — a resolver that drops or mis-orders any field flips RED. -bug swaps Intensity↔Range (a real
// input→field remap corruption; a want-flip could not distinguish the two scalars).
int runPointLightSelfTest(bool injectBug) {
  bool ok = true;
  const simd::float3 pos = simd::make_float3(2.0f, -3.0f, 5.0f);
  const float intensity = 7.0f;
  const simd::float4 col = simd::make_float4(0.25f, 0.5f, 0.75f, 1.0f);
  const float range = 11.0f;
  const float decay = 3.0f;  // OFF the constructor's =2 default AND the input-slot 0 default → tests the pass-through

  SwPointLight l = injectBug ? swResolvePointLight(pos, /*intensity↔range swap*/ range, col, intensity, decay)
                             : swResolvePointLight(pos, intensity, col, range, decay);

  // Expected = the .cs constructor field assignment (SetPointLight.cs:17-21), impl-independent.
  ok = ok && near3(l.position, pos);
  ok = ok && nearf(l.intensity, intensity);
  ok = ok && near4(l.color, col);
  ok = ok && nearf(l.range, range);
  ok = ok && nearf(l.decay, decay);
  std::printf("[selftest-pointlight] pos=(%.1f,%.1f,%.1f) I=%.1f(want %.1f) col=(%.2f,%.2f,%.2f,%.2f) "
              "range=%.1f(want %.1f) decay=%.1f -> %s\n",
              l.position.x, l.position.y, l.position.z, l.intensity, intensity, l.color.x, l.color.y,
              l.color.z, l.color.w, l.range, range, l.decay, ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// ── SetFog value currency ────────────────────────────────────────────────────────────────────────────
// TiXL SetFog.cs:20-25: new FogParameters { Bias, Distance, Color }. Probe OFF the op's zero input
// defaults. ALSO assert the AMBIENT default (FogSettings.cs:15-20 = {(0,0,0,1),10000,2}) is DISTINCT
// from what SetFog produces at its input defaults ({0,0,0} ) — the P2-identity guard (testing SetFog only
// at 10000/2 would never catch a resolver that ignored its inputs and returned the ambient buffer).
// -bug swaps Distance↔Bias (real input→field corruption).
int runFogSelfTest(bool injectBug) {
  bool ok = true;
  const simd::float4 col = simd::make_float4(0.1f, 0.2f, 0.3f, 0.4f);
  const float distance = 250.0f;
  const float bias = 1.5f;

  SwFogParameters f = injectBug ? swResolveFog(col, /*distance↔bias swap*/ bias, distance)
                                : swResolveFog(col, distance, bias);
  ok = ok && near4(f.color, col);
  ok = ok && nearf(f.distance, distance);
  ok = ok && nearf(f.bias, bias);

  // P2 guard: the resolver at INPUT defaults ({0,0,0}) must NOT equal the ambient default ({(0,0,0,1),10000,2}).
  SwFogParameters atInputDefaults = swResolveFog(simd::make_float4(0, 0, 0, 0), 0.0f, 0.0f);
  SwFogParameters ambient = swFogAmbientDefault();
  ok = ok && !(nearf(atInputDefaults.distance, ambient.distance) && nearf(atInputDefaults.bias, ambient.bias));
  // And the ambient default itself matches FogSettings.cs:15-20 (independent value anchor).
  ok = ok && nearf(ambient.distance, 10000.0f) && nearf(ambient.bias, 2.0f) &&
       near4(ambient.color, simd::make_float4(0, 0, 0, 1));

  std::printf("[selftest-fog] col=(%.1f,%.1f,%.1f,%.1f) distance=%.1f(want %.1f) bias=%.1f(want %.1f) "
              "ambient(10000/2) distinct-from-input-default -> %s\n",
              f.color.x, f.color.y, f.color.z, f.color.w, f.distance, distance, f.bias, bias,
              ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// ── SetMaterial value currency (PbrParameters numeric slice) ─────────────────────────────────────────
// TiXL SetMaterial.cs:34-38: Parameters.{BaseColor,EmissiveColor,Roughness,Specular,Metal} = inputs.
// Probe OFF the fresh-material defaults (PbrMaterial.cs:83-88 = {(1,1,1,1),(0,0,0,1),0.5,10,0}) so a
// resolver that ignored its inputs and returned the default struct flips RED. -bug swaps
// Roughness↔Specular (real input→field corruption; 0.5 vs 10 are far apart → unambiguous bite).
int runPbrMaterialSelfTest(bool injectBug) {
  bool ok = true;
  const simd::float4 baseCol = simd::make_float4(0.2f, 0.4f, 0.6f, 0.8f);
  const simd::float4 emisCol = simd::make_float4(0.9f, 0.1f, 0.05f, 1.0f);
  const float roughness = 0.3f;   // OFF the 0.5 default
  const float specular = 25.0f;   // OFF the 10 default
  const float metal = 0.7f;       // OFF the 0 default

  SwPbrParameters p = injectBug
      ? swResolvePbrParameters(baseCol, emisCol, /*roughness↔specular swap*/ specular, roughness, metal)
      : swResolvePbrParameters(baseCol, emisCol, roughness, specular, metal);
  ok = ok && near4(p.baseColor, baseCol);
  ok = ok && near4(p.emissiveColor, emisCol);
  ok = ok && nearf(p.roughness, roughness);
  ok = ok && nearf(p.specular, specular);
  ok = ok && nearf(p.metal, metal);

  // Fresh-material default anchor (PbrMaterial.cs:83-88) — independent value, and DISTINCT from the probe.
  SwPbrParameters def;
  ok = ok && near4(def.baseColor, simd::make_float4(1, 1, 1, 1));
  ok = ok && near4(def.emissiveColor, simd::make_float4(0, 0, 0, 1));
  ok = ok && nearf(def.roughness, 0.5f) && nearf(def.specular, 10.0f) && nearf(def.metal, 0.0f);

  std::printf("[selftest-pbrmaterial] base=(%.1f,%.1f,%.1f,%.1f) rough=%.2f(want %.2f) spec=%.1f(want %.1f) "
              "metal=%.1f default(0.5/10/0) anchored -> %s\n",
              p.baseColor.x, p.baseColor.y, p.baseColor.z, p.baseColor.w, p.roughness, roughness,
              p.specular, specular, p.metal, ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
