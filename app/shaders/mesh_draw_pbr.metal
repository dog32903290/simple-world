// DrawMeshPbr — the LIT 3D mesh (DrawKind::MeshPbr). TiXL authority: DrawMesh.t3 → mesh-Draw.hlsl
// (vsMain + psMain) + shared/pbr.hlsl (the Cook-Torrance BRDF) + shared/pbr-render.hlsl (ComputePbr).
//
// SCOPE (minimal-viable slice, named boundary): the DIRECT-lighting Cook-Torrance path only —
//   Lambertian diffuse + Cook-Torrance microfacet specular over the scoped point lights (pbr.hlsl:47-71).
// DEFERRED to the NEXT battle (fork-pbr-ibl / fork-pbr-textures — see mesh_pbr_params.h):
//   • IBL / ambient (pbr-render.hlsl:74-103) — needs PrefilteredSpecular cubemap + BRDFLookup LUT textures.
//     ambientLighting is dropped (=0); litColor = directLighting only.
//   • ALL texture maps (albedo/normal/RSMO/emissive) — folded to their no-map defaults, exactly like
//     DrawMeshUnlit folds its white t2: albedo=white (BaseColorMap default), N=the interpolated vertex
//     normal (flat default normal map → tangent (0,0,1) → TBN·(0,0,1) = geometric normal), RSMO=0
//     (frag.Metalness=saturate(0+Metal)=saturate(Metal); frag.Roughness=Roughness; Occlusion feeds only
//     the dropped ambient term so it is irrelevant), EmissiveColorMap=1 (→ + EmissiveColor.rgb).
//   • SpecularAA (mesh-Draw.hlsl:222) — SpecularAA default 0 → sqrt(R²+0)=R → Roughness verbatim.
//   • UseFlatShading, AlphaCutOff, GetField, SV_Target1 Normal output → default/off (single SV_Target0).
//
// MATRIX CONVENTION (locked, runtime/field_camera.h): every matrix is host-packed ROW-MAJOR (m[r*4+c]);
// the VS does the row-vector multiply BY HAND (mul4row) = mul(v, M) in HLSL. NO transpose, NO float4x4
// (avoids MSL column-major reinterpret). Byte-identical to mesh_draw_unlit.metal's mul4row.
//   mesh-Draw.hlsl:98  posInClipSpace = mul(posInObject, ObjectToClipSpace)   → mul4row(objectToClipSpace)
//   mesh-Draw.hlsl:115 worldPosition  = mul(posInObject, ObjectToWorld).xyz   → we build worldPos DIRECTLY:
//     at the minimal path ObjectToWorld = identity (a mesh's own O2W; the SRT stack is a deferred parent
//     Transform op), so worldPosition = vertex.Position verbatim. (The executor composes the mesh at
//     world identity; the camera lives in objectToClipSpace / objectToCamera / cameraToWorld.)
//   mesh-Draw.hlsl:120 posInCamera    = mul(posInObject, ObjectToCamera)      → mul4row(objectToCamera) (fog)
//   mesh-Draw.hlsl:227 eyePosition    = mul((0,0,0,1), CameraToWorld)         → cameraToWorld row 3 (translation)
//
// applyTransform==0 (the drop-mul golden tooth) skips the clip-space mul (raw object position) so the
// render golden can prove the projection is load-bearing (mis-project bite, same as DrawMeshUnlit Tooth A).
#include <metal_stdlib>
#include "mesh_pbr_params.h"  // MeshPbrParams + MESHPBR_* bindings + MeshPbrLight
#include "sw_mesh.h"          // SwVertex (80B) / SwTriIndex (12B) — MSL-shareable (packed_float3)
using namespace metal;

#ifndef SW_PBR_PI
#define SW_PBR_PI 3.141592f        // pbr.hlsl:11
#endif
#ifndef SW_PBR_EPS
#define SW_PBR_EPS 0.00001f        // pbr.hlsl:15 Epsilon
#endif
#define SW_PBR_FDIELECTRIC 0.04f   // pbr.hlsl:31 Fdielectric

struct MeshPbrVSOut {
  float4 position [[position]];
  float3 worldPosition;  // mesh-Draw.hlsl:115 (identity O2W → vertex.Position)
  float3 normal;         // the interpolated vertex normal (no normal map → frag.N; mesh-Draw.hlsl default)
  float3 colorRGB;       // vertex.ColorRgb (frag.albedo.rgb *= colorRGB; mesh-Draw.hlsl:218)
  float  fog;            // mesh-Draw.hlsl:117-123 (posInCamera.z / FogDistance)^FogBias
};

