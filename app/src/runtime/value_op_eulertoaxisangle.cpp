// EulerToAxisAngle value op (value-op self-registration seam leaf — Phase C numbers/vec3 mining).
// TiXL authority: Operators/Lib/numbers/vec3/EulerToAxisAngle.cs (+ EulerToAxisAngle.t3 defaults).
//
//   EulerToAxisAngle.cs Update() (lines 18-58):
//     Source: https://www.euclideanspace.com/maths/geometry/rotations/conversions/eulerToAngle/
//     INPUTS ARE IN RADIANS (line 27: "Assuming the angles are in radians").
//     heading = Rotation.X; attitude = Rotation.Y; bank = Rotation.Z.
//     c1=cos(heading/2); s1=sin(heading/2);
//     c2=cos(attitude/2); s2=sin(attitude/2);
//     c3=cos(bank/2);    s3=sin(bank/2);
//     c1c2=c1*c2; s1s2=s1*s2;
//     w  = c1c2*c3 - s1s2*s3;
//     x  = c1c2*s3 + s1s2*c3;
//     y  = s1*c2*c3 + c1*s2*s3;
//     z  = c1*s2*c3 - s1*c2*s3;
//     angle = 2*acos(w);
//     norm = x*x + y*y + z*z;
//     if (norm < 0.001) { x=1; y=z=0; }
//     else { norm=sqrt(norm); x/=norm; y/=norm; z/=norm; }
//     Axis.Value = (x,y,z); Angle.Value = (float)angle.
//
//   Ports (from EulerToAxisAngle.cs field declarations):
//     Outputs: Axis  (line 7)  = Slot<Vector3>  — rotation axis (unit vector)
//              Angle (line 10) = Slot<float>    — angle in radians
//     Input:   Rotation (line 63) = InputSlot<Vector3> — Euler angles in radians {0,0,0}
//
//   EulerToAxisAngle.t3 DefaultValues: Rotation={X:0, Y:0, Z:0}.
//
// OUTPUTS: Axis (vec3 → 3 Float ports) + Angle (1 Float). Total 4 output ports at indices 3..6.
//
// EVAL-SIDE LAYOUT (flat path):
//   in[] = [Rotation.x, Rotation.y, Rotation.z]  (n = 3)
//   Output ports: Axis.x (idx 3), Axis.y (idx 4), Axis.z (idx 5), Angle (idx 6).
//   Component:
//     outIdx 3 → k=0 = Axis.x
//     outIdx 4 → k=1 = Axis.y
//     outIdx 5 → k=2 = Axis.z
//     outIdx 6 → k=3 = Angle
//
// FORKS (named):
//   - fork-eulertoaxisangle-vec3-as-3-floats: Rotation (input) and Axis (output) are each
//     3 Float ports. Math is byte-identical to TiXL. Not an eval fork.
//   - fork-eulertoaxisangle-angle-as-float: TiXL Angle output is Slot<float>; this runtime
//     encodes it directly as a Float port (single output). Not an eval fork.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

int runEulerToAxisAngleSelfTest(bool injectBug);

namespace {

// in[] = [Rotation.x (heading), Rotation.y (attitude), Rotation.z (bank)]  n = 3.
// Outputs at outIdx 3..6: Axis.x, Axis.y, Axis.z, Angle.
// Formula verbatim from EulerToAxisAngle.cs lines 26-57.
// INPUTS IN RADIANS (cs line 27).
float evalEulerToAxisAngle(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 3) return 0.0f;
  const int k = outIdx - 3;  // 0=Axis.x, 1=Axis.y, 2=Axis.z, 3=Angle
  if (k < 0 || k > 3) return 0.0f;

  const double heading  = in[0];  // Rotation.X (radians)
  const double attitude = in[1];  // Rotation.Y (radians)
  const double bank     = in[2];  // Rotation.Z (radians)

