// Vector3Gizmo value op (value-op self-registration seam leaf — Phase C numbers/vec3 mining).
// TiXL authority: Operators/Lib/numbers/vec3/Vector3Gizmo.cs (verbatim below).
//
//   Vector3Gizmo.cs Update():
//     var gizmoChanged = ShowGizmo.DirtyFlag.IsDirty;
//     if (gizmoChanged)
//         Result.DirtyFlag.Trigger = ShowGizmo.GetValue(context) ? Animated : None;  // EDITOR-ONLY
//     TransformCallback?.Invoke(this, context);                                       // EDITOR-ONLY
//     Result.Value = Position.GetValue(context);                                      // ← THE VALUE
//
//   Vector3Gizmo is an ITransformable: in the TiXL editor the Position can be dragged via an on-canvas
//   3D gizmo (ITransformable.TranslationInput => Position). On the VALUE rail the op is an IDENTITY:
//   its Result IS its Position input, unchanged. Everything else in Update() is editor-side machinery
//   (DirtyFlag trigger toggling for animation redraw + the TransformCallback the editor uses to draw
//   and hit-test the gizmo handle) — it has NO effect on the produced value.
//
//   Ports (from Vector3Gizmo.cs field order):
//     Position  = InputSlot<Vector3>  (the gizmo-editable position)
//     ShowGizmo = InputSlot<bool>     (editor: whether to draw the on-canvas handle)
//     Output: Result = Slot<Vector3>  (= Position)
//   Vector3Gizmo.t3 DefaultValues: Position = {X:0,Y:0,Z:0}, ShowGizmo = true.
//
// EVAL-SIDE LAYOUT (flat path — no multiInput):
//   in[] = [Position.x, Position.y, Position.z, ShowGizmo]  (n = 4).
//   Output ports Result.x/.y/.z follow at spec indices 4/5/6.
//   Component k = outIdx - n (0=x, 1=y, 2=z).  eval: Result[k] = Position[k]  (identity).
//
// FORKS (named):
//   - fork-vector3gizmo-vec3-as-3-floats (precedent: fork-addvec3-vec3-as-3-floats): the Vector3
//     Position/Result are 3 consecutive Float ports. Identity passthrough — byte-exact to TiXL.
//   - fork-vector3gizmo-gizmo-is-editor-only: TiXL's on-canvas drag gizmo + DirtyFlag.Trigger toggle
//     + TransformCallback are EDITOR-layer behaviour (ITransformable). They are NOT part of the value
//     computation and have no host value-rail equivalent here. This port models the VALUE semantics
//     only: Result = Position (identity). ShowGizmo is carried as an inert parity port (toggling it
//     does not change the produced value — exactly as in TiXL, where ShowGizmo only governs whether
//     the editor draws the handle, never the output value). A node author who wants the gizmo's value
//     simply wires/edits Position, the same value the gizmo would have written. NO divergence on the
//     value path; the divergence is the (absent here) interactive editor affordance.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

int runVector3GizmoSelfTest(bool injectBug);

namespace {

// in[] = [Position.x, Position.y, Position.z, ShowGizmo]  (n = 4).
// Result = Position (identity). ShowGizmo (in[3]) is inert on the value path (editor-only).
float evalVector3Gizmo(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 3) return 0.0f;          // need 3 Position components
  const int k = outIdx - n;        // 0=x, 1=y, 2=z (4 inputs precede the 3 outputs)
  if (k < 0 || k > 2) return 0.0f;
  return in[k];                    // fork-vector3gizmo-gizmo-is-editor-only: Result = Position
}

}  // namespace

