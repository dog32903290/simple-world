// runtime/sw_pbr_scope — bodies for the PBR context-stack scope (decls + full rationale in sw_pbr_scope.h).
// Verbatim shape of point_ops_camera.cpp's LiveCameraScope + point_ops_setvarcmd.cpp's LiveCtxVarScope:
// thread_local live pointers + RAII save/restore, plus the param resolvers the two cook legs share.
#include "runtime/sw_pbr_scope.h"

namespace sw {
namespace {

// ── vec/scalar param helpers (same .x/.y/.z/.w suffix convention as cookVecN / resolveActiveCamera) ──
float paramOr(const std::map<std::string, float>& p, const char* key, float def) {
  auto it = p.find(key);
  return it != p.end() ? it->second : def;
}
simd::float4 vec4Or(const std::map<std::string, float>& p, const char* base, simd::float4 def) {
  std::string b(base);
  return simd::make_float4(paramOr(p, (b + ".x").c_str(), def.x), paramOr(p, (b + ".y").c_str(), def.y),
                           paramOr(p, (b + ".z").c_str(), def.z), paramOr(p, (b + ".w").c_str(), def.w));
}
simd::float3 vec3Or(const std::map<std::string, float>& p, const char* base, simd::float3 def) {
  std::string b(base);
  return simd::make_float3(paramOr(p, (b + ".x").c_str(), def.x), paramOr(p, (b + ".y").c_str(), def.y),
                           paramOr(p, (b + ".z").c_str(), def.z));
}

// thread_local live scopes (mirror liveActiveCamera_ / liveCtxVars_). nullptr/empty = no scope active.
thread_local const ActiveMaterial* tlMaterial = nullptr;
thread_local LivePointLights* tlLights = nullptr;
thread_local const ActiveFog* tlFog = nullptr;
thread_local std::map<std::string, ActiveMaterial>* tlLibrary = nullptr;  // DefineMaterials by-name library

}  // namespace

// ─────────────────────────── material scope ───────────────────────────
bool isMaterialScopeWriter(const std::string& opType) {
  return opType == "SetMaterial" || opType == "UseMaterial";
}

ActiveMaterial resolveActiveMaterial(const std::map<std::string, float>& params, const std::string& name) {
  ActiveMaterial m;
  m.active = true;
  m.name = name;
  // SetMaterial.cs:34-38 field writes; the resolver is swResolvePbrParameters (sw_pbr_material.h:40). BaseColor
  // input default (1,1,1,1) = the PbrMaterial default; scalar inputs default 0 (SetMaterial InputSlot<float>()).
  simd::float4 baseColor = vec4Or(params, "BaseColor", simd::make_float4(1, 1, 1, 1));
  simd::float4 emissive = vec4Or(params, "EmissiveColor", simd::make_float4(0, 0, 0, 1));
  float roughness = paramOr(params, "Roughness", 0.0f);
  float specular = paramOr(params, "Specular", 0.0f);
  float metal = paramOr(params, "Metal", 0.0f);
  m.params = swResolvePbrParameters(baseColor, emissive, roughness, specular, metal);
  return m;
}

LiveMaterialScope::LiveMaterialScope(const ActiveMaterial& mat) : prev_(tlMaterial), engaged_(mat.active) {
  if (engaged_) tlMaterial = &mat;  // innermost wins; an inactive material leaves the prior live one
}
LiveMaterialScope::~LiveMaterialScope() {
  if (engaged_) tlMaterial = prev_;
}
const ActiveMaterial* liveActiveMaterial() { return tlMaterial; }

// ─────────────────────────── light scope ───────────────────────────
bool isPointLightScopeWriter(const std::string& opType) { return opType == "SetPointLight"; }

SwPointLight resolvePointLightFromParams(const std::map<std::string, float>& params) {
  // SetPointLight.cs:17-21: new PointLight(Position, Intensity, Color, Range, Decay). Input-slot defaults 0.
  simd::float3 position = vec3Or(params, "Position", simd::make_float3(0, 0, 0));
  float intensity = paramOr(params, "Intensity", 0.0f);
  simd::float4 color = vec4Or(params, "Color", simd::make_float4(0, 0, 0, 0));
  float range = paramOr(params, "Range", 0.0f);
  float decay = paramOr(params, "Decay", 0.0f);  // op passes Decay explicitly → input default 0 (not ctor =2)
  return swResolvePointLight(position, intensity, color, range, decay);
}

LivePointLightScope::LivePointLightScope(const SwPointLight& light, bool active) : engaged_(active) {
  if (!engaged_) return;
  if (!tlLights) tlLights = new LivePointLights();  // first push this thread opens the stack
  tlLights->lights.push_back(light);
}
LivePointLightScope::~LivePointLightScope() {
  if (!engaged_ || !tlLights) return;
  if (!tlLights->lights.empty()) tlLights->lights.pop_back();
  if (tlLights->lights.empty()) { delete tlLights; tlLights = nullptr; }  // stack empty → no scope active
}
const LivePointLights* liveActiveLights() {
  return (tlLights && !tlLights->lights.empty()) ? tlLights : nullptr;
}

// ─────────────────────────── fog scope ───────────────────────────
bool isFogScopeWriter(const std::string& opType) { return opType == "SetFog"; }

ActiveFog resolveActiveFog(const std::map<std::string, float>& params) {
  ActiveFog f;
  f.active = true;
  // SetFog.cs:20-25 field writes; resolver swResolveFog (sw_fog.h:38). Input-slot defaults all 0 (sw_fog.h:16-19).
  simd::float4 color = vec4Or(params, "Color", simd::make_float4(0, 0, 0, 0));
  float distance = paramOr(params, "Distance", 0.0f);
  float bias = paramOr(params, "Bias", 0.0f);
  f.params = swResolveFog(color, distance, bias);
  return f;
}

LiveFogScope::LiveFogScope(const ActiveFog& fog) : prev_(tlFog), engaged_(fog.active) {
  if (engaged_) tlFog = &fog;
}
LiveFogScope::~LiveFogScope() {
  if (engaged_) tlFog = prev_;
}
const ActiveFog* liveActiveFog() { return tlFog; }

// ─────────────────────────── material library ───────────────────────────
MaterialLibraryScope::MaterialLibraryScope(const ActiveMaterial& mat, bool active)
    : name_(mat.name), engaged_(active && !mat.name.empty()) {
  if (!engaged_) return;
  if (!tlLibrary) tlLibrary = new std::map<std::string, ActiveMaterial>();
  (*tlLibrary)[name_] = mat;  // register by name for the enclosing SubGraph cook (DefineMaterials.cs:34)
}
MaterialLibraryScope::~MaterialLibraryScope() {
  if (!engaged_ || !tlLibrary) return;
  tlLibrary->erase(name_);  // DefineMaterials.cs:42 RemoveRange (un-register on subtree exit)
  if (tlLibrary->empty()) { delete tlLibrary; tlLibrary = nullptr; }
}
bool lookupDefinedMaterial(const std::string& name, ActiveMaterial& out) {
  if (!tlLibrary || name.empty()) return false;
  auto it = tlLibrary->find(name);
  if (it == tlLibrary->end()) return false;
  out = it->second;
  return true;
}

// ─────────────────────────── cc-fill bridge (single source, both cook legs) ───────────────────────────
void fillCmdPbrFromScope(bool& outHasMaterial, SwPbrParameters& outMaterial, bool& outHasFog,
                         SwFogParameters& outFog, int& outLightCount, SwPointLight* outLights) {
  if (const ActiveMaterial* m = liveActiveMaterial()) { outHasMaterial = true; outMaterial = m->params; }
  if (const ActiveFog* f = liveActiveFog()) { outHasFog = true; outFog = f->params; }
  outLightCount = 0;
  if (const LivePointLights* l = liveActiveLights()) {
    int n = (int)l->lights.size();
    if (n > kMaxPbrLights) n = kMaxPbrLights;
    for (int i = 0; i < n; ++i) outLights[i] = l->lights[(size_t)i];
    outLightCount = n;
  }
}


// two-leg SHARED resolve (see header). Verbatim consolidation of the identical flat/resident blocks.
PbrScopeResolve resolvePbrScopes(const std::string& opType,
                                 const std::map<std::string, std::string>& strChannel,
                                 const std::map<std::string, float>& params) {
  PbrScopeResolve r;
  if (!materialScopeBugSkipPush() && isMaterialScopeWriter(opType)) {
    std::string matName;  // SetMaterial.MaterialId / UseMaterial.MaterialReference (String channel)
    auto sit = strChannel.find(opType == "UseMaterial" ? "MaterialReference" : "MaterialId");
    if (sit != strChannel.end()) matName = sit->second;
    if (opType == "UseMaterial") {
      if (!lookupDefinedMaterial(matName, r.mat)) r.mat.active = false;  // invalid ref → prior (UseMaterial.cs:19)
    } else {
      r.mat = resolveActiveMaterial(params, matName);
    }
  }
  r.lightActive = !pointLightScopeBugSkipPush() && isPointLightScopeWriter(opType);
  if (r.lightActive) r.light = resolvePointLightFromParams(params);
  if (!fogScopeBugSkipPush() && isFogScopeWriter(opType)) r.fog = resolveActiveFog(params);
  if (opType == "DefineMaterials") {
    auto sit = strChannel.find("MaterialId");
    if (sit != strChannel.end()) r.define = resolveActiveMaterial(params, sit->second);
  }
  return r;
}

// ─────────────────────────── -bug driver flags ───────────────────────────
bool& materialScopeBugSkipPush() { static bool f = false; return f; }
bool& pointLightScopeBugSkipPush() { static bool f = false; return f; }
bool& fogScopeBugSkipPush() { static bool f = false; return f; }

}  // namespace sw
