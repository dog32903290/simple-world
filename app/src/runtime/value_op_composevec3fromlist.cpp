// ComposeVec3FromList value op (FloatList-consumer → host-value(Vector3)-emit leaf, WITH cross-frame
// spring-damp STATE — the FIRST stateful host-value(Vec3) emit). TiXL authority:
// external/tixl/Operators/Lib/numbers/floats/conversion/ComposeVec3FromList.cs:
//
//   Update() (cs:16-33):
//     indexForX/Y/Z = IndexFor{X,Y,Z}.GetValue(context);       // int
//     _inputRange  = InputRange.GetValue(context);             // Vector2
//     _outputRange = OutputRange.GetValue(context);            // Vector2
//     _damping     = SpringDamping.GetValue(context);          // float
//     _springConstant = 100f / (_damping + 0.001f);            // cs:25
//     list = Input.GetValue(context);                          // List<float>
//     Result = Vector3( PickValue(list, indexForX, ref _dampedX,  ref _dampedXVelocity),
//                       PickValue(list, indexForY, ref _dampedY,  ref _dampedYVelocity),
//                       PickValue(list, indexForZ, ref _dampedZ,  ref _dampedZVelocity) );
//
//   PickValue(list, index, ref damped, ref dampVelocity) (cs:35-52):
//     if (list == null || index < 0 || index >= list.Count) return float.NaN;   // cs:37-38
//     f = list[index];
//     if (float.IsNaN(damped))      damped = f;                                  // cs:41-42 (seed)
//     if (float.IsNaN(dampVelocity)) dampVelocity = 0;                           // cs:43-44
//     remapped = Remap(f, _inputRange.X, _inputRange.Y, _outputRange.X, _outputRange.Y);  // cs:46
//     if (_damping == 0) return remapped;                                        // cs:47-48 (no damp)
//     damped = SpringDamp(remapped, damped, ref dampVelocity, _springConstant);  // cs:50 (4-arg → dt=1/60)
//     return damped;
//
//   MathUtils.SpringDamp(target, current, ref velocity, springConstant, timeStep=1/60f) (MathUtils.cs:484):
//     springForce  = (target - current) * springConstant;
//     dampingForce = -velocity * 2 * sqrt(springConstant);
//     velocity += (springForce + dampingForce) * timeStep;
//     return current + velocity * timeStep;
//   MathUtils.Remap(v, inMin, inMax, outMin, outMax) = outMin + (v-inMin)/(inMax-inMin) * (outMax-outMin).
//
//   Ports: Input=InputSlot<List<float>>; IndexForX/Y/Z=InputSlot<int>(0/1/2); InputRange/OutputRange=
//          InputSlot<Vector2>; SpringDamping=InputSlot<float>. Output: Result=Slot<Vector3>.
//
// This is a HOST-EMIT op (evaluate==nullptr): its value comes from gathering the FloatList currency +
// the resolved index/range/damping params — NOT a pure evaluate(Float...). Cooked once per frame by
// cookComposeVec3Nodes (below), writing ResidentNode::extOut[the 3 Result.* output slots] — the EXACT
// channel evalResidentFloat reads for a !evaluate node. Structural twin of value_op_pickcolorfromlist.cpp
// (ColorList-consumer → vec4 emit), but: FloatList input (cookResidentFloatList), Vector3 out (3 ports),
// and CROSS-FRAME spring-damp state per axis (the new part — a process-static store keyed by resident path,
// mirror of residentFloatListState / s_colorListState).
//
// FORKS (named):
//   - fork-composevec3-vec3-as-3-floats: TiXL Result is ONE Slot<Vector3>; sw decomposes into 3 Float
//     output ports Result.x/.y/.z (the established vecN-as-N-floats fork).
//   - fork-int-dissolve-to-float: IndexForX/Y/Z are InputSlot<int>; sw has no Int port → they ride Float
//     ports, recovered with std::lround (Cut32). InputRange/OutputRange Vector2 = 2 Float ports (.x/.y).
//   - fork-composevec3-fixed-dt-60: the 4-arg SpringDamp call (cs:50) leaves timeStep at its 1/60f default
//     (NOT frame-duration-scaled — that is the OTHER overload SpringDampFloat). Ported EXACTLY: dt=1/60.
//   - fork-composevec3-state-per-resident-path: the per-axis _dampedX/Y/Z + velocities persist frame→frame
//     in a process-static map keyed by resident path (the SAME pattern residentFloatListState uses — the
//     Vec3 rail has no single cross-frame store otherwise). NaN-seeded (cs:41-44): the first cook seeds
//     `damped = f` and velocity = 0. Cooked once/frame by the single cookComposeVec3Nodes pass → the damp
//     advances once per frame (no double-advance, unlike a fan-out-pulled op).
#include "runtime/resident_eval_graph.h"  // ResidentEvalGraph / ResidentEvalCtx / cookResidentFloatList

