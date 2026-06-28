// LoadObjEdges pointlist op — loads a Wavefront .OBJ and emits its UNIQUE FACE EDGES as line-segment point
// triples (from → to → Separator). SECOND CONSUMER of the obj_parse seam (after LoadObjAsPoints). TiXL
// authority: external/tixl/Operators/Lib/mesh/_/LoadObjEdges.cs (mirrored 1:1).
//
//   LoadObjEdges.cs Update() (the load-bearing path, distilled):
//     mesh = ObjMesh.TryLoadFromFile(Path);
//     hashSet = new HashSet<uint>();
//     foreach face f: InsertVertexPair(f.V0,f.V1); InsertVertexPair(f.V1,f.V2); InsertVertexPair(f.V2,f.V0);
//       InsertVertexPair(from,to): if (from < to) swap(from,to);  combined = (to << 16) + from;  hashSet.Add(combined);
//       // after the swap, `from` holds the LARGER index, `to` the SMALLER; pack puts SMALLER in the high 16
//       // bits (to<<16) and LARGER in the low 16 bits (+from).
//     _pointList.SetLength(count * 3);
//     foreach pair in hashSet:
//       fromIndex = pair & 0xffff;   // == the LARGER vertex index (low bits)
//       toIndex   = pair >> 16;      // == the SMALLER vertex index (high bits)
//       skip if either index out of [0, _pointList.Length-1]   (a TiXL quirk: bound is count*3-1, NOT
//                                                                Positions.Count-1 — see fork below)
//       emit Point{ Position=mesh.Positions[fromIndex], Orientation=Identity, F1=1 }   // "from"
//       emit Point{ Position=mesh.Positions[toIndex],   Orientation=Identity, F1=1 }   // "to"
//       emit Point.Separator()                                                          // polyline break
//
// NAMED FORKS:
//   • fork-loadobjedges-raw-positions: edge endpoints index mesh.Positions DIRECTLY (RAW positions, NOT
//     distinct vertices). Faithful to LoadObjEdges.cs:70/79.
//   • fork-loadobjedges-16bit-pack-dedup: edges are de-duplicated via a 16-bit pack (to<<16)+from into a
//     hash set, exactly as TiXL (LoadObjEdges.cs:42-51). The order-independent SWAP (larger→`from`) means
//     edge (a,b) and (b,a) collapse to one entry. Reproduced 1:1 (std::unordered_set<uint32_t>).
//   • fork-loadobjedges-separator-nan-scale: each emitted edge ends with Point.Separator() — a default
//     Point with Scale=(NaN,NaN,NaN) (Point.cs:37-46). DrawPoints' vertex shader pushes a NaN-Scale point
//     offscreen (draw_points.metal), so the separator draws nothing = the polyline break between edges.
//     SAME convention as LinePointsCpu / RadialPointsCpu (swPointDefault() + Scale=NaN).
//   • fork-loadobjedges-emit-order: TiXL iterates a HashSet<uint> whose enumeration order is UNSPECIFIED
//     (.NET hash-bucket order); sw iterates std::unordered_set<uint32_t> (also unspecified). The SET of
//     emitted edges is identical; the ORDER of the triples may differ from TiXL (and between sw runs/builds).
//     Named — the golden asserts an order-INDEPENDENT set of edges, never a fixed sequence.
//   • fork-loadobjedges-bound-is-listlen: TiXL's skip-guard checks fromIndex/toIndex against
//     _pointList.TypedElements.Length-1 (= count*3-1), NOT Positions.Count-1 (LoadObjEdges.cs:63-64) — a
//     latent TiXL quirk (the real out-of-range protection is obj_parse's load-fail bounds guard, which
//     already rejects any face index outside Positions). We mirror TiXL's exact guard so the observable
//     matches, but it is effectively unreachable: the parse already failed any out-of-range face index.
//   • fork-pointlist-string-path-channel / fork-pointlist-flat-only-no-resident / fork-no-hot-reload:
//     IDENTICAL to LoadObjAsPoints — Path arrives via PointListCookCtx::inputStrings[0] (FLAT-cook only;
//     resident leaves inputStrings null → loads nothing), and the file is re-read every cook.
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "runtime/graph.h"                   // NodeSpec, PortSpec, Widget
#include "runtime/obj_parse.h"               // objParseFromFile / ObjMesh
#include "runtime/pointlist_op_registry.h"   // PointListOp / PointListCookCtx / pointListInjectBug / swPointDefault
#include "runtime/tixl_point.h"              // SwPoint full def (the host point currency)

