#pragma once
// mesh_modify_golden_cases — SHARED case support for the mesh→mesh MODIFY goldens
// (mesh_modify_golden.cpp + mesh_modify2_golden.cpp). Split out per ARCHITECTURE.md rule 4
// (≤400-line files): the hand/python derivations live HERE next to their named expected-value
// constants, so the golden TUs only build graphs and assert. Every oracle below is hand-derived
// from the TiXL shader/op sources @395c4c55 (file:line cited per case), NOT from the sw cook path.
//
// QuadMesh defaults (Segments=(1,1), Scale=1, Pivot=0.5, Center varies): verts v0=(0,0,0) v1=(0,1,0)
// v2=(1,0,0) v3=(1,1,0); faces f0=(0,2,1) f1=(2,3,1). Attributes (QuadMesh.cs/quadCook):
// Normal=(0,0,1), Tangent=(1,0,0), Bitangent=(0,1,0), Selection=1, TexCoord v0=(0,0) v1=(0,1)
// v2=(1,0) v3=(1,1).
//
// ZONE: shell-tier golden support (graph-building helpers + pure constants; no Metal dependency).
#include "runtime/graph.h"  // Graph/Node/NodeSpec/findSpec

namespace sw {
namespace mesh_golden {

inline Node makeQuad(int id, float cx, float cy, float cz) {
  Node m; m.id = id; m.type = "QuadMesh";
  m.params["Segments.x"] = 1.0f; m.params["Segments.y"] = 1.0f; m.params["Scale"] = 1.0f;
  m.params["Stretch.x"] = 1.0f; m.params["Stretch.y"] = 1.0f;
  m.params["Pivot.x"] = 0.5f; m.params["Pivot.y"] = 0.5f;
  m.params["Center.x"] = cx; m.params["Center.y"] = cy; m.params["Center.z"] = cz;
  return m;
}

// Find a node spec's output pin index + its first Mesh-typed input pin index.
inline void meshPins(const char* type, int& outPin, int& meshInPin) {
  outPin = -1; meshInPin = 0;
  const NodeSpec* s = findSpec(type);
  for (size_t i = 0; i < s->ports.size(); ++i) {
    if (!s->ports[i].isInput && s->ports[i].dataType == "Mesh") outPin = (int)i;
    if (s->ports[i].isInput && s->ports[i].dataType == "Mesh" && meshInPin == 0) meshInPin = (int)i;
  }
  // outPin may also be a non-Mesh "Data"/"Result"; re-scan for the single output if not found above.
  if (outPin < 0) for (size_t i = 0; i < s->ports.size(); ++i) if (!s->ports[i].isInput) { outPin = (int)i; break; }
}

// ═══════════ mesh_modify_golden.cpp cases ═══════════

// ── RecomputeNormals, FLAT quad (sub-case 1) ──
// Flat z=0 quad: every face N=(0,0,1) → normalSum∝(0,0,1) → newNormal=(0,0,1). newTangent=
// cross(bitangent(0,1,0), N(0,0,1))=(1,0,0); newBitangent=cross(N,(1,0,0))=(0,1,0). Selection=faceCount:
// v0 in f0 only=1; v1 in f0,f1=2; v2 in f0,f1=2; v3 in f1 only=1 (THE recompute-only discriminator).

// ── RecomputeNormals, NON-COPLANAR fold (sub-case 2, P2 fix) ──
// The flat quad above is degenerate for the averaging formula: recomputed normal == input normal,
// so a wrong normal-sum stayed green. Fold one corner: QuadMesh → DeformMesh (TwistAxis=X,
// Twist=90°, same config as mesh_modify2's deform golden; mesh-Deform.hlsl:50-55) → v3
// (1,1,0)→(1,0,1), others fixed → RecomputeNormals.
// Oracle hand-derived from mesh-RecomputeNormals.hlsl computeNormal (@395c4c55):
//   per adjacent face N=cross(P2-P1,P3-P1) (:93-96), area=0.5*|N| (:98),
//   normalSum += N + N*area (:99,:102), newNormal=normalize(normalSum) (:118).
//   f0=(v0,v2,v1): N=(0,0,1), area=0.5 → contrib (0,0,1.5).
//   f1=(v2,v3,v1): U=(0,0,1), V=(-1,1,0) → N=(-1,-1,0), area=√2/2 → contrib
//     (-1.70710678,-1.70710678,0).   # python: N*(1+0.5*sqrt(2))
//   v1,v2 (both faces): normalize(-1.70710678,-1.70710678,1.5)
//     = (-0.6006165,-0.6006165,0.5277495)
//   v3 (f1 only): (-0.7071068,-0.7071068,0); v0 (f0 only): (0,0,1).
//   newTangent=cross(srcBitangent(0,1,0), newNormal) (:120, NOT normalized):
//     v3 → (0,0,0.7071068). Selection=faceCount (:123): (1,2,2,1).
constexpr float kFoldN12xy = -0.6006165f, kFoldN12z = 0.5277495f;  // v1/v2 normal (x==y)
constexpr float kFoldN3xy = -0.7071068f;                           // v3 normal (x==y, z=0)
constexpr float kFoldT3z = 0.7071068f;                             // v3 tangent (0,0,z)

// ── TransformMeshUVs sub-case 1: pure UV translation (0.1,0.2) ──
// Pivot 0.5, Stretch 1, Rot 0 → M = T(0.1,0.2,0). (Pivot cancels for the identity scale/rotation
// block → pure translation regardless of pivot.)

// ── TransformMeshUVs sub-case 2: pure UV scale-about-pivot (Stretch 2, Pivot 0.5) ──
// M scales UVs about (0.5,0.5): uv' = (uv - 0.5)*2 + 0.5. v0 (0,0)→(-0.5,-0.5);
// v3 (1,1)→(1.5,1.5); the (0.5,0.5) center is fixed.

// ── TransformMeshUVs sub-case 3: pure UV ROTATION (P2 fix — sub-cases 1/2 leave the rotation
//    block identity) ──
// Oracle: TransformMeshUVs.t3 wires Rotate → TransformMatrix.Rotation_PitchYawRoll; the matrix
// is TransformMatrix.cs @395c4c55: yaw=r.Y, pitch=r.X, roll=r.Z (:29-32),
// CreateFromYawPitchRoll (:39) about Pivot (:47-52); Pivot default (0.5,0.5,0.5) (.t3).
// Rotate=(0,0,30) → roll=RotZ(30°) about (0.5,0.5) in UV space (.NET row-vector: CCW):
// uv' = (uv-p)·RotZ + p; cos30=0.8660254, sin30=0.5. python:
//   uv0 (0,0): rel(-0.5,-0.5) → (-0.5c+0.5s, -0.5s-0.5c)+0.5 = (0.3169873, -0.1830127)
//   uv3 (1,1): rel(0.5,0.5)   → (0.5c-0.5s, 0.5s+0.5c)+0.5  = (0.6830127, 1.1830127)
constexpr float kRotZ30Uv0x = 0.3169873f, kRotZ30Uv0y = -0.1830127f;
constexpr float kRotZ30Uv3x = 0.6830127f, kRotZ30Uv3y = 1.1830127f;

// ═══════════ mesh_modify2_golden.cpp cases ═══════════

// ── SplitMeshVertices ──
// QuadMesh → SplitMeshVertices (ShadeFlat=0). Topology un-weld: 2 faces → 6 verts, faces re-indexed
// (0,1,2) and (3,4,5). Each face's 3 verts are copies of the source corners; at ShadeFlat=0 the
// normals stay (0,0,1). Face f0=(v0,v2,v1) → out verts [v0,v2,v1] at positions (0,0,0)(1,0,0)(0,1,0).

// ── SelectVertices, INSIDE/OUTSIDE legs ──
// QuadMesh → SelectVertices (Box volume, Center=(0.5,0.5,0) so the quad's [0,1]² centers on the
// gizmo, Stretch=1, Scale=1, FallOff=0, Mode=Override, Strength=1, ClampResult off).
// TransformVolume = inverse(T(0.5,0.5,0)) → posInVolume = pos - (0.5,0.5,0).
//   v0 (0,0,0)→(-0.5,-0.5,0): |max|=0.5 → smoothstep(1,1→clamps)... d=0.5 < 1 → t=(0.5-1)/(1-1)...
//   uses smoothstep(1+0, 1, 0.5): edge0=1,edge1=1 degenerate; HLSL smoothstep with edge0==edge1 →
//   step. To avoid the degenerate edge we use FallOff=1 → smoothstep(2,1,d). d(v0)=0.5 →
//   t=(0.5-2)/(1-2)=1.5→clamp1 → s=1 (fully selected, INSIDE). A far point: Center=(5,5,0) →
//   posInVolume huge → d>2 → s=0.
// We assert: with Center on the quad, all 4 verts Selection==1; with Center far away, all ==0;
// plus the MID-FALLOFF leg below sampling the smoothstep middle (the P2 fix for endpoint-only
// sampling).

// ── SelectVertices, MID-FALLOFF leg (P2 fix) ──
// inside/outside above only sample the smoothstep PLATEAUS (s=1 / s=0); a wrong falloff curve
// stayed green. Park two verts in the diverging middle.
// Oracle mesh-SelectVertices.hlsl @395c4c55: Box → d = max(|posInVolume|) (:60-61, Phase=0),
// s = smoothstep(1+FallOff, 1, d) (:62); Override → s *= Strength (:81-83).
// Center=(-1.5,0.5,0), FallOff=2 → posInVolume = pos-(−1.5,0.5,0); smoothstep(3,1,d):
//   t = saturate((d-3)/(1-3)), s = t²(3-2t).  python:
//   v0 (0,0,0)/v1 (0,1,0): d=max(1.5,0.5)=1.5 → t=0.75 → s=0.5625*1.5 = 0.84375
//   v2 (1,0,0)/v3 (1,1,0): d=max(2.5,0.5)=2.5 → t=0.25 → s=0.0625*2.5 = 0.15625
constexpr float kSelMidNear = 0.84375f, kSelMidFar = 0.15625f;

// ── DeformMesh, TWIST leg ──
// QuadMesh → DeformMesh, UseVertexSelection=false (s=1). Twist about Z, TwistAxis=2, Twist=90°,
// TwistPivot=0, Spherize=0, Taper=0. Only Twist active.
//   twist angle for a vertex = pos.z * radians(90). All quad verts have z=0 → angle=0 → no change.
//   → Position unchanged. To get a measurable twist we set TwistAxis=0 (about X), Twist=90:
//   angle = pos.x * radians(90). v2 (1,0,0): angle = 1 * π/2; rotate (twp=pos, twPivot=0) about X:
//     tw.x=twp.x=1; tw.y=twp.y*cos - twp.z*sin = 0; tw.z=twp.y*sin + twp.z*cos = 0 → (1,0,0)
//     unchanged (y=z=0). v3 (1,1,0): angle=π/2; tw.y=1*cos(π/2)-0=~0; tw.z=1*sin(π/2)+0=1 → (1,0,1).
//   Assert v3.Position ≈ (1, 0, 1); v0 (0,0,0) angle=0 → (0,0,0).

// ── DeformMesh, SPHERIZE leg (P2 fix — Spherize/Taper in the twist leg are 0, so those formula
//    branches never fired) ──
// Quad shifted to Center=(1,0,0) so no vertex sits at the origin singularity.
// Oracle mesh-Deform.hlsl @395c4c55: spherePos = (pos-Pivot)*(Radius/|pos-Pivot|) (:27-32,
// Pivot=0 default); pos = lerp(pos, lerp(pos, spherePos, Spherize), s=1) (:81,:89).
// Spherize=0.5, Radius=2 → pos' = pos*(0.5 + 1/|pos|).  python:
//   v0 (1,0,0): |v|=1  → *1.5        = (1.5, 0, 0)
//   v1 (1,1,0): |v|=√2 → *1.20710678 = (1.2071068, 1.2071068, 0)
//   v3 (2,1,0): |v|=√5 → *0.94721360 = (1.8944272, 0.9472136, 0)
// (Taper=0/Twist=0 stay identity downstream, :96/:111-114.)
constexpr float kSphV0x = 1.5f;
constexpr float kSphV1xy = 1.2071068f;
constexpr float kSphV3x = 1.8944272f, kSphV3y = 0.9472136f;

// ── CollapseVertices ──
// QuadMesh → CollapseVertices, Box volume Center=(0.5,0.5,0) FallOff=1 (so s=1 everywhere on the
// quad), StepCount=1, Strength=2, GridOffset=0, Amount=1, SmoothSteps=0 (→ BlendStep=0 →
// smoothstep(0,1,0)=0 → take snap1 only). With s=1, StepCount=1: xx=1, step=1 → but step clamps via
// (1<<step). To get a clean snap pick StepCount=1, s=1 → xx=1.0, step=1, ff=0. maxS=1<<1=2.
// ss1=(1<<1)/2*2 = 2. snap1=floor(pos/2+0.5)*2.
//   v0 (0,0,0): floor(0/2+0.5)*2 = floor(0.5)*2 = 0 → (0,0,0). v3 (1,1,0): floor(1/2+0.5)*2=
//   floor(1)*2=2 → (2,2,0). Position = lerp(pos, snap1+GridOffset, Amount=1) = snap1. Assert
//   v3 ≈ (2,2,0), v0 ≈ (0,0,0). (ff=0 → blend uses snap1 regardless of BlendStep; SmoothSteps
//   irrelevant here, keeps the golden exact.)

// ── MeshProjectUV ──
// QuadMesh → MeshProjectUV (identity: Translate=0, Rotate=0, Stretch=1, Scale=1, ToTexCoord2=false).
//   Transform = Identity → uv = pos.xy + (1,1). v0 (0,0,0) → TexCoord (1,1); v3 (1,1,0) → (2,2).
//   With Translate=(0.5, -0.5, 0): uv = (pos.x+0.5, pos.y-0.5) + (1,1). v0 → (1.5, 0.5);
//   v3 → (2.5,1.5).

}  // namespace mesh_golden
}  // namespace sw
