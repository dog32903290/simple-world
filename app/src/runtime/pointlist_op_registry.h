// runtime/pointlist_op_registry — self-registration seam for POINTLIST ops (the 7th cook flow).
//
// The PointList channel is a HOST-side CPU point-list currency — TiXL's Slot<StructuredList> /
// StructuredList<Point> (a CPU Point[] that rides between the _cpu point ops), NOT a GPU buffer. It is
// the CPU-side parallel of the GPU "Points" flow (BufferWithViews): a producer port (dataType==
// "PointList" output) hands a std::vector<SwPoint> to a consumer port (dataType=="PointList" input).
// The list lives in HOST memory the whole way; it never touches the 16-byte GPU EvaluationContext and
// never allocates a Metal buffer here. The UPLOAD BRIDGE (ListToBuffer) is the ONLY crossing to the GPU
// flow: it memcpys a host PointList into a GPU SwPoint buffer (a Points-producing op), after which the
// existing DrawPoints/RenderTarget GPU path consumes it with ZERO changes (point_graph.cpp cookNode).
//
// Pattern cloned VERBATIM from floatlist_op_registry.h (the 5th cook flow) — float→SwPoint. Adding a
// pointlist op = add ONE leaf .cpp ending with a file-scope `PointListOp` registrar. The registrar
// feeds two sinks:
//   (1) pointListSpecSink()   — its NodeSpec (so it appears in the Add menu / findSpec, like any op),
//   (2) pointListCookFns()    — its PointListCookFn (so the cook driver's cookPointListNode runs it).
//
// Init-order safety (identical to the floatlist / string / value-op sinks): every registrar is a
// namespace-scope static, so all finish their dynamic-init constructors before main and before any
// LIVE sink read (node_registry's findSpec/specTypes read the sink live, never snapshot).
//
// FORK (named): cpupoint-reuses-swpoint — TiXL's StructuredList<Point> rides T3.Core.DataTypes.Point
//   (Stride=64, Position@0 / F1@12 / Orientation@16 / Color@32 / Scale@48 / F2@60). sw reuses the
//   existing SwPoint (tixl_point.h, SAME 64B stride + offsets) instead of inventing a new host type;
//   the field rename is Point.Orientation→SwPoint.Rotation and Point.F1→SwPoint.FX1 / Point.F2→FX2
//   (the SAME rename the GPU 四流 already adopted). A fresh SwPoint must be seeded with TiXL's `new
//   Point()` defaults (F1=1, Rotation=identity quat, Color=(1,1,1,1), Scale=(1,1,1), F2=1) — a
//   default-constructed SwPoint is raw zeros, which is NOT the TiXL default; leaves call swPointDefault().
//
// FORK (named): intra-family ORDER in the sink follows cross-TU dynamic-init order (unspecified).
//   Cosmetic only (Add-menu position); findSpec is keyed by type name, the cook by type name.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "runtime/graph.h"        // NodeSpec

namespace MTL {
class Device;
class Library;
class CommandQueue;
class Buffer;
}  // namespace MTL

struct EvaluationContext;  // runtime/eval_context.h
// Forward-declare SwPoint (global scope, full def in runtime/tixl_point.h). This header DOES NOT pull
// tixl_point.h on purpose: tixl_point.h and the legacy Particle.h both define `struct Particle`
// independently and MUST NOT co-occur in one TU (graph.h carries the same rule). node_registry.cpp
// includes this header only for pointListSpecSink() (NodeSpec, no SwPoint) — an incomplete SwPoint is
// fine in the declarations below (std::vector<SwPoint> members / a by-value return only need the
// complete type at INSTANTIATION, which happens in the .cpp leaves + drivers that DO include tixl_point.h).
struct SwPoint;

