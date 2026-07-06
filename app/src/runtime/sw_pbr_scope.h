// runtime/sw_pbr_scope — the PBR render-pass CONTEXT-STACK bridge: the material / point-light / fog
// scope the Command-rail SetMaterial / SetPointLight / SetFog / UseMaterial / DefineMaterials ops push
// around their SubGraph cook, and that DrawMeshPbr READS to stamp the lit draw's currency.
//
// TiXL ground truth: TiXL threads three context stacks through EvaluationContext for the PBR draw —
//   context.PbrMaterial + context.Materials      (SetMaterial.cs:48-65 push/pop; UseMaterial.cs:21-35;
//                                                  DefineMaterials.cs:22-42 — the material-library define)
//   context.PointLights                          (SetPointLight.cs:16-26 stack push/pop)
//   context.FogParameters                        (SetFog.cs:27-33 scoped const-buffer)
// mesh-Draw.hlsl's cbuffers b2/b3/b4 (FogParams / PointLights / PbrParams) are filled from those stacks.
//
// MECHANISM — a verbatim copy of the C1 LiveCameraScope pattern (point_ops_camera_scope.h): a set of
// thread_local "live" scopes the Command op's cook-driver branch SETS around its SubGraph cook, that the
// DrawMeshPbr cook (or a Command op cooked inside the scope) READS. The scoped currency lives for exactly
// the SubGraph's cook lifetime, then pops (innermost wins = TiXL push/pop). Empty outside any scope →
// DrawMeshPbr uses the fog AMBIENT default + zero lights + the default PbrMaterial (faithful: an
// un-lit-scoped mesh renders with the same defaults TiXL's fresh context carries).
//
// ★FLAT-RESIDENT MIRROR HARD GATE (the S2c blood lesson): the scope MUST be set on BOTH cook legs'
// Command branch (point_graph_command_cook.cpp flat + point_graph_resident_command_cook.cpp
// resident=production). A resident-only miss = a prod-only unlit black-hole. The resolvers + scope guards
// here are the SINGLE shared code path both legs call — only the scope-SET site is mirrored, never forked.
//
// runtime leaf: pure CPU + the sw_pbr_material/point_light/fog value types. No UI, no upward deps. Own
// tiny header (NOT render_command.h, which is at its 400-line cap) so the two cook drivers + DrawMeshPbr
// call it without bloating the currency header.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "runtime/sw_fog.h"            // SwFogParameters + swResolveFog/swFogAmbientDefault
#include "runtime/sw_pbr_material.h"   // SwPbrParameters + swResolvePbrParameters
#include "runtime/sw_point_light.h"    // SwPointLight + swResolvePointLight

