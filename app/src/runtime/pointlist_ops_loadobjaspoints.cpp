// LoadObjAsPoints pointlist op — loads a Wavefront .OBJ and emits its vertices as a host SwPoint list.
// FIRST CONSUMER of the obj_parse seam + the pointlist STRING-path channel (fork-pointlist-string-path-
// channel). TiXL authority: external/tixl/Operators/Lib/point/helper/LoadObjAsPoints.cs (mirrored 1:1).
//
//   LoadObjAsPoints.cs Update() (the load-bearing path, distilled):
//     mesh = ObjMesh.TryLoadFromFile(Path);
//     sortedVertexIndices = Range(0, mesh.Positions.Count).ToList();      // RAW position indices (NOT distinct)
//     if (sorting != Ignore) sortedVertexIndices.Sort(by _sortAxisAndDirections[sorting]);  // axis/dir table
//     switch (Mode):
//       Vertices_WithColor:
//         for vertexIndex in [0, Positions.Count):
//           svi = sortedVertexIndices[vertexIndex];
//           c   = (svi >= Colors.Count) ? Vector4.One : Colors[svi];      // no-color → (1,1,1,1)
//           Position    = Positions[svi];
//           Orientation = new Quaternion(c.X, c.Y, c.Z, c.W);             // COLOR-AS-ROTATION quirk
//           F1 = 1; Color = c;
//
// NAMED FORKS:
//   • fork-loadobjaspoints-raw-positions: uses RAW mesh.Positions / mesh.Colors directly (NOT distinct
//     vertices). Faithful to LoadObjAsPoints.cs:64 (Range(0, Positions.Count)). The obj_parse distinct/TBN
//     path (obj_parse_distinct.cpp) is for the LATER LoadObj op, not this one.
//   • fork-loadobjaspoints-color-as-orientation: Vertices_WithColor sets Orientation = Quaternion(c) — the
//     vertex COLOR is reinterpreted as a rotation quaternion (LoadObjAsPoints.cs:147). A genuine TiXL quirk;
//     reproduced, NOT "fixed".
//   • fork-loadobjaspoints-modes-subset: only the three Vertices_* color modes (WithColor / ColorAsGrayScale
//     / GrayscaleAsW) are implemented — they share one loop. AllVertices is UNIMPLEMENTED IN TIXL (:86 logs
//     "Object mode not implemented"); LinesVertices / WireframeLines need the line/face-edge builders and are
//     DEFERRED (named, not silent) → an unimplemented Mode emits an EMPTY list (TiXL logs a warning, same
//     observable: Points.Value stays empty/last).
//   • fork-pointlist-string-path-channel: Path arrives via PointListCookCtx::inputStrings[0] (the small
//     String channel added to the pointlist driver — see point_graph_hostvalue_cook.cpp / pointlist_op_
//     registry.h). The driver gathers it wire-OR-const (Node::strParams["Path"] override, else PortSpec
//     strDef) via the SHARED gatherStringInputs — the SAME wire-OR-const the String rail uses.
//   • fork-pointlist-flat-only-no-resident: the pointlist STRING channel is FLAT-cook only. The resident
//     (production) pointlist driver builds PointListCookCtx with inputStrings == nullptr (no String gather),
//     so a per-instance Path does NOT reach a resident LoadObjAsPoints → it loads nothing (empty list). Same
//     scope as LoadImage's fork[resident-uses-default-path] and every String-rail op (flat-only). The golden
//     drives the FLAT leg (the cook fn directly), which is where the Path wiring lives.
//   • fork-no-hot-reload: TiXL wraps the mesh in Resource<ObjMesh> with a file watcher; sw re-reads the file
//     every cook (objParseFromFile). Observable Result is the current on-disk mesh either way.
//   • fork-loadobjaspoints-pointarray-output-dropped: TiXL's commented-out VertexBuffer/IndexBuffer outputs
//     are absent; sw carries the single PointList (StructuredList) output (the active TiXL output).
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "runtime/graph.h"                   // NodeSpec, PortSpec, Widget
#include "runtime/obj_parse.h"               // objParseFromFile / ObjMesh
#include "runtime/pointlist_op_registry.h"   // PointListOp / PointListCookCtx / pointListInjectBug / swPointDefault
#include "runtime/tixl_point.h"              // SwPoint full def (the host point currency)