namespace sw {

// TiXL `new Point()` defaults (Point.cs:27-35) on a fresh SwPoint: Position=0, F1(=FX1)=1,
// Orientation(=Rotation)=identity quaternion (0,0,0,1), Color=(1,1,1,1), Scale=(1,1,1), F2(=FX2)=1.
// A default-constructed SwPoint is raw bytes (zeros) — NOT this — so every leaf that CREATES a point
// must start from swPointDefault(), exactly as TiXL's `new Point` seeds the struct.
SwPoint swPointDefault();

// Everything a pointlist op gets to cook one node this frame. Mirrors FloatListCookCtx
// (floatlist_op_registry.h) but the currency is a HOST std::vector<SwPoint>, not a host float list —
// so (like floatListBuf) there is NO pre-sizing (the vector self-sizes; the op clears + fills it) and
// NO Metal allocation. The dev/lib/queue refs ride along for symmetry (the upload bridge ListToBuffer
// is NOT a pointlist op — it is a Points op; it lives in the cook driver, not here).
//
//   inputLists : the already-cooked upstream PointList inputs (in spec port order, MultiInput-expanded
//                into wire-declaration order). A pure producer (RadialPointsCpu) has none → the vector
//                is empty. A consumer (TransformCpuPoint) reads inputLists->at(i) for its i-th source.
//   output     : THIS node's host point list. The cook driver owns it (in Impl::pointListBuf, keyed by
//                flatKey(id) or resident path); the op WRITES into *output (clear + fill).
//   params     : RESOLVED Float params of THIS node (same value spine as FloatListCookCtx::params) —
//                the cook driver resolves every Float input port and hands the result here. Vector
//                params are keyed by component port id ("Center.x"/".y"/".z"), read via pointListVec3.
struct PointListCookCtx {
  MTL::Device* dev = nullptr;
  MTL::Library* lib = nullptr;
  MTL::CommandQueue* queue = nullptr;
  const EvaluationContext* ctx = nullptr;  // time / frameIndex / deltaTime
  int nodeId = 0;
  // Cooked upstream PointList inputs (one entry per WIRED PointList source, in spec port order with
  // MultiInput ports expanded into wire-declaration order). Borrowed (driver-owned); never retained.
  const std::vector<std::vector<SwPoint>>* inputLists = nullptr;
  // STRING-input channel (fork-pointlist-string-path-channel): the cooked String input(s) of THIS node,
  // in spec port order, gathered wire-OR-const (a wired upstream String → its cooked value; an unwired
  // String input → Node::strParams[id] override, else PortSpec.strDef). This is a SMALL mirror of
  // StringCookCtx::inputStrings — added so a pointlist op can carry a file PATH (the OBJ-IO sub-lane:
  // LoadObjAsPoints.Path). params is map<string,float> (float-only) and cannot carry a path; the driver
  // (cookFlatPointList) gathers String inputs via the SHARED gatherStringInputs (point_graph_string_cook.cpp)
  // exactly as the String rail does. Empty (no String input ports) for every prior pointlist op. Borrowed.
  const std::vector<std::string>* inputStrings = nullptr;
  // POINTS-bag input (the GPU→host point-readback rail-crossing, the DOWNLOAD mirror of ListToBuffer's
  // host→GPU upload): the already-cooked upstream Points buffer wired to a pointlist op's "PointBuffer"
  // input port, + its point count. A pointlist op that READS a GPU point bag (PointsToCPU) copies whole
  // SwPoints (all 64 bytes) out of the bag into the host list. The bag is StorageModeShared and the
  // upstream point op committed+waited during its own cook, so contents() is valid CPU-side here (same
  // posture as ColorListCookCtx::inputPointsBag for ReadPointColors). null/0 for every pointlist op with
  // no Points input (RadialPointsCpu / LinePoints CPU / TransformCpuPoint / ListToBuffer).
  const MTL::Buffer* inputPointsBag = nullptr;
  uint32_t inputPointsCount = 0;
  // Driver-owned output list. The op writes via output->clear()/push_back; never allocates/frees it.
  std::vector<SwPoint>* output = nullptr;
  // RESOLVED Float params of THIS node (mirror of FloatListCookCtx::params); read via pointListParam.
  const std::map<std::string, float>* params = nullptr;
};

// A pointlist op: read inputLists (+ resolved Float params) → write *output. ONE fn (a host vector
// self-sizes — the driver never pre-allocates it).
using PointListCookFn = void (*)(PointListCookCtx&);

// Read a Float param from a PointListCookCtx's RESOLVED map (mirror of floatListParam); `def` when the
// driver supplied no map (ops invoked outside a cook driver, e.g. a hand-built ctx in a golden).
float pointListParam(const std::map<std::string, float>* params, const char* id, float def);

// Read a vec3 param (component-keyed "<base>.x"/".y"/".z") from a resolved map; component missing →
// fallback[i]. Mirror of readVecN/cookVecN for the pointlist ops (RadialPointsCpu.Center/Axis/Offset,
// TransformCpuPoint.Translation/Rotation).
void pointListVec3(const std::map<std::string, float>* params, const char* base, const float* fallback,
                   float* out);

// --- the two sinks every pointlist-op leaf registrar feeds ---
std::vector<NodeSpec>& pointListSpecSink();              // NodeSpecs (node_registry reads live)
std::map<std::string, PointListCookFn>& pointListCookFns();  // type-name -> cook fn

// Lookup the cook fn for a type (nullptr if not a pointlist op). Used by the cook driver's dispatch.
const PointListCookFn* findPointListOp(const std::string& type);

// Test-only injection seam (goldens): when set, a pointlist op's cook corrupts its REAL output (drops
// the last point) so the golden's RED case fires on the actual cook path (NOT by flipping the expected
// value). Off in production. A leaf reads it at the end of its cook.
bool& pointListInjectBug();

// RAII registrar: declare one file-scope static of this type at the end of each pointlist-op leaf.
//   PointListOp(spec, cookFn);  // pushes spec into pointListSpecSink() and cook into pointListCookFns()
struct PointListOp {
  PointListOp(NodeSpec spec, PointListCookFn cook);
};

}  // namespace sw
