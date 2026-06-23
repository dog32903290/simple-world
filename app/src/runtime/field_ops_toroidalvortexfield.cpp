// ToroidalVortexField — field/generate/vec3 VECTOR-field generator (zero-shared-file leaf on the field
// self-registration seam). Unlike the SDF generators (whose meaningful output is the scalar distance in
// f.w), this op emits a full float4 field: f.xyz = a swirl+radial VELOCITY contribution around a torus
// centerline, f.w = a decay weight in [0,1]. It is the lone clean unported field codegen leaf at this
// HEAD; every other unported field op needs an unbuilt seam (Image2D/HeightMap texture-into-field,
// point-buffer RepeatFieldAtPoints, gradient color, raymarch3D, or IGraphNodeOp custom material).
//
// TiXL authority: external/tixl/Operators/Lib/field/generate/vec3/ToroidalVortexField.cs (+ .t3 defaults).
//   AddDefinitions: registers TWO Globals helpers:
//     "fDecay"               — a standalone decay helper. *** NAMED FORK [fork-fDecay-dead] ***: TiXL
//                              registers fDecay but fToroidalVectorField NEVER calls it (it computes its
//                              own `decay` inline). It is dead code in TiXL too. Ported VERBATIM for byte
//                              parity of the assembled GLOBALS block — NOT pruned (a faithful clone emits
//                              the same shader text TiXL would). It compiles cleanly and is unreferenced.
//     "fToroidalVectorField" — the actual float4 field: phi=atan2(p.y,p.x); basis e_r/e_phi; closest point
//                              on the centerline ring; rho=length(offset); decay=saturate(1-pow(rho/range,
//                              decayK)); swirl=normalize(cross(e_phi,r))*swirlGain*decay; radial=
//                              (-r/rho)*radialGain*decay; return float4(swirl+radial, decay).
//   GetPreShaderCode (ToroidalVortexField.cs:GetPreShaderCode):
//     a = _axisCodes0[(int)_axis];   // { "zyx", "xzy", "xyz" }  (Axis enum X=0,Y=1,Z=2)
//     c.AppendCall($"f{c} = fToroidalVectorField(p{c}.{a} - {n}Center.{a}, {n}Radius, {n}Range,
//                   {n}SwirlGain, {n}RadialGain, {n}FallOffRate).{a}w;");
//   This writes the WHOLE f{c} (a float4), not just f{c}.w — the swizzle `.{a}w` re-orders the returned
//   vector into the chosen-axis frame and appends the decay scalar as .w. For the default Axis=Z the code
//   `a="xyz"` so `.{a}w` = `.xyzw` (identity); the field is authored in the natural xy-plane / z-up frame.
//   .t3 defaults: Center=(0,0,0), Radius=1.0, Range=1.0, SwirlGain=1.0, RadialGain=1.0, FallOffRate=2.0,
//   Axis=2 (Z).
//
// PORTABILITY (STEP-0, PASSED): pure codegen — only AddDefinitions globals + GetPreShaderCode. No texture
//   input, no point buffer, no gradient/color LUT, no raymarch, no camera, no custom IGraphNodeOp collect.
//   Rides the SAME field self-registration seam as every SDF leaf (FieldOp registrar -> fieldSpecSink +
//   fieldNodeFactories; assembleFieldMSL injects GLOBALS + FLOAT_PARAMS + the FIELD_CALL).
//
// PARAM ORDER PARITY: [GraphParam] reflection order = Center(vec3) -> Radius -> Range -> SwirlGain ->
//   RadialGain -> FallOffRate. Axis is an InputSlot<int> with MappedType (an enum) but is NOT a
//   [GraphParam] (RotateAxis/BendField precedent) -> a compile-time swizzle selector, NEVER packed.
//   Layout: Center=floats[0..2] (packed_float3 at offset 0, padForVec3 adds none) then 5 trailing scalars
//   Radius/Range/SwirlGain/RadialGain/FallOffRate = floats[3..7]; 8 floats total.
//
// HLSL->MSL forks (named):
//   [fork-fDecay-dead]   fDecay helper registered+ported verbatim though unreferenced (TiXL parity; see
//                        above). All math (atan2/cos/sin/cross/normalize/length/pow/saturate) is identical
//                        in HLSL and MSL — no math fork in either helper body.
//   [fork-param-prefix]  `{n}Name` -> `P.<prefix>Name` (the assembled FieldParams struct member), exactly
//                        like every SDF leaf. {n} = ShaderGraphNode.BuildNodeId = "<Type>_<shortId>_".
//   [fork-axis-selector] Axis enum -> compile-time _axisCodes0 swizzle (NOT packed), byte-verbatim
//                        { "zyx", "xzy", "xyz" } from the .cs. Default Z -> "xyz" -> identity swizzle.
//
// VERIFICATION CEILING (named, honest): the field 2D render template
//   (shaders/templates/field_render_template.metal) visualizes ONLY f.w into the RED channel
//   (`return float4(f.w, 0.0, 0.0, 1.0);`). For an SDF leaf f.w is the whole point (the distance). For
//   THIS op f.w is the DECAY scalar — fully closed-form and machine-verified by the golden below. The
//   VELOCITY (f.xyz) — the op's actual purpose — is NOT visualized by the current 2D template; its real
//   consumers are particle field-forces / ApplyVectorField, which ride the as-yet-unbuilt particle-field
//   seam. The golden therefore (1) GPU-verifies the decay channel against the closed-form decay, and
//   (2) asserts the assembled MSL TEXT contains the velocity math (cross/normalize swirl + radial), so a
//   regression that drops the velocity is caught even though the template can't render it. When the
//   particle-field / vector-application seam lands, a follow-up golden should probe f.xyz directly.
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <memory>
#include <string>
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx, appendVec3Param/appendScalarParam
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// fDecay — VERBATIM from ToroidalVortexField.cs AddDefinitions. *** [fork-fDecay-dead] ***: registered
// but never called by fToroidalVectorField (TiXL has the same dead helper). Ported for GLOBALS byte
// parity; compiles clean, unreferenced. pow/max identical in MSL.
static const char* kBodyFDecay =
    "float fDecay(float dist, float falloffRadius, float rate)\n"
    "{\n"
    "    //float x = saturate(dist / max(falloffRadius, 1e-6));\n"
    "    return 1.0 / (falloffRadius + pow(dist, rate));\n"
    "}";

