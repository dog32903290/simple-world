// RotateVector3 value op (value-op self-registration seam leaf — Phase C numbers/vec3 mining).
// TiXL authority: Operators/Lib/numbers/vec3/RotateVector3.cs (+ RotateVector3.t3 defaults).
//
//   RotateVector3.cs Update() (lines 15-22):
//     var vec   = VectorA.GetValue(context);
//     var axis  = Axis.GetValue(context);
//     var angle = Angle.GetValue(context) / 180 * MathF.PI;    // degrees → radians (line 19)
//     var m     = Matrix4x4.CreateFromAxisAngle(axis, angle);   // rotation matrix (line 21)
//     Result.Value = Vector3.TransformNormal(vec, m) * Scale.GetValue(context); // (line 22)
//
//   Math note: Matrix4x4.CreateFromAxisAngle implements the standard Rodrigues' rotation formula.
//   For a unit axis k and angle θ:
//     m = I*cos(θ) + (1-cos(θ))*(k⊗k) + sin(θ)*[k]×
//   Vector3.TransformNormal applies only the 3×3 upper-left (no translation).
//   The result is then scaled by Scale (scalar multiply).
//   If axis is the zero vector, CreateFromAxisAngle in .NET 6+ returns identity, so result = vec*scale.
//
//   Ports (from RotateVector3.cs field declaration order):
//     VectorA = InputSlot<Vector3>  (line 25)  — vector to rotate, default {1,0,0}
//     Angle   = InputSlot<float>    (line 28)  — rotation in DEGREES, default 0.0
//     Axis    = InputSlot<Vector3>  (line 31)  — rotation axis, default {0,0,1}
//     Scale   = InputSlot<float>    (line 34)  — output scale factor, default 1.0
//   Output:
//     Result  = Slot<Vector3>       (line 7)   — rotated and scaled vec3
//
//   RotateVector3.t3 DefaultValues: Axis={0,0,1}, VectorA={1,0,0}, Angle=0.0, Scale=1.0.
//
// EVAL-SIDE LAYOUT (flat path — no multiInput):
//   Each Vector3 input decomposes into 3 consecutive Float ports (fork-vec3-as-3-floats convention).
//   in[] = [VectorA.x, VectorA.y, VectorA.z,   (indices 0-2)
//           Angle,                               (index 3)
//           Axis.x, Axis.y, Axis.z,             (indices 4-6)
//           Scale]                              (index 7)   n = 8
//   Output ports Result.x/.y/.z follow at spec indices 8/9/10.
//   Component k = outIdx - 8 (0=x, 1=y, 2=z).
//
//   Rodrigues inline (avoids needing a full Matrix4x4 type):
//     angle_rad = Angle / 180 * PI  (line 19)
//     cos_a = cos(angle_rad), sin_a = sin(angle_rad)
//     kx = Axis.x, ky = Axis.y, kz = Axis.z  (axis; may be non-unit in TiXL — .NET normalizes internally)
//     .NET Matrix4x4.CreateFromAxisAngle NORMALIZES the axis before use.
//     After normalizing k:
//       R = cos_a*I + sin_a*[k]× + (1-cos_a)*(k⊗k)
//     TransformNormal((vx,vy,vz), R):
//       rx = vx*(cos_a + kx*kx*(1-cos_a)) + vy*(kx*ky*(1-cos_a) - kz*sin_a) + vz*(kx*kz*(1-cos_a) + ky*sin_a)
//       ry = vx*(ky*kx*(1-cos_a) + kz*sin_a) + vy*(cos_a + ky*ky*(1-cos_a)) + vz*(ky*kz*(1-cos_a) - kx*sin_a)
//       rz = vx*(kz*kx*(1-cos_a) - ky*sin_a) + vy*(kz*ky*(1-cos_a) + kx*sin_a) + vz*(cos_a + kz*kz*(1-cos_a))
//     Result[k] = r[k] * Scale.
//
// FORKS (named):
//   - fork-rotatevec3-vec3-as-3-floats: VectorA, Axis, and Result are each 3 Float ports.
//     eval is byte-identical to TiXL (Rodrigues = same formula .NET uses). Not an eval fork.
//   - fork-rotatevec3-axis-normalize: .NET Matrix4x4.CreateFromAxisAngle normalizes the axis
//     internally. This impl replicates that normalization (divide by norm, guard zero-axis).
//     For a pre-normalized axis (the typical use-case) the result is byte-identical.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

int runRotateVector3SelfTest(bool injectBug);

namespace {

// Rodrigues rotation (matches .NET Matrix4x4.CreateFromAxisAngle + Vector3.TransformNormal).
// Axis is normalized internally (fork-rotatevec3-axis-normalize).
// angle_rad = Angle_degrees / 180 * PI  (RotateVector3.cs line 19).
float evalRotateVector3(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 8) return 0.0f;
  const int k = outIdx - 8;  // 0=x, 1=y, 2=z
  if (k < 0 || k > 2) return 0.0f;

  const float vx = in[0], vy = in[1], vz = in[2];
  const float angleDeg = in[3];
  float kx = in[4], ky = in[5], kz = in[6];
  const float scale = in[7];