  // EulerToAxisAngle.cs lines 27-47
  const double c1 = std::cos(heading  / 2.0);
  const double s1 = std::sin(heading  / 2.0);
  const double c2 = std::cos(attitude / 2.0);
  const double s2 = std::sin(attitude / 2.0);
  const double c3 = std::cos(bank     / 2.0);
  const double s3 = std::sin(bank     / 2.0);
  const double c1c2 = c1 * c2;
  const double s1s2 = s1 * s2;
  const double w = c1c2 * c3 - s1s2 * s3;
  double x = c1c2 * s3 + s1s2 * c3;
  double y = s1 * c2 * c3 + c1 * s2 * s3;
  double z = c1 * s2 * c3 - s1 * c2 * s3;
  const double angle = 2.0 * std::acos(w);  // radians
  double norm = x*x + y*y + z*z;
  if (norm < 0.001) {
    // degenerate: near-zero rotation → arbitrary axis
    x = 1.0; y = 0.0; z = 0.0;  // cs lines 44-46
  } else {
    norm = std::sqrt(norm);
    x /= norm; y /= norm; z /= norm;  // cs lines 50-52
  }

  switch (k) {
    case 0: return (float)x;
    case 1: return (float)y;
    case 2: return (float)z;
    case 3: return (float)angle;
    default: return 0.0f;
  }
}

}  // namespace

static const ValueOp _reg_eulertoaxisangle{
    // EulerToAxisAngle (TiXL Lib.numbers.vec3.EulerToAxisAngle):
    //   Converts Euler angles (radians, XYZ order) to axis-angle representation.
    //   Outputs: Axis (unit vec3) + Angle (radians).
    // Port order matches in[] read: Rotation.x/y/z, then Axis.x/y/z, Angle.
    // Defaults from EulerToAxisAngle.t3: Rotation={0,0,0}.
    // fork-eulertoaxisangle-vec3-as-3-floats: Rotation input and Axis output as 3 Float ports.
    // fork-eulertoaxisangle-angle-as-float: Angle output is a single Float port.
    {"EulerToAxisAngle", "EulerToAxisAngle",
     {{"Rotation.x", "Rotation",   "Float", true,  0.0f, -6.2832f, 6.2832f, Widget::Vec, {}, false, 3},
      {"Rotation.y", "Rotation.y", "Float", true,  0.0f, -6.2832f, 6.2832f, Widget::Vec, {}, false, 1},
      {"Rotation.z", "Rotation.z", "Float", true,  0.0f, -6.2832f, 6.2832f, Widget::Vec, {}, false, 1},
      {"Axis.x",     "Axis.x",     "Float", false},
      {"Axis.y",     "Axis.y",     "Float", false},
      {"Axis.z",     "Axis.z",     "Float", false},
      {"Angle",      "Angle",      "Float", false}},
     evalEulerToAxisAngle},
    "eulertoaxisangle", runEulerToAxisAngleSelfTest};