// fToroidalVectorField — VERBATIM math from ToroidalVortexField.cs AddDefinitions. Returns float4:
// .xyz = velocity (swirl + radial), .w = decay weight in [0,1]. atan2/cos/sin/cross/normalize/length/
// pow/saturate all exist identically in MSL -> NO math fork. The trailing comment lines from the .cs
// (the //decay=1; and //Normalize then scale... lines) are kept verbatim for parity of the emitted text.
static const char* kBodyFToroidalVectorField =
    "float4 fToroidalVectorField(\n"
    "float3 p, \n"
    "float radius,\n"
    "float range,\n"
    "float swirlGain,\n"
    "float radialGain,\n"
    "float decayK)\n"
    "{\n"
    "    const float eps = 1e-6;\n"
    "    \n"
    "    // Angle around Z and basis on the centerline\n"
    "    float phi = atan2(p.y, p.x);\n"
    "    float c = cos(phi), s = sin(phi);\n"
    "    float3 e_r   = float3(c,  s, 0);  // radial in XY\n"
    "    float3 e_phi = float3(-s, c, 0);  // tangent of centerline (around Z)\n"
    "    \n"
    "    // Closest point on centerline and offset from it (minor-plane vector)\n"
    "    float3 C   = radius * e_r;\n"
    "    float3 r   = p - C;                // lies in span{e_r, e_z}\n"
    "    float  rho = length(r);\n"
    "    if (rho < eps) return float4(0,0,0,0);\n"
    "    \n"
    "    // Decay in [0,1]: 1 at rho=0, ->0 at rho >= range\n"
    "    float x = rho / max(range, eps);\n"
    "    float decay = saturate(1.0 - pow(x, decayK));\n"
    "    //decay = 1;\n"
    "    \n"
    "    // --- Swirl around the minor circle (tangent to cross-section) ---\n"
    "    // Use cross(e_phi, r) so swirl -> 0 on centerline and grows ~ rho.\n"
    "    float3 vSwirl = cross(e_phi, r);\n"
    "    // Normalize then scale by gain and decay (keeps units stable):\n"
    "    vSwirl = normalize(vSwirl) * (swirlGain * decay);\n"
    "    \n"
    "    // --- Radial attraction/repulsion toward/from the centerline ---\n"
    "    // +radialGain attracts toward the ring, -radialGain repels.\n"
    "    float3 dirToRing = -r / rho; // toward centerline\n"
    "    float3 vRadial   = dirToRing * (radialGain * decay);\n"
    "    \n"
    "    float3 v = vSwirl + vRadial;\n"
    "    return float4(v, decay);\n"
    "}";