// mul4row(M_rowmajor, v) = v·M for a ROW-MAJOR float[16]: (v·M)_j = Σ_i v_i · M[i*4+j]. Byte-identical to
// mesh_draw_unlit.metal's mul4row (the convention --selftest-field-camera pins). `constant` (cbuffer array).
static float4 mul4row(constant float M[16], float4 v) {
  float4 o;
  for (int j = 0; j < 4; ++j) {
    float s = 0.0f;
    for (int i = 0; i < 4; ++i) s += v[i] * M[i * 4 + j];
    o[j] = s;
  }
  return o;
}

// ── Cook-Torrance BRDF pieces (pbr.hlsl:35-61, VERBATIM) ──────────────────────────────────────────────
// GGX/Towbridge-Reitz NDF, Disney alpha=roughness² reparam (pbr.hlsl:35-42).
static float ndfGGX(float cosLh, float roughness) {
  float alpha = roughness * roughness;
  float alphaSq = alpha * alpha;
  float denom = (cosLh * cosLh) * (alphaSq - 1.0f) + 1.0f;
  return alphaSq / (SW_PBR_PI * denom * denom);
}
// Single Schlick-GGX term (pbr.hlsl:45-48).
static float gaSchlickG1(float cosTheta, float k) {
  return cosTheta / (cosTheta * (1.0f - k) + k);
}
// Smith's method geometric attenuation, Epic k=(r+1)²/8 analytic-light remap (pbr.hlsl:51-56).
static float gaSchlickGGX(float cosLi, float cosLo, float roughness) {
  float r = roughness + 1.0f;
  float k = (r * r) / 8.0f;
  return gaSchlickG1(cosLi, k) * gaSchlickG1(cosLo, k);
}
// Schlick Fresnel (pbr.hlsl:59-62).
static float3 fresnelSchlick(float3 F0, float cosTheta) {
  return F0 + (1.0f - F0) * pow(1.0f - cosTheta, 5.0f);
}

vertex MeshPbrVSOut mesh_draw_pbr_vs(uint vid [[vertex_id]],
                                     device const SwVertex* PbrVertices [[buffer(MESHPBR_PbrVertices)]],
                                     device const SwTriIndex* FaceIndices [[buffer(MESHPBR_FaceIndices)]],
                                     constant MeshPbrParams& P [[buffer(MESHPBR_Params)]]) {
  MeshPbrVSOut o;
  // mesh-Draw.hlsl:91-94 VERBATIM index math (same as mesh-DrawUnlit).
  int faceIndex = (int)vid / 3;
  int faceVertexIndex = (int)vid % 3;
  SwTriIndex tri = FaceIndices[faceIndex];
  int vIndex = (faceVertexIndex == 0) ? tri.X : (faceVertexIndex == 1) ? tri.Y : tri.Z;
  SwVertex vtx = PbrVertices[vIndex];  // NOT `vertex` (MSL reserved)

  float4 posInObject = float4(vtx.Position.x, vtx.Position.y, vtx.Position.z, 1.0f);
  // mesh-Draw.hlsl:98. applyTransform==0 = drop-mul tooth (raw object pos).
  o.position = (P.applyTransform != 0u) ? mul4row(P.objectToClipSpace, posInObject) : posInObject;
  // worldPosition (mesh-Draw.hlsl:115): identity O2W in the minimal path → vertex.Position verbatim.
  o.worldPosition = float3(vtx.Position.x, vtx.Position.y, vtx.Position.z);
  // Normal: no normal map → the interpolated vertex normal (mesh-Draw.hlsl:186-187 default = normalize(N)·TBN;
  // at identity O2W the TBN world-transform is identity so N = vertex.Normal). Normalized in the PS.
  o.normal = float3(vtx.Normal.x, vtx.Normal.y, vtx.Normal.z);
  o.colorRGB = float3(vtx.ColorRgb.x, vtx.ColorRgb.y, vtx.ColorRgb.z);
  // Fog (mesh-Draw.hlsl:117-123): fog = pow(saturate(-posInCamera.z / FogDistance), FogBias); FogDistance<=0 → 0.
  if (P.fogDistance > 0.0f) {
    float4 posInCamera = mul4row(P.objectToCamera, posInObject);
    o.fog = pow(saturate(-posInCamera.z / P.fogDistance), P.fogBias);
  } else {
    o.fog = 0.0f;
  }
  return o;
}