  const float angle = angleDeg / 180.0f * (float)M_PI;  // line 19

  // Normalize axis (fork-rotatevec3-axis-normalize — mirrors .NET CreateFromAxisAngle behavior).
  const float axisLen = std::sqrt(kx*kx + ky*ky + kz*kz);
  if (axisLen > 1e-7f) {
    kx /= axisLen; ky /= axisLen; kz /= axisLen;
  } else {
    // Zero axis: .NET returns identity matrix → result = vec * scale.
    return (k == 0 ? vx : k == 1 ? vy : vz) * scale;
  }

  const float c = std::cos(angle);
  const float s = std::sin(angle);
  const float t = 1.0f - c;

  // Rodrigues' rotation matrix row k applied to v:
  float r;
  if (k == 0) {
    r = vx*(c + kx*kx*t) + vy*(kx*ky*t - kz*s) + vz*(kx*kz*t + ky*s);
  } else if (k == 1) {
    r = vx*(ky*kx*t + kz*s) + vy*(c + ky*ky*t) + vz*(ky*kz*t - kx*s);
  } else {
    r = vx*(kz*kx*t - ky*s) + vy*(kz*ky*t + kx*s) + vz*(c + kz*kz*t);
  }
  return r * scale;  // line 22: * Scale
}

}  // namespace

static const ValueOp _reg_rotatevector3{
    // RotateVector3 (TiXL Lib.numbers.vec3.RotateVector3):
    //   angle_rad = Angle/180*PI; m = Matrix4x4.CreateFromAxisAngle(Axis, angle_rad);
    //   Result = TransformNormal(VectorA, m) * Scale.
    // Port order matches in[] read: VectorA.x/y/z, Angle, Axis.x/y/z, Scale, then Result.x/y/z.
    // Defaults from RotateVector3.t3: VectorA={1,0,0}, Angle=0.0, Axis={0,0,1}, Scale=1.0.
    // fork-rotatevec3-vec3-as-3-floats: VectorA and Axis each become 3 Float ports (Widget::Vec).
    {"RotateVector3", "RotateVector3",
     {{"VectorA.x", "VectorA",   "Float", true,  1.0f,  -100.0f, 100.0f, Widget::Vec,    {}, false, 3},
      {"VectorA.y", "VectorA.y", "Float", true,  0.0f,  -100.0f, 100.0f, Widget::Vec,    {}, false, 1},
      {"VectorA.z", "VectorA.z", "Float", true,  0.0f,  -100.0f, 100.0f, Widget::Vec,    {}, false, 1},
      {"Angle",     "Angle",     "Float", true,  0.0f,  -360.0f, 360.0f, Widget::Slider},
      {"Axis.x",    "Axis",      "Float", true,  0.0f,  -1.0f,   1.0f,   Widget::Vec,    {}, false, 3},
      {"Axis.y",    "Axis.y",    "Float", true,  0.0f,  -1.0f,   1.0f,   Widget::Vec,    {}, false, 1},
      {"Axis.z",    "Axis.z",    "Float", true,  1.0f,  -1.0f,   1.0f,   Widget::Vec,    {}, false, 1},
      {"Scale",     "Scale",     "Float", true,  1.0f,  -10.0f,  10.0f,  Widget::Slider},
      {"Result.x",  "Result.x",  "Float", false},
      {"Result.y",  "Result.y",  "Float", false},
      {"Result.z",  "Result.z",  "Float", false}},
     evalRotateVector3},
    "rotatevector3", runRotateVector3SelfTest};