namespace sw {

namespace {

// InsertVertexPair (LoadObjEdges.cs:42-51): order-independent 16-bit pack. Swap so `from` is the LARGER
// index, then combined = (smaller << 16) + larger.
uint32_t packEdge(int a, int b) {
  uint32_t from = (uint32_t)a, to = (uint32_t)b;
  if (from < to) { uint32_t t = from; from = to; to = t; }  // (from,to) = (to,from) — `from` becomes larger
  return (to << 16) + from;
}

void cookLoadObjEdges(PointListCookCtx& c) {
  if (!c.output) return;
  c.output->clear();

  // Path: PointListCookCtx::inputStrings[0] (the String channel). null (resident) / empty → no mesh.
  std::string path;
  if (c.inputStrings && !c.inputStrings->empty()) path = (*c.inputStrings)[0];

  ObjMesh mesh;
  if (!objParseFromFile(path, mesh)) {
    // No mesh (empty/invalid path, unreadable file, or the parse load-gate failed) → empty list (TiXL logs
    // an error and returns without touching Data → Data.Value stays at its prior/empty value).
    if (pointListInjectBug()) c.output->clear();
    return;
  }

  // De-dup face edges via the 16-bit pack hash set (LoadObjEdges.cs:33-51).
  std::unordered_set<uint32_t> edges;
  for (const ObjFace& f : mesh.faces) {
    edges.insert(packEdge(f.v0, f.v1));
    edges.insert(packEdge(f.v1, f.v2));
    edges.insert(packEdge(f.v2, f.v0));
  }

  const int posCount = (int)mesh.positions.size();
  const int listLen = (int)edges.size() * 3;  // _pointList length (count*3); TiXL's skip-guard bound.
  c.output->reserve((size_t)listLen);

  // Emit each edge as from → to → Separator (LoadObjEdges.cs:57-90). Iteration order is unspecified
  // (fork-loadobjedges-emit-order); the golden asserts the SET, not the sequence.
  for (uint32_t pair : edges) {
    int fromIndex = (int)(pair & 0xffffu);  // LARGER vertex index (LoadObjEdges.cs:60)
    int toIndex = (int)(pair >> 16);        // SMALLER vertex index (LoadObjEdges.cs:61)
    // TiXL's skip-guard checks against _pointList.Length-1 (= listLen-1), not Positions.Count-1
    // (fork-loadobjedges-bound-is-listlen). Mirror it; obj_parse's load-fail bounds guard means a face
    // index outside Positions never reaches here, so this also keeps mesh.positions[] in range.
    if (fromIndex < 0 || fromIndex > listLen - 1 || toIndex < 0 || toIndex > listLen - 1) continue;
    if (fromIndex >= posCount || toIndex >= posCount) continue;  // safety (unreachable post load-gate)

    const ObjVec3& pFrom = mesh.positions[(size_t)fromIndex];
    SwPoint a = swPointDefault();  // Orientation=Identity, F1=1 set by the default (TiXL new Point())
    a.Position = {pFrom.x, pFrom.y, pFrom.z};
    c.output->push_back(a);

    const ObjVec3& pTo = mesh.positions[(size_t)toIndex];
    SwPoint b = swPointDefault();
    b.Position = {pTo.x, pTo.y, pTo.z};
    c.output->push_back(b);

    SwPoint sep = swPointDefault();  // Point.Separator(): default EXCEPT Scale = NaN (Point.cs:37-46)
    sep.Scale = {std::nanf(""), std::nanf(""), std::nanf("")};  // fork-loadobjedges-separator-nan-scale
    c.output->push_back(sep);
  }

  // Test-only: corrupt the REAL output → CLEAR the whole list (bites readback / GPU count 0). Off in
  // production. (Same hook as every pointlist leaf.)
  if (pointListInjectBug()) c.output->clear();
}

}  // namespace

// Self-registration. ONE PointList output "Data" + Path(String, wire-OR-const).
// PortSpec positional: {id, name, dataType, isInput, def, minV, maxV, widget, labels, pinless, vecArity,
//                       multiInput, strDef}.
static const PointListOp _reg_loadobjedges{
    {"LoadObjEdges", "LoadObjEdges",
     {{"Data", "Data", "PointList", false},
      // Path (String input, wire-OR-const). strDef "" → no mesh by default (TiXL's Path has no default
      // asset; the user must point it at an .obj). Last positional field.
      {"Path", "Path", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, ""}},
     /*evaluate=*/nullptr},
    cookLoadObjEdges};

}  // namespace sw