// psMain (mesh-Draw.hlsl:208-247) + ComputePbr (pbr-render.hlsl:27-113), DIRECT-lighting subset.
fragment float4 mesh_draw_pbr_fs(MeshPbrVSOut pin [[stage_in]],
                                 constant MeshPbrParams& P [[buffer(MESHPBR_Params)]]) {
  // ── psMain frag setup (mesh-Draw.hlsl:215-228), no-texture defaults folded ──
  float metalness = saturate(P.metal);                    // saturate(RSMO.y(=0) + Metal); :215
  // albedo = BaseColorMap.Sample(=white) ; .rgb *= colorRGB (:217-218). Alpha from the white default = 1.
  float4 albedo = float4(pin.colorRGB, 1.0f);
  float roughness = saturate(P.roughness);                // AdjustRoughnessForSpecularAA(RSMO.x(=0)+R, SpecularAA=0)=R; :222
  float3 N = normalize(pin.normal);                       // ComputeNormal default branch → interpolated normal; :220
  float3 worldPosition = pin.worldPosition;               // :225

  // eyePosition = mul((0,0,0,1), CameraToWorld) → the CameraToWorld translation (row 3 of a row-major mat).
  // (0,0,0,1)·M = M's 4th row = { M[12], M[13], M[14], M[15] }. Lo = normalize(eye - worldPos); :227-228.
  float3 eyePosition = float3(P.cameraToWorld[12], P.cameraToWorld[13], P.cameraToWorld[14]);
  float3 Lo = normalize(eyePosition - worldPosition);     // the outgoing/view vector V

  // ── ComputePbr direct lighting (pbr-render.hlsl:29-71), VERBATIM ──
  float3 V = Lo;
  float cosLo = abs(dot(N, Lo));                          // :34
  float3 F0 = mix(float3(SW_PBR_FDIELECTRIC), albedo.rgb, metalness);  // lerp(Fdielectric, albedo, metal); :43

  float3 directLighting = float3(0.0f);
  uint lightCount = min(P.activeLightCount, (uint)MESH_PBR_MAX_LIGHTS);
  for (uint i = 0; i < lightCount; ++i) {                 // :47
    float3 lightPos = float3(P.lights[i].position);
    float3 Lvec = lightPos - worldPosition;               // :49
    float dist = length(Lvec);                            // :50
    float3 L = Lvec / max(dist, 1e-4f);                   // :51 normalize once
    // intensity = I / (pow(dist/range, decay) + 1); :53
    float intensity = P.lights[i].intensity /
                      (pow(dist / P.lights[i].range, P.lights[i].decay) + 1.0f);
    float3 Lradiance = P.lights[i].color.rgb * intensity; // :55

    float NdotV = saturate(dot(N, V));                    // :57
    float NdotL = saturate(dot(N, L));                    // :58
    float3 H = normalize(L + V);                          // :59
    float NdotH = saturate(dot(N, H));                    // :60

    float3 F = fresnelSchlick(F0, saturate(dot(H, V)));   // :62
    float D = ndfGGX(NdotH, roughness);                   // :63
    float G = gaSchlickGGX(NdotL, NdotV, roughness);      // :64

    float3 kd = mix(1.0f - F, float3(0.0f), metalness);   // lerp(1-F, 0, metal); :66
    float3 diffuseBRDF = kd * albedo.rgb;                 // :67
    float3 specularBRDF = ((F * D * G) / max(SW_PBR_EPS, 4.0f * NdotL * NdotV)) * P.specular;  // :68
    directLighting += (diffuseBRDF + specularBRDF) * Lradiance * NdotL;  // :70
  }

  // Ambient/IBL (pbr-render.hlsl:74-103) DROPPED (needs cubemap+LUT) → ambientLighting = 0. NAMED FORK.
  // Final color (pbr-render.hlsl:106-110): litColor = (direct + 0) * BaseColor * Color; + EmissiveColor
  // (EmissiveColorMap default = 1 → + EmissiveColor.rgb); fog lerp; alpha *= albedo.a.
  float4 litColor = float4(directLighting, 1.0f) * P.baseColor * P.color;         // :106
  litColor += float4(P.emissiveColor.rgb, 0.0f);                                  // :108 (EmissiveColorMap=1)
  litColor.rgb = mix(litColor.rgb, P.fogColor.rgb, pin.fog * P.fogColor.a);       // :109
  litColor.a *= albedo.a;                                                          // :110
  return litColor;
}