// Self-registration. File-scope static ValueOp — independent leaf (no shared edit point).
static const ValueOp _reg_vector3gizmo{
    // Vector3Gizmo (TiXL Lib.numbers.vec3.Vector3Gizmo): Result = Position (the gizmo-editable
    // position; on the value rail this is an identity passthrough — the gizmo handle is editor-only).
    // Port order MUST match evalVector3Gizmo's in[] read: Position.x/y/z, ShowGizmo, then Result.x/y/z.
    // Defaults from Vector3Gizmo.t3: Position = {0,0,0}, ShowGizmo = true (1).
    {"Vector3Gizmo", "Vector3Gizmo",
     {{"Position.x", "Position",   "Float", true, 0.0f, -10000.0f, 10000.0f, Widget::Vec, {}, false, 3},
      {"Position.y", "Position.y", "Float", true, 0.0f, -10000.0f, 10000.0f, Widget::Vec, {}, false, 1},
      {"Position.z", "Position.z", "Float", true, 0.0f, -10000.0f, 10000.0f, Widget::Vec, {}, false, 1},
      {"ShowGizmo",  "ShowGizmo",  "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool},
      {"Result.x", "Result.x", "Float", false},
      {"Result.y", "Result.y", "Float", false},
      {"Result.z", "Result.z", "Float", false}},
     evalVector3Gizmo},
    "vector3gizmo", runVector3GizmoSelfTest};

// --- Vector3Gizmo MATH golden ------------------------------------------------------------------
// Builds 1-node Vector3Gizmo graph, sets Position components + ShowGizmo, pulls Result.x/.y/.z via
// evalFloat (flat path). Hand-computed from Vector3Gizmo.cs: Result = Position (identity); ShowGizmo
// is inert. injectBug asserts a wrong Result.x (negated) → flips RED.
int runVector3GizmoSelfTest(bool injectBug) {
  const float eps = 1e-5f;
  bool ok = true;

  auto evalGizmo = [&](float px, float py, float pz, float showGizmo,
                       const char* outPort) -> float {
    const NodeSpec* spec = findSpec("Vector3Gizmo");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "Vector3Gizmo";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["Position.x"] = px; g.node(nid)->params["Position.y"] = py;
    g.node(nid)->params["Position.z"] = pz; g.node(nid)->params["ShowGizmo"] = showGizmo;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == outPort) { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // TYPICAL: Position=(3,-7,42), ShowGizmo=1 → Result=(3,-7,42) (identity).
  // injectBug: claim Result.x = -3 (negated) → RED.
  {
    float rx = evalGizmo(3.0f, -7.0f, 42.0f, 1.0f, "Result.x");
    float want = injectBug ? -3.0f : 3.0f;  // bug: claim negation → RED
    bool pass = std::fabs(rx - want) < eps;
    ok = ok && pass;
    printf("[selftest-vector3gizmo] Gizmo((3,-7,42)).x=%.5f want=%.5f -> %s\n",
           rx, want, pass ? "PASS" : "FAIL");
  }
  {
    float ry = evalGizmo(3.0f, -7.0f, 42.0f, 1.0f, "Result.y");
    bool pass = std::fabs(ry - (-7.0f)) < eps;
    ok = ok && pass;
    printf("[selftest-vector3gizmo] Gizmo((3,-7,42)).y=%.5f want=-7.00000 -> %s\n",
           ry, pass ? "PASS" : "FAIL");
  }
  {
    float rz = evalGizmo(3.0f, -7.0f, 42.0f, 1.0f, "Result.z");
    bool pass = std::fabs(rz - 42.0f) < eps;
    ok = ok && pass;
    printf("[selftest-vector3gizmo] Gizmo((3,-7,42)).z=%.5f want=42.00000 -> %s\n",
           rz, pass ? "PASS" : "FAIL");
  }

  // ShowGizmo INERT: toggling ShowGizmo=0 must NOT change the value (editor-only port).
  {
    float rxOn  = evalGizmo(5.0f, 6.0f, 7.0f, 1.0f, "Result.x");
    float rxOff = evalGizmo(5.0f, 6.0f, 7.0f, 0.0f, "Result.x");
    bool pass = std::fabs(rxOn - 5.0f) < eps && std::fabs(rxOff - 5.0f) < eps;
    ok = ok && pass;
    printf("[selftest-vector3gizmo] ShowGizmo inert: on=%.5f off=%.5f want=5.00000 both -> %s\n",
           rxOn, rxOff, pass ? "PASS" : "FAIL");
  }

  // DEFAULTS (t3): Position=(0,0,0) → Result=(0,0,0).
  {
    float rx = evalGizmo(0.0f, 0.0f, 0.0f, 1.0f, "Result.x");
    bool pass = std::fabs(rx) < eps;
    ok = ok && pass;
    printf("[selftest-vector3gizmo] Gizmo(t3 defaults).x=%.5f want=0.00000 -> %s\n",
           rx, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