namespace sw {

namespace {

// LoadObjAsPoints.cs _sortAxisAndDirections (:45): [axisIndex, direction] for Sorting 0..5; index 6
// (ObjMesh.SortDirections.Ignore) skips the sort.
struct AxisDir { int axis; int dir; };
const AxisDir kSortAxisAndDir[6] = {{0, 1}, {0, -1}, {1, 1}, {1, -1}, {2, 1}, {2, -1}};

// Modes enum (LoadObjAsPoints.cs:336): AllVertices=0, LinesVertices=1, Vertices_WithColor=2,
// Vertices_ColorAsGrayScale=3, Vertices_GrayscaleAsW=4, WireframeLines=5.
enum class Mode { AllVertices = 0, LinesVertices = 1, Vertices_WithColor = 2,
                  Vertices_ColorAsGrayScale = 3, Vertices_GrayscaleAsW = 4, WireframeLines = 5 };

float axisOf(const ObjVec3& p, int axis) { return axis == 0 ? p.x : (axis == 1 ? p.y : p.z); }

void cookLoadObjAsPoints(PointListCookCtx& c) {
  if (!c.output) return;
  c.output->clear();

  // Path: PointListCookCtx::inputStrings[0] (the String channel). null (resident) / empty → no mesh.
  std::string path;
  if (c.inputStrings && !c.inputStrings->empty()) path = (*c.inputStrings)[0];

  ObjMesh mesh;
  if (!objParseFromFile(path, mesh)) {
    // No mesh (empty/invalid path, unreadable file, or TiXL's load gate failed) → empty list (TiXL logs
    // "No mesh found" and returns without touching Points → Points.Value stays at its prior/empty value).
    if (pointListInjectBug()) c.output->clear();
    return;
  }

  const int posCount = (int)mesh.positions.size();
  const int colCount = (int)mesh.colors.size();

  // sortedVertexIndices = Range(0, Positions.Count), then sort by the axis/dir table unless Ignore.
  std::vector<int> sortedVertexIndices((size_t)posCount);
  for (int i = 0; i < posCount; ++i) sortedVertexIndices[(size_t)i] = i;
  int sorting = (int)(pointListParam(c.params, "Sorting", 6.0f) + 0.5f);  // default Ignore (6)
  if (sorting >= 0 && sorting < 6) {
    int axis = kSortAxisAndDir[sorting].axis;
    int dir = kSortAxisAndDir[sorting].dir;
    // .NET List.Sort with a comparator returning CompareTo*dir. fork-loadobjaspoints-stable-sort-tieorder:
    // .NET List<T>.Sort is UNSTABLE (introsort), so equal-axis vertices may land in a TiXL-different order;
    // sw uses std::stable_sort, which keeps ties in original index order. Named fork — only observable when
    // two+ vertices share the sort-axis value AND a non-Ignore Sorting is selected (the DEFAULT mode is
    // Ignore = no sort, so this is usually moot).
    std::stable_sort(sortedVertexIndices.begin(), sortedVertexIndices.end(), [&](int a, int b) {
      float va = axisOf(mesh.positions[(size_t)a], axis);
      float vb = axisOf(mesh.positions[(size_t)b], axis);
      return dir > 0 ? (va < vb) : (va > vb);
    });
  }

  Mode mode = (Mode)(int)(pointListParam(c.params, "Mode", 2.0f) + 0.5f);  // default Vertices_WithColor (2)
  switch (mode) {
    case Mode::Vertices_WithColor:
    case Mode::Vertices_ColorAsGrayScale:
    case Mode::Vertices_GrayscaleAsW: {
      c.output->reserve((size_t)posCount);
      for (int vertexIndex = 0; vertexIndex < posCount; ++vertexIndex) {
        int svi = sortedVertexIndices[(size_t)vertexIndex];
        // c = (svi >= Colors.Count) ? Vector4.One : Colors[svi]  (LoadObjAsPoints.cs:110-112)
        simd::float4 col = (svi >= colCount)
                               ? simd::float4{1.0f, 1.0f, 1.0f, 1.0f}
                               : simd::float4{mesh.colors[(size_t)svi].x, mesh.colors[(size_t)svi].y,
                                              mesh.colors[(size_t)svi].z, mesh.colors[(size_t)svi].w};
        const ObjVec3& p = mesh.positions[(size_t)svi];
        SwPoint sp = swPointDefault();
        sp.Position = {p.x, p.y, p.z};
        if (mode == Mode::Vertices_GrayscaleAsW) {
          sp.FX1 = (col.x + col.y + col.z) / 3.0f;  // F1 = gray
          sp.Color = col;
        } else if (mode == Mode::Vertices_ColorAsGrayScale) {
          float gray = (col.x + col.y + col.z) / 3.0f;
          sp.FX1 = 1.0f;
          sp.Color = {gray, gray, gray, gray};       // new Vector4(gray) — all four lanes = gray
        } else {                                      // Vertices_WithColor
          sp.Rotation = col;                          // Orientation = Quaternion(c.X,c.Y,c.Z,c.W) (color-as-rotation)
          sp.FX1 = 1.0f;
          sp.Color = col;
        }
        c.output->push_back(sp);
      }
      break;
    }
    default:
      // AllVertices (unimplemented in TiXL) / LinesVertices / WireframeLines (deferred) → empty list.
      break;
  }

  // Test-only: corrupt the REAL output → CLEAR the whole list (bites transport readback / GPU count 0 /
  // production black). Off in production. (Same hook as every pointlist leaf.)
  if (pointListInjectBug()) c.output->clear();
}

}  // namespace

// Self-registration. ONE PointList output "Points" + Path(String, wire-OR-const) + Mode/Sorting(Float enum).
// PortSpec positional: {id, name, dataType, isInput, def, minV, maxV, widget, labels, pinless, vecArity,
//                       multiInput, strDef}.
static const PointListOp _reg_loadobjaspoints{
    {"LoadObjAsPoints", "LoadObjAsPoints",
     {{"Points", "Points", "PointList", false},
      // Path (String input, wire-OR-const). strDef "" → no mesh by default (TiXL's Path has no default
      // asset; the user must point it at an .obj). Last positional field.
      {"Path", "Path", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, ""},
      // Mode enum (LoadObjAsPoints.cs:336 order). Default Vertices_WithColor (index 2).
      {"Mode", "Mode", "Float", true, 2.0f, 0.0f, 5.0f, Widget::Enum,
       {"AllVertices", "LinesVertices", "Vertices_WithColor", "Vertices_ColorAsGrayScale",
        "Vertices_GrayscaleAsW", "WireframeLines"},
       true},
      // Sorting enum (ObjMesh.SortDirections, :437). Default Ignore (index 6 → no sort).
      {"Sorting", "Sorting", "Float", true, 6.0f, 0.0f, 6.0f, Widget::Enum,
       {"XForward", "XBackwards", "YForward", "YBackwards", "ZForward", "ZBackwards", "Ignore"},
       true}},
     /*evaluate=*/nullptr},
    cookLoadObjAsPoints};

}  // namespace sw