#include <cmath>   // std::lround, std::sqrt, std::isnan, NAN
#include <map>
#include <string>
#include <vector>

#include "runtime/graph.h"              // NodeSpec / PortSpec / Widget / findSpec
#include "runtime/value_op_registry.h"  // ValueOp self-registration + valueOpSelfTests()

namespace sw {

int runComposeVec3FromListSelfTest(bool injectBug);

namespace {

// Per-axis cross-frame damp state (ComposeVec3FromList.cs:54-56, NaN-seeded per PickValue's cs:41-44).
struct AxisDamp { float damped = NAN; float velocity = NAN; };
struct ComposeVec3State { AxisDamp x, y, z; };

// Process-lifetime store keyed by resident path — the Vec3-emit twin of residentFloatListState. A graph
// without ComposeVec3FromList never creates an entry (no leak).
std::map<std::string, ComposeVec3State>& composeVec3State() {
  static std::map<std::string, ComposeVec3State> s;
  return s;
}

// Test-only injection seam (golden): when set, cookComposeVec3Nodes SKIPS the SpringDamp advance and
// emits the RAW remapped value instead — so a damped trajectory (damping>0, mid-frame) diverges from the
// correct spring-damped value. Corrupts the REAL cook output, not the expected value. Off in production.
bool& composeVec3InjectBug() {
  static bool b = false;
  return b;
}

// MathUtils.Remap (MathUtils.cs): linear remap; a zero input span divides by zero exactly like C# float
// (→ ±Inf/NaN), but callers use non-degenerate ranges (the golden does).
float remap(float v, float inMin, float inMax, float outMin, float outMax) {
  return outMin + (v - inMin) / (inMax - inMin) * (outMax - outMin);
}

// MathUtils.SpringDamp (MathUtils.cs:484-498), 4-arg form (timeStep defaults 1/60f — cs:50's call).
float springDamp(float target, float current, float& velocity, float springConstant) {
  const float timeStep = 1.0f / 60.0f;
  const float springForce = (target - current) * springConstant;
  const float dampingForce = -velocity * 2.0f * std::sqrt(springConstant);
  velocity += (springForce + dampingForce) * timeStep;
  return current + velocity * timeStep;
}

// PickValue (ComposeVec3FromList.cs:35-52): index-out-of-range → NaN; else remap + optional spring-damp.
// `damped`/`velocity` are the persistent per-axis state (NaN-seeded). `rawOnly` = injectBug: skip the damp.
float pickValue(const std::vector<float>& list, int index, float& damped, float& velocity,
                float inMinV, float inMaxV, float outMinV, float outMaxV, float damping, bool rawOnly) {
  if (index < 0 || index >= (int)list.size()) return NAN;  // cs:37-38
  const float f = list[(size_t)index];
  if (std::isnan(damped)) damped = f;        // cs:41-42 seed
  if (std::isnan(velocity)) velocity = 0.0f;  // cs:43-44 seed
  const float remapped = remap(f, inMinV, inMaxV, outMinV, outMaxV);  // cs:46
  if (damping == 0.0f || rawOnly) return remapped;  // cs:47-48 (rawOnly = injectBug's damp-skip tooth)
  damped = springDamp(remapped, damped, velocity, 100.0f / (damping + 0.001f));  // cs:25,50
  return damped;
}

}  // namespace

// Per-frame PRODUCTION cook-emit for ComposeVec3FromList: gather the wired FloatList input through the
// resident drivers, pick 3 indexed values, remap + spring-damp each (advancing the per-node cross-frame
// state), and emit the Vector3 onto the 3 Result.* extOut slots. Called from cook_host_values.cpp AFTER
// cookHostScalarNodes (so its FloatList producer is settled). Mutates g (writes extOut) + the state store.
void cookComposeVec3Nodes(ResidentEvalGraph& g, const ResidentEvalCtx& ctx) {
  for (ResidentNode& rn : g.nodes) {
    if (rn.opType != "ComposeVec3FromList") continue;
    const NodeSpec* s = findSpec(rn.opType);
    if (!s) continue;

    // Map the 3 Result.x/.y/.z OUTPUT ports to their extOut[] slots (the port's FULL spec index, inputs
    // included — the same readback rule PickColorFromList uses).
    int outSlot[3] = {-1, -1, -1};
    { int comp = 0;
      for (size_t i = 0; i < s->ports.size() && comp < 3; ++i)
        if (!s->ports[i].isInput) outSlot[comp++] = (int)i; }
    if (outSlot[2] < 0 || outSlot[2] >= 8) continue;  // defensive (always 3 outputs)

    // Gather the "Input" FloatList through the resident Connection driver (cookResidentFloatList recurses
    // the upstream producer). An unwired input → empty list → every PickValue returns NaN (cs:37-38).
    const ResidentInput* in = rn.input("Input");
    std::vector<float> list;
    if (in && in->driver == ResidentInput::Driver::Connection)
      cookResidentFloatList(g, in->srcNodePath, ctx, list, 0);

    // Resolve params (int-dissolved indices + Vec2 range components + damping) via the value spine.
    std::map<std::string, float> p = resolveResidentFloatInputs(g, rn, ctx);
    auto pf = [&](const char* k, float d) { auto it = p.find(k); return it != p.end() ? it->second : d; };
    const int ix = (int)std::lround(pf("IndexForX", 0.0f));
    const int iy = (int)std::lround(pf("IndexForY", 1.0f));
    const int iz = (int)std::lround(pf("IndexForZ", 2.0f));
    const float inMin = pf("InputRange.x", 0.0f),  inMax = pf("InputRange.y", 0.0f);
    const float outMin = pf("OutputRange.x", 0.0f), outMax = pf("OutputRange.y", 0.0f);
    const float damping = pf("SpringDamping", 0.0f);
    const bool rawOnly = composeVec3InjectBug();

    ComposeVec3State& st = composeVec3State()[rn.path];  // per-node cross-frame damp (default-created NaN)
    const float rx = pickValue(list, ix, st.x.damped, st.x.velocity, inMin, inMax, outMin, outMax, damping, rawOnly);
    const float ry = pickValue(list, iy, st.y.damped, st.y.velocity, inMin, inMax, outMin, outMax, damping, rawOnly);
    const float rz = pickValue(list, iz, st.z.damped, st.z.velocity, inMin, inMax, outMin, outMax, damping, rawOnly);
    rn.extOut[outSlot[0]] = rx;
    rn.extOut[outSlot[1]] = ry;
    rn.extOut[outSlot[2]] = rz;
  }
}

// Test-only reset of the cross-frame state store (a golden runs several independent trajectories in one
// process; without this the previous trajectory's damp would leak). No production caller.
void resetComposeVec3State() { composeVec3State().clear(); }
// Test-only accessor to arm the injectBug tooth from the golden TU.
bool& composeVec3InjectBugRef() { return composeVec3InjectBug(); }

// Self-registration. File-scope static ValueOp — independent leaf (SW_VALUE_OP_SRCS glob compiles
// value_op_*.cpp). evaluate==nullptr: host-emitted by cookComposeVec3Nodes onto extOut[the 3 Result slots].
//   Ports: Input (FloatList) ; IndexForX/Y/Z (Float, int-dissolved, TiXL defaults 0/0/0 —
//          ComposeVec3FromList.t3) ; InputRange.x/.y = 0/1, OutputRange.x/.y = 0/1 (Vec2 components,
//          TiXL default {0,1}) ; SpringDamping (Float, default 0) ; Result.x/.y/.z (3 Float outputs).
// ★ OUTPUT PORTS FIRST (indices 0/1/2): Result.* must land in extOut[0..2] (float[8]). This op has NINE
// input ports; if the outputs trailed them they would sit at spec indices 9/10/11 — OUT OF BOUNDS for
// extOut[8]/outCache[8], so evalResidentFloat would read 0. Declaring outputs first keeps them addressable
// (the evalResidentFloat readback matches by port id → index; index<8 is the only constraint). The value
// spine reads inputs by id, not position, so leading outputs are safe for the params/gather too.
static const ValueOp _reg_composevec3fromlist{
    {"ComposeVec3FromList", "ComposeVec3FromList",
     {{"Result.x", "Result.x", "Float", false},
      {"Result.y", "Result.y", "Float", false},
      {"Result.z", "Result.z", "Float", false},
      {"Input", "Input", "FloatList", true},
      {"IndexForX", "IndexForX", "Float", true, 0.0f, -100000.0f, 100000.0f},
      {"IndexForY", "IndexForY", "Float", true, 0.0f, -100000.0f, 100000.0f},
      {"IndexForZ", "IndexForZ", "Float", true, 0.0f, -100000.0f, 100000.0f},
      {"InputRange.x", "InputRange", "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 2},
      {"InputRange.y", "InputRange.y", "Float", true, 1.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 1},
      {"OutputRange.x", "OutputRange", "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 2},
      {"OutputRange.y", "OutputRange.y", "Float", true, 1.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 1},
      {"SpringDamping", "SpringDamping", "Float", true, 0.0f, 0.0f, 100.0f}},
     /*evaluate=*/nullptr}};  // host-emit via cookComposeVec3Nodes; golden registered separately below

}  // namespace sw