// --- RotateVector3 MATH golden -----------------------------------------------------------------
// Hand-computed using Rodrigues' formula (identical to .NET Matrix4x4.CreateFromAxisAngle).
// All vectors use normalized axes; angle_rad = Angle_degrees / 180 * PI.
//
// CASE 1: Rotate (1,0,0) around Z=(0,0,1) by 90°, Scale=1.
//   angle_rad = π/2; c=cos(π/2)=0, s=sin(π/2)=1; k=(0,0,1) (already unit).
//   rx = 1*(0+0*0*1) + 0*(0*0*1-1*1) + 0*(0*1*1+0*1) = 0
//   ry = 1*(0*0*1+1*1) + 0*(0+0*0*1) + 0*(0*0*1-0*1) = 1
//   rz = 1*(1*0*1-0*1) + 0*(1*0*1+0*1) + 0*(0+1*1*1) = 0
//   Result = (0,1,0)*1 = (0,1,0)   ✓ (standard 90° CCW around Z)
//
// CASE 2: Rotate (1,0,0) around Y=(0,1,0) by 90°, Scale=2.
//   c=0, s=1; k=(0,1,0).
//   rx = 1*(0+0) + 0*(0*1*1-0*1) + 0*(0*0*1+1*1) = 0
//   ry = 1*(1*0*1+0*1) + 0*(0+0) + 0*(1*0*1-0*1) = 0
//   rz = 1*(0*0*1-1*1) + 0*(0*1*1+0*1) + 0*(0+0) = -1
//   Result = (0,0,-1)*2 = (0,0,-2)
int runRotateVector3SelfTest(bool injectBug) {
  const float eps = 1e-4f;  // trig computations need slightly wider eps
  bool ok = true;

  auto evalRV3 = [&](float vx, float vy, float vz,
                     float angle, float ax, float ay, float az,
                     float scale, const char* outPort) -> float {
    const NodeSpec* spec = findSpec("RotateVector3");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "RotateVector3";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["VectorA.x"] = vx;  g.node(nid)->params["VectorA.y"] = vy;
    g.node(nid)->params["VectorA.z"] = vz;
    g.node(nid)->params["Angle"]     = angle;
    g.node(nid)->params["Axis.x"]   = ax;   g.node(nid)->params["Axis.y"]   = ay;
    g.node(nid)->params["Axis.z"]   = az;
    g.node(nid)->params["Scale"]    = scale;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == outPort) { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // CASE 1: Rotate (1,0,0) around Z=(0,0,1) by 90°, Scale=1 → (0,1,0).
  // injectBug: claim Result.x = 1 (no rotation) instead of 0 → only breaks when x≠0 expected.
  // Use Result.y for the bite since x = 0 either way.
  {
    float rx = evalRV3(1,0,0, 90.0f, 0,0,1, 1.0f, "Result.x");
    bool pass = std::fabs(rx) < eps;  // 0
    ok = ok && pass;
    printf("[selftest-rotatevector3] Rot90Z.x=%.5f want=0.00000 -> %s\n", rx, pass?"PASS":"FAIL");
  }
  {
    float ry = evalRV3(1,0,0, 90.0f, 0,0,1, 1.0f, "Result.y");
    float want = injectBug ? 0.0f : 1.0f;  // bug: skip rotation (wrong formula) → 0 instead of 1 → RED
    bool pass = std::fabs(ry - want) < eps;
    ok = ok && pass;
    printf("[selftest-rotatevector3] Rot90Z.y=%.5f want=%.5f (CCW 90deg Z) -> %s\n",
           ry, want, pass?"PASS":"FAIL");
  }
  {
    float rz = evalRV3(1,0,0, 90.0f, 0,0,1, 1.0f, "Result.z");
    bool pass = std::fabs(rz) < eps;  // 0
    ok = ok && pass;
    printf("[selftest-rotatevector3] Rot90Z.z=%.5f want=0.00000 -> %s\n", rz, pass?"PASS":"FAIL");
  }

  // CASE 2: Rotate (1,0,0) around Y=(0,1,0) by 90°, Scale=2 → (0,0,-1)*2 = (0,0,-2).
  //   Computed above: rx=0, ry=0, rz=-1, then *2 → (0,0,-2).
  {
    float rx = evalRV3(1,0,0, 90.0f, 0,1,0, 2.0f, "Result.x");
    bool pass = std::fabs(rx) < eps;
    ok = ok && pass;
    printf("[selftest-rotatevector3] Rot90Y*scale2.x=%.5f want=0.00000 -> %s\n",
           rx, pass?"PASS":"FAIL");
  }
  {
    float rz = evalRV3(1,0,0, 90.0f, 0,1,0, 2.0f, "Result.z");
    bool pass = std::fabs(rz - (-2.0f)) < eps;  // -1*2 = -2
    ok = ok && pass;
    printf("[selftest-rotatevector3] Rot90Y*scale2.z=%.5f want=-2.00000 -> %s\n",
           rz, pass?"PASS":"FAIL");
  }

  // CASE 3: t3 defaults — Rotate (1,0,0) around Z=(0,0,1) by 0°, Scale=1 → (1,0,0).
  //   c=1, s=0; Result = vec * scale = (1,0,0)*1 = (1,0,0).
  {
    float rx = evalRV3(1,0,0, 0.0f, 0,0,1, 1.0f, "Result.x");
    bool pass = std::fabs(rx - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-rotatevector3] t3-defaults.x=%.5f want=1.00000 (identity rotation) -> %s\n",
           rx, pass?"PASS":"FAIL");
  }

  // CASE 4: Rotate (0,1,0) around X=(1,0,0) by 180°, Scale=1 → (0,-1,0).
  //   c=cos(π)=-1, s=sin(π)=0; k=(1,0,0).
  //   rx = 0*(-1+1*1*2) + 1*(1*0*2-0*0) + 0*(1*0*2+0*0) = 0
  //   ry = 0*(0*1*2+0*0) + 1*(-1+0*0*2) + 0*(0*0*2-1*0) = -1
  //   rz = 0*(0*1*2-0*0) + 1*(0*0*2+1*0) + 0*(-1+0*0*2) = 0
  //   Result = (0,-1,0)*1 = (0,-1,0).
  {
    float ry = evalRV3(0,1,0, 180.0f, 1,0,0, 1.0f, "Result.y");
    bool pass = std::fabs(ry - (-1.0f)) < eps;
    ok = ok && pass;
    printf("[selftest-rotatevector3] Rot180X.y=%.5f want=-1.00000 (180deg flip) -> %s\n",
           ry, pass?"PASS":"FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