namespace sw {

// The maximum simultaneous lights the PBR cbuffer carries. TiXL: mesh-Draw.hlsl b3 `PointLight Lights[8]`
// — a fixed 8-slot array + ActiveLightCount. SetPointLight stacks lights; the draw uploads up to 8.
constexpr int kMaxPbrLights = 8;

// ─────────────────────────── material scope (context.PbrMaterial + context.Materials) ───────────────────────────
// The active PBR material (SetMaterial/UseMaterial push context.PbrMaterial; DefineMaterials/SetMaterial
// add to context.Materials — the by-name library). A DrawMeshPbr reads liveActiveMaterial() → its
// SwPbrParameters feed the b4 PbrParams cbuffer. hasMaterial=false → the TiXL _defaultParameters
// (SwPbrParameters default ctor = BaseColor.One / Roughness .5 / Specular 10 / Metal 0, PbrMaterial.cs:83-88).
struct ActiveMaterial {
  bool active = false;
  std::string name;             // SetMaterial.MaterialId (SetMaterial.cs:55) / gltf material name; "" = unnamed
  SwPbrParameters params;       // the numeric half uploaded to b4 (swResolvePbrParameters output)
};

// True iff opType is a Command-rail material-scope WRITER (SetMaterial / UseMaterial). The cook drivers'
// Command branch consults this to decide whether to set the active-material scope around the SubGraph cook.
bool isMaterialScopeWriter(const std::string& opType);

// Resolve a SetMaterial op's RESOLVED Float/Vec params (the SAME map cookDrawMeshPbr reads via cookVecN) into
// an ActiveMaterial (active=true). Vec4 params use the .x/.y/.z/.w suffix convention (BaseColor/EmissiveColor).
// Defaults = SetMaterial input-slot defaults (BaseColor input default (1,1,1,1) per PbrMaterial default; the
// scalar inputs default 0 — SetMaterial.cs InputSlot<float>()). `name` from the String MaterialId param.
ActiveMaterial resolveActiveMaterial(const std::map<std::string, float>& params, const std::string& name);

// RAII guard — construct around the SubGraph cook, destroy after. Nests correctly (innermost material wins;
// TiXL SetMaterial.cs:48-65 previousMaterial save/restore). An INACTIVE material leaves the prior live one.
struct LiveMaterialScope {
  explicit LiveMaterialScope(const ActiveMaterial& mat);
  ~LiveMaterialScope();
  LiveMaterialScope(const LiveMaterialScope&) = delete;
  LiveMaterialScope& operator=(const LiveMaterialScope&) = delete;
 private:
  const ActiveMaterial* prev_;
  bool engaged_;
};

// The live ACTIVE material, or nullptr when no material scope is active. Consulted by cookDrawMeshPbr to fill
// the b4 PbrParams cbuffer from the wired SetMaterial instead of the default.
const ActiveMaterial* liveActiveMaterial();

// ─────────────────────────── light scope (context.PointLights stack) ───────────────────────────
// The live point-light STACK (TiXL EvaluationContext.PointLights). SetPointLight pushes ONE light for its
// SubGraph (SetPointLight.cs:16-26); nested SetPointLights accumulate (a deeper op adds another light to
// the same ambient stack). DrawMeshPbr reads liveActiveLights() → the first kMaxPbrLights fill b3 Lights[].
struct LivePointLights {
  std::vector<SwPointLight> lights;  // the stack, outermost-first (draw uploads up to kMaxPbrLights)
};

// True iff opType is a Command-rail light-scope WRITER (SetPointLight). Consulted by the cook drivers.
bool isPointLightScopeWriter(const std::string& opType);

// Resolve a SetPointLight op's RESOLVED params into a SwPointLight (swResolvePointLight; SetPointLight.cs:17-21).
// Vec3/Vec4 use .x/.y/.z(/.w); defaults = SetPointLight input-slot defaults (all 0, incl. Decay — the op
// passes Decay explicitly so the input default 0 wins over the ctor =2; sw_point_light.h:36-48).
SwPointLight resolvePointLightFromParams(const std::map<std::string, float>& params);

// RAII guard — PUSH one light onto the live stack around the SubGraph cook, POP after. Nests: an inner
// SetPointLight adds a second light the inner subtree sees; the outer sees only its own after the inner pops.
struct LivePointLightScope {
  explicit LivePointLightScope(const SwPointLight& light, bool active);
  ~LivePointLightScope();
  LivePointLightScope(const LivePointLightScope&) = delete;
  LivePointLightScope& operator=(const LivePointLightScope&) = delete;
 private:
  bool engaged_;
};

// The live point-light stack, or nullptr when no SetPointLight scope is active. Read by cookDrawMeshPbr.
const LivePointLights* liveActiveLights();

// ─────────────────────────── fog scope (context.FogParameters) ───────────────────────────
// The live fog params (TiXL context.FogParameters). SetFog pushes ONE FogParameters for its SubGraph
// (SetFog.cs:27-33). DrawMeshPbr reads liveActiveFog() → b2 FogParams; nullptr → swFogAmbientDefault()
// (the FogSettings.DefaultSettingsBuffer 10000/2 ambient — NOT what SetFog at its input defaults produces).
struct ActiveFog {
  bool active = false;
  SwFogParameters params;
};

// True iff opType is a Command-rail fog-scope WRITER (SetFog). Consulted by the cook drivers.
bool isFogScopeWriter(const std::string& opType);

// Resolve a SetFog op's RESOLVED params into SwFogParameters (swResolveFog; SetFog.cs:20-25). Color is a Vec4
// (.x/.y/.z/.w); Distance/Bias are Float. Defaults = SetFog input-slot defaults (all 0 — sw_fog.h:16-19).
ActiveFog resolveActiveFog(const std::map<std::string, float>& params);

// RAII guard — set the live fog around the SubGraph cook, restore after. Nests (innermost fog wins). An
// INACTIVE fog leaves the prior live one.
struct LiveFogScope {
  explicit LiveFogScope(const ActiveFog& fog);
  ~LiveFogScope();
  LiveFogScope(const LiveFogScope&) = delete;
  LiveFogScope& operator=(const LiveFogScope&) = delete;
 private:
  const ActiveFog* prev_;
  bool engaged_;
};

// The live fog, or nullptr when no SetFog scope is active. Read by cookDrawMeshPbr (nullptr → ambient default).
const ActiveFog* liveActiveFog();

// ─────────────────────────── material library (context.Materials by-name; UseMaterial reference) ───────────────────────────
// DefineMaterials collects a set of named materials into a process-scoped by-name library its SubGraph can
// reference; UseMaterial(MaterialReference=name) pushes a referenced material as the active one. This is the
// v1 slice of TiXL's Slot<PbrMaterial> reference wiring (the Reference output SetMaterial.cs:12-13 / the
// PbrMaterial-typed MaterialReference input UseMaterial.cs:42) done by NAME rather than a live object handle
// (sw has no PbrMaterial slot currency yet — a named fork, honest boundary). registerDefinedMaterial adds one;
// lookupDefinedMaterial resolves a name UseMaterial references. The library is per-cook (cleared each frame by
// the DefineMaterials scope guard) so it does not leak across frames.
struct MaterialLibraryScope {
  // Register `mat` under its name for the enclosing SubGraph cook; unregister on destruction (DefineMaterials.cs
  // RemoveRange). active=false → a no-op (an unnamed / null material is skipped, DefineMaterials.cs:30-31).
  MaterialLibraryScope(const ActiveMaterial& mat, bool active);
  ~MaterialLibraryScope();
  MaterialLibraryScope(const MaterialLibraryScope&) = delete;
  MaterialLibraryScope& operator=(const MaterialLibraryScope&) = delete;
 private:
  std::string name_;
  bool engaged_;
};

// Resolve a name against the live material library. Returns true + fills `out` on a hit; false on a miss
// (UseMaterial.cs:19-20 invalid-reference: the op leaves the prior material). Read by the UseMaterial scope-set.
bool lookupDefinedMaterial(const std::string& name, ActiveMaterial& out);

// ─────────────────────────── two-leg SHARED scope resolve (S2c: ONE resolve site) ───────────────────────────
// Resolve the four PBR scope objects from a command node's op type + String channel + resolved Float params.
// BOTH cook legs call THIS (flat: type/strParams; resident: opType/strInputs) so the resolve can never fork.
struct PbrScopeResolve {
  ActiveMaterial mat, define;
  SwPointLight light;
  bool lightActive = false;
  ActiveFog fog;
};
PbrScopeResolve resolvePbrScopes(const std::string& opType,
                                 const std::map<std::string, std::string>& strChannel,
                                 const std::map<std::string, float>& params);

// ─────────────────────────── cc-fill bridge (the SINGLE source both cook legs read) ───────────────────────────
// Read the live material / light / fog scope into the DrawMeshPbr surfacing fields. BOTH cook legs call THIS
// (not the thread_local getters piecemeal) so the flat/resident fill can never fork (the S2c mirror gate).
// Takes primitives (not CmdCookCtx — this leaf must not depend on the cook-ctx header). `outLights` is a
// kMaxPbrLights-sized SwPointLight array; outLightCount is clamped to kMaxPbrLights.
void fillCmdPbrFromScope(bool& outHasMaterial, SwPbrParameters& outMaterial, bool& outHasFog,
                         SwFogParameters& outFog, int& outLightCount, SwPointLight* outLights);

// ─────────────────────────── -bug DRIVER flags (mirror of cameraScopeBugSkipPush) ───────────────────────────
// When true BOTH cook legs SKIP the corresponding scope set → DrawMeshPbr reads the default → the golden's
// lit-shading change disappears → RED on both flat + resident. OFF in production.
bool& materialScopeBugSkipPush();    // skip the material push → default gray material
bool& pointLightScopeBugSkipPush();  // skip the light push → zero lights → no direct lighting
bool& fogScopeBugSkipPush();         // skip the fog push → ambient default fog (no scoped fog effect)

}  // namespace sw