// _axisCodes0 — byte-verbatim from ToroidalVortexField.cs (Axis enum X=0,Y=1,Z=2). The swizzle re-orders
// the sample point INTO and the result OUT OF the chosen-axis frame. Default Z -> "xyz" (identity).
// [fork-axis-selector] compile-time selector, NOT packed (RotateAxis/BendField precedent).
static const char* kAxisCodes[] = {"zyx", "xzy", "xyz"};
constexpr int kAxisCount = static_cast<int>(sizeof(kAxisCodes) / sizeof(kAxisCodes[0]));

// ---- ToroidalVortexField codegen node (a FieldNode subclass; pure generator — no inputs) -------------

struct ToroidalVortexFieldNode : FieldNode {
  float centerX = 0.f, centerY = 0.f, centerZ = 0.f;  // Center .t3 (0,0,0)
  float radius = 1.0f;                                 // Radius .t3 1.0
  float range = 1.0f;                                  // Range .t3 1.0
  float swirlGain = 1.0f;                              // SwirlGain .t3 1.0
  float radialGain = 1.0f;                             // RadialGain .t3 1.0
  float fallOffRate = 2.0f;                            // FallOffRate .t3 2.0 (decayK)
  int axis = 2;                                        // Axis .t3 2 (Z). Compile-time selector, NOT packed.
  // test-only bug mode (configureToroidalVortexField): 0 = none, 1 = drop the field-call (no emit).
  int injectBug = 0;

  explicit ToroidalVortexFieldNode(const std::string& shortId) {
    // TiXL BuildNodeId: <TypeName>_<shortGuid>_ — this IS the {ShaderNode} prefix the .cs interpolates.
    prefix = "ToroidalVortexField_" + shortId + "_";
  }

  int axisIdx() const { return (axis >= 0 && axis < kAxisCount) ? axis : 2; }

  void addGlobals(CodeAssembleCtx& c) const override {
    // ToroidalVortexField.cs AddDefinitions — both helpers, keys matching the .cs Globals keys.
    c.globals["fDecay"] = kBodyFDecay;  // [fork-fDecay-dead] registered though unreferenced (parity).
    c.globals["fToroidalVectorField"] = kBodyFToroidalVectorField;
  }

  void preShaderCode(CodeAssembleCtx& c, int /*inputIndex*/) const override {
    // PARITY ToroidalVortexField.cs GetPreShaderCode:
    //   a = _axisCodes0[(int)_axis];
    //   `f{c} = fToroidalVectorField(p{c}.{a} - {n}Center.{a}, {n}Radius, {n}Range, {n}SwirlGain,
    //          {n}RadialGain, {n}FallOffRate).{a}w;`
    // {c} = ctx id (root ""); {a} = axis swizzle; {n}Name -> P.<prefix>Name [fork-param-prefix].
    // Writes the WHOLE f{c} (float4): velocity in .xyz, decay in .w (the `.{a}w` swizzle re-frames it).
    if (injectBug == 1) return;  // drop the field call -> f stays seed (1,1,1,1) -> golden decay RED.
    const std::string ctx = c.ctx();
    const std::string a = kAxisCodes[axisIdx()];
    const std::string n = "P." + prefix;
    c.appendCall("f" + ctx + " = fToroidalVectorField(p" + ctx + "." + a + " - " + n + "Center." + a +
                 ", " + n + "Radius, " + n + "Range, " + n + "SwirlGain, " + n + "RadialGain, " + n +
                 "FallOffRate)." + a + "w;");
  }

  void collectParams(std::vector<float>& floatParams,
                     std::vector<std::string>& paramFields) const override {
    // [GraphParam] reflection order: Center(vec3) -> Radius -> Range -> SwirlGain -> RadialGain ->
    // FallOffRate. Axis is NOT a [GraphParam] (compile-time selector). Center at offset 0 -> packed_float3,
    // padForVec3 adds no padding; then 5 trailing scalars. 8 floats total.
    appendVec3Param(floatParams, paramFields, prefix + "Center", centerX, centerY, centerZ);
    appendScalarParam(floatParams, paramFields, prefix + "Radius", radius);
    appendScalarParam(floatParams, paramFields, prefix + "Range", range);
    appendScalarParam(floatParams, paramFields, prefix + "SwirlGain", swirlGain);
    appendScalarParam(floatParams, paramFields, prefix + "RadialGain", radialGain);
    appendScalarParam(floatParams, paramFields, prefix + "FallOffRate", fallOffRate);
  }
};