// --- EulerToAxisAngle MATH golden -------------------------------------------------------------
// Inputs are in RADIANS (EulerToAxisAngle.cs line 27).
// All hand-computation verified against the euclideanspace.com formula (cs source).
//
// CASE 1: Rotation=(0,0,0) — all-zero → norm < 0.001 → Axis=(1,0,0), Angle=2*acos(1)=0.
//   c1=c2=c3=1, s1=s2=s3=0; w=1, x=0,y=0,z=0; norm=0 < 0.001 → x=1,y=z=0; angle=0.
//
// CASE 2: Rotation=(π,0,0) — heading=π, attitude=0, bank=0.
//   c1=cos(π/2)=0, s1=sin(π/2)=1, c2=c3=1, s2=s3=0.
//   c1c2=0, s1s2=0.
//   w=0*1-0*0=0; x=0*0+0*1=0; y=1*1*1+0*0*0=1; z=0*1*1-1*1*0=0.
//   angle=2*acos(0)=π≈3.14159.
//   norm=0+1+0=1 ≥ 0.001 → x/=1=0, y/=1=1, z/=1=0.
//   Axis=(0,1,0), Angle=π.
//
// CASE 3: Rotation=(π/2,0,0) — heading=π/2.
//   c1=cos(π/4)≈0.7071, s1≈0.7071, c2=c3=1, s2=s3=0.
//   c1c2≈0.7071, s1s2=0.
//   w=0.7071*1-0=0.7071; x=0+0=0; y=0.7071*1*1+0=0.7071; z=0-0=0.
//   angle=2*acos(0.7071)=2*(π/4)=π/2≈1.5708.
//   norm=0.7071²=0.5; sqrt(0.5)≈0.7071. y/=0.7071=1.0.
//   Axis=(0,1,0), Angle=π/2.
int runEulerToAxisAngleSelfTest(bool injectBug) {
  const float eps = 1e-4f;  // trig/acos need slightly wider eps
  bool ok = true;

  auto evalE2A = [&](float rx, float ry, float rz, const char* outPort) -> float {
    const NodeSpec* spec = findSpec("EulerToAxisAngle");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "EulerToAxisAngle";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["Rotation.x"] = rx;
    g.node(nid)->params["Rotation.y"] = ry;
    g.node(nid)->params["Rotation.z"] = rz;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == outPort) { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  static const float PI = 3.14159265358979f;

  // CASE 1: (0,0,0) → Axis=(1,0,0), Angle=0 (t3 defaults, degenerate path).
  {
    float ax = evalE2A(0,0,0, "Axis.x");
    bool pass = std::fabs(ax - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-eulertoaxisangle] (0,0,0).Axis.x=%.5f want=1.00000 (degenerate) -> %s\n",
           ax, pass ? "PASS" : "FAIL");
  }
  {
    float angle = evalE2A(0,0,0, "Angle");
    bool pass = std::fabs(angle) < eps;
    ok = ok && pass;
    printf("[selftest-eulertoaxisangle] (0,0,0).Angle=%.5f want=0.00000 -> %s\n",
           angle, pass ? "PASS" : "FAIL");
  }

  // CASE 2: (π,0,0) → Axis=(0,1,0), Angle=π.
  // injectBug: claim Axis.y=0 instead of 1 → RED (heading=π must produce Y-axis).
  {
    float ay = evalE2A(PI, 0, 0, "Axis.y");
    float want = injectBug ? 0.0f : 1.0f;  // bug: return wrong axis y → RED
    bool pass = std::fabs(ay - want) < eps;
    ok = ok && pass;
    printf("[selftest-eulertoaxisangle] (pi,0,0).Axis.y=%.5f want=%.5f -> %s\n",
           ay, want, pass ? "PASS" : "FAIL");
  }
  {
    float angle = evalE2A(PI, 0, 0, "Angle");
    bool pass = std::fabs(angle - PI) < eps;
    ok = ok && pass;
    printf("[selftest-eulertoaxisangle] (pi,0,0).Angle=%.5f want=%.5f (pi) -> %s\n",
           angle, PI, pass ? "PASS" : "FAIL");
  }
  {
    float ax = evalE2A(PI, 0, 0, "Axis.x");
    bool pass = std::fabs(ax) < eps;  // x=0
    ok = ok && pass;
    printf("[selftest-eulertoaxisangle] (pi,0,0).Axis.x=%.5f want=0.00000 -> %s\n",
           ax, pass ? "PASS" : "FAIL");
  }

  // CASE 3: (π/2,0,0) → Axis=(0,1,0), Angle=π/2.
  //   (heading=π/2, all-Y axis rotation as computed above)
  {
    float ay = evalE2A(PI/2.0f, 0, 0, "Axis.y");
    bool pass = std::fabs(ay - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-eulertoaxisangle] (pi/2,0,0).Axis.y=%.5f want=1.00000 -> %s\n",
           ay, pass ? "PASS" : "FAIL");
  }
  {
    float angle = evalE2A(PI/2.0f, 0, 0, "Angle");
    bool pass = std::fabs(angle - PI/2.0f) < eps;
    ok = ok && pass;
    printf("[selftest-eulertoaxisangle] (pi/2,0,0).Angle=%.5f want=%.5f (pi/2) -> %s\n",
           angle, PI/2.0f, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