NodeSpec toroidalVortexFieldSpec() {
  NodeSpec s;
  s.type = "ToroidalVortexField";
  s.title = "Toroidal Vortex Field";
  // Center = Vec3 head run (.x/.y/.z), default (0,0,0).
  PortSpec cx; cx.id = "Center.x"; cx.name = "Center"; cx.dataType = "Float"; cx.isInput = true;
  cx.def = 0.0f; cx.minV = -10.0f; cx.maxV = 10.0f; cx.widget = Widget::Vec; cx.vecArity = 3;
  PortSpec cy; cy.id = "Center.y"; cy.name = "Center.y"; cy.dataType = "Float"; cy.isInput = true;
  cy.def = 0.0f; cy.minV = -10.0f; cy.maxV = 10.0f;
  PortSpec cz; cz.id = "Center.z"; cz.name = "Center.z"; cz.dataType = "Float"; cz.isInput = true;
  cz.def = 0.0f; cz.minV = -10.0f; cz.maxV = 10.0f;
  PortSpec rad; rad.id = "Radius"; rad.name = "Radius"; rad.dataType = "Float"; rad.isInput = true;
  rad.def = 1.0f; rad.minV = 0.0f; rad.maxV = 10.0f;
  PortSpec rng; rng.id = "Range"; rng.name = "Range"; rng.dataType = "Float"; rng.isInput = true;
  rng.def = 1.0f; rng.minV = 0.0f; rng.maxV = 10.0f;
  PortSpec sg; sg.id = "SwirlGain"; sg.name = "SwirlGain"; sg.dataType = "Float"; sg.isInput = true;
  sg.def = 1.0f; sg.minV = -10.0f; sg.maxV = 10.0f;
  PortSpec rg; rg.id = "RadialGain"; rg.name = "RadialGain"; rg.dataType = "Float"; rg.isInput = true;
  rg.def = 1.0f; rg.minV = -10.0f; rg.maxV = 10.0f;
  PortSpec fr; fr.id = "FallOffRate"; fr.name = "FallOffRate"; fr.dataType = "Float"; fr.isInput = true;
  fr.def = 2.0f; fr.minV = 0.0f; fr.maxV = 10.0f;
  // Axis = enum CODE SELECTOR (Float port storing the enum index), .t3 default 2 (Z). NOT packed.
  PortSpec ax; ax.id = "Axis"; ax.name = "Axis"; ax.dataType = "Float"; ax.isInput = true;
  ax.def = 2.0f; ax.minV = 0.0f; ax.maxV = 2.0f; ax.widget = Widget::Enum;
  ax.labels = {"X", "Y", "Z"};
  // Output: a Field (ShaderGraphNode in TiXL) — the connectable result other field ops / consumers read.
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  s.ports = {cx, cy, cz, rad, rng, sg, rg, fr, ax, out};
  return s;
}

std::shared_ptr<FieldNode> makeToroidalVortexField(const std::string& shortId) {
  return std::make_shared<ToroidalVortexFieldNode>(shortId);
}

const FieldOp g_toroidalVortexFieldOp(toroidalVortexFieldSpec(), makeToroidalVortexField);

}  // namespace

// Param-cook + test seam (mirrors configureRotateAxis / configureBendField): set the REAL params on a
// makeFieldNode("ToroidalVortexField",...) node plus a test-only injectBug (0 none / 1 drop-field-call)
// that corrupts the OP's REAL preShaderCode emit. The leaf type is TU-private; this downcasts inside the
// owning TU. Production passes injectBug=0. No-op if `node` is not a ToroidalVortexFieldNode (defensive).
void configureToroidalVortexField(FieldNode& node, float centerX, float centerY, float centerZ,
                                  float radius, float range, float swirlGain, float radialGain,
                                  float fallOffRate, int axis, int injectBug) {
  if (auto* n = dynamic_cast<ToroidalVortexFieldNode*>(&node)) {
    n->centerX = centerX; n->centerY = centerY; n->centerZ = centerZ;
    n->radius = radius; n->range = range;
    n->swirlGain = swirlGain; n->radialGain = radialGain; n->fallOffRate = fallOffRate;
    n->axis = axis;
    n->injectBug = injectBug;
  }
}

}  // namespace sw
