// DataPointImportExport pointlist op — imports a JSON point list into the host point-list currency
// (and optionally exports it again). Rides the pointlist STRING-path channel (LoadObjAsPoints
// precedent) + the Points-bag download channel (PointsToCPU precedent).
//
// TiXL authority: external/tixl/Operators/Lib/point/io/DataPointImportExport.cs (mirrored below):
//   Update (:25-62): on Import trigger OR path-change autoload → ImportFileAsync; on Export trigger →
//     ExportFileAsync(bufferIn); PointBufferOut.Value = _pointBuffer ?? bufferIn (imported data wins,
//     else forward the incoming buffer).
//   LoadFromJsonAsync (:243-256): JSON = ARRAY of records; ParseRecord each.
//   ParseRecord (:281-309): Position = GetNestedFloat("Position","X"/"Y"/"Z", 0);
//     Orientation = Quaternion(GetNestedFloat("Orientation","X"/"Y"/"Z",0), W default 1);
//     Scale = GetNestedFloat("Scale","X"/"Y"/"Z", 1); F1 = GetFloat("F1", 0).
//     `new Point{...}` is a C# STRUCT initializer → every field NOT listed stays ZERO: the imported
//     points carry Color=(0,0,0,0) and F2=0 (NOT the `new Point()` defaults). Reproduced verbatim —
//     zero-init the SwPoint, set only the four parsed fields.
//   SaveToJsonAsync (:258-277): writes [{Position:{X,Y,Z},Orientation:{W,X,Y,Z},Scale:{X,Y,Z},F1}...].
//   ExportFileAsync (:103-135): pointsToExport = bufferIn != null ? ToPointList(bufferIn)
//     : _lastImportedPoints (the INPUT buffer wins when wired).
//   Value semantics (GetFloat/GetNestedFloat/ParseFloatString :311-346) live in runtime/datapoint_json.
//
// NAMED FORKS:
//   • fork-datapoint-pointlist-rail: TiXL's currency is BufferWithViews (GPU). sw carries the HOST
//     PointList; the ListToBuffer upload bridge is the sanctioned GPU crossing (pointlist_op_registry.h).
//     PointBufferIn rides PointListCookCtx::inputPointsBag (the PointsToCPU download channel).
//   • fork-datapoint-level-semantics: TiXL's Import is a WasTriggered EDGE (+ autoload on path change);
//     the pointlist cook is stateless-per-frame, so import runs whenever ImportFilePath is readable —
//     the SAME steady state TiXL reaches after its autoload (valid path → imported points every frame;
//     invalid → bufferIn forwarded). The Import port is kept for spec parity but the cook ignores it.
//     Export likewise: a LEVEL bool (≥0.5 → write each cook, idempotent), not an edge.
//   • fork-no-hot-reload: the file is re-read every cook (LoadObjAsPoints precedent) — no Resource<>
//     watcher; the observable output is the current on-disk data either way.
//   • fork-pointlist-flat-only-no-resident: the String channel is FLAT-cook only (registry-wide scope,
//     see pointlist_op_registry.h). The golden drives the flat cook fn directly.
//   • fork-datapoint-lenient-leaf: see runtime/datapoint_json.cpp jsonToFloat (a non-string/number leaf
//     degrades per-field; the .cs GetString() throw would abort the whole import).
//   • fork-datapoint-json-number-format: the export writer prints floats with %.9g (round-trip exact);
//     System.Text.Json writes shortest-round-trip. Consumers parse numerically — no byte contract.
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "runtime/datapoint_json.h"          // jsonParse / jsonFind / jsonToFloat
#include "runtime/graph.h"                   // NodeSpec, PortSpec, Widget
#include "runtime/pointlist_op_registry.h"   // PointListOp / PointListCookCtx / pointListInjectBug
#include "runtime/tixl_point.h"              // SwPoint (64B host point)

#include <Metal/Metal.hpp>                   // MTL::Buffer::contents (bag download, PointsToCPU pattern)

namespace sw {
namespace {

// GetNestedFloat (DataPointImportExport.cs:322-337): record[parent][child] as float, else def.
float nestedFloat(const JsonVal& record, const char* parent, const char* child, float def) {
  const JsonVal* p = jsonFind(record, parent);
  if (!p || p->kind != JsonVal::Obj) return def;
  return jsonToFloat(jsonFind(*p, child), def);
}

// ParseRecord (DataPointImportExport.cs:281-309). ZERO-init then set the four parsed fields (the C#
// struct-initializer semantics — Color / F2 stay 0).
SwPoint parseRecord(const JsonVal& rec) {
  SwPoint p{};  // all-zero (NOT swPointDefault — see header)
  p.Position = {nestedFloat(rec, "Position", "X", 0.0f), nestedFloat(rec, "Position", "Y", 0.0f),
                nestedFloat(rec, "Position", "Z", 0.0f)};
  p.Rotation = {nestedFloat(rec, "Orientation", "X", 0.0f), nestedFloat(rec, "Orientation", "Y", 0.0f),
                nestedFloat(rec, "Orientation", "Z", 0.0f),
                nestedFloat(rec, "Orientation", "W", 1.0f)};  // W default 1 (unit quaternion)
  p.Scale = {nestedFloat(rec, "Scale", "X", 1.0f), nestedFloat(rec, "Scale", "Y", 1.0f),
             nestedFloat(rec, "Scale", "Z", 1.0f)};
  p.FX1 = jsonToFloat(jsonFind(rec, "F1"), 0.0f);
  return p;
}

// LoadFromJson: file → records → points. False when unreadable / malformed / not an array (the .cs
// catch path: the import FAILS and the previous output semantics apply — here, `out` is untouched).
bool loadFromJsonFile(const std::string& path, std::vector<SwPoint>& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f.is_open()) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  JsonVal root;
  if (!jsonParse(ss.str(), root) || root.kind != JsonVal::Arr) return false;
  out.clear();
  out.reserve(root.arr.size());
  for (const JsonVal& rec : root.arr) out.push_back(parseRecord(rec));
  return true;
}

// SaveToJson (DataPointImportExport.cs:258-277): the exact property shape TiXL writes (Orientation
// W-first — cosmetic; JSON keys are unordered for consumers). %.9g = float round-trip (named fork).
bool saveToJsonFile(const std::string& path, const std::vector<SwPoint>& pts) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f.is_open()) return false;
  auto n = [](float v) { char b[32]; std::snprintf(b, sizeof(b), "%.9g", (double)v); return std::string(b); };
  f << "[\n";
  for (size_t i = 0; i < pts.size(); ++i) {
    const SwPoint& p = pts[i];
    f << "  {\n"
      << "    \"Position\": { \"X\": " << n(p.Position.x) << ", \"Y\": " << n(p.Position.y)
      << ", \"Z\": " << n(p.Position.z) << " },\n"
      << "    \"Orientation\": { \"W\": " << n(p.Rotation.w) << ", \"X\": " << n(p.Rotation.x)
      << ", \"Y\": " << n(p.Rotation.y) << ", \"Z\": " << n(p.Rotation.z) << " },\n"
      << "    \"Scale\": { \"X\": " << n(p.Scale.x) << ", \"Y\": " << n(p.Scale.y)
      << ", \"Z\": " << n(p.Scale.z) << " },\n"
      << "    \"F1\": " << n(p.FX1) << "\n"
      << "  }" << (i + 1 < pts.size() ? "," : "") << "\n";
  }
  f << "]\n";
  return true;
}

void cookDataPointImportExport(PointListCookCtx& c) {
  if (!c.output) return;
  c.output->clear();

  // String inputs in spec String-port order: [0]=ImportFilePath, [1]=ExportFilePath.
  std::string importPath, exportPath;
  if (c.inputStrings && c.inputStrings->size() > 0) importPath = (*c.inputStrings)[0];
  if (c.inputStrings && c.inputStrings->size() > 1) exportPath = (*c.inputStrings)[1];

  // ------- IMPORT (level semantics — see fork-datapoint-level-semantics) -------
  bool imported = false;
  if (!importPath.empty()) imported = loadFromJsonFile(importPath, *c.output);

  // ------- bufferIn download (PointsToCPU pattern; whole 64B SwPoints out of the shared bag) -------
  std::vector<SwPoint> bufferIn;
  if (c.inputPointsBag && c.inputPointsCount > 0) {
    const SwPoint* pts = (const SwPoint*)const_cast<MTL::Buffer*>(c.inputPointsBag)->contents();
    bufferIn.assign(pts, pts + c.inputPointsCount);
  }

  // PointBufferOut.Value = _pointBuffer ?? bufferIn (DataPointImportExport.cs:61).
  if (!imported) *c.output = bufferIn;

  // ------- EXPORT (level bool; the INPUT buffer wins when wired — .cs:118) -------
  if (pointListParam(c.params, "Export", 0.0f) >= 0.5f && !exportPath.empty()) {
    const std::vector<SwPoint>& toExport = !bufferIn.empty() ? bufferIn : *c.output;
    if (!toExport.empty()) saveToJsonFile(exportPath, toExport);
  }

  // Test-only: corrupt the REAL output → clear (the registry-wide pointlist tooth). Off in production.
  if (pointListInjectBug()) c.output->clear();
}

}  // namespace

// Self-registration. PortSpec positional: {id, name, dataType, isInput, def, minV, maxV, widget,
// labels, pinless, vecArity, multiInput, strDef}. String defaults faithful to the .cs slot defaults
// ("points.json" both). Import is kept for spec parity (the cook ignores it — level semantics fork).
static const PointListOp _reg_datapointimportexport{
    {"DataPointImportExport", "DataPointImportExport",
     {{"Points", "Points", "PointList", false},
      {"PointBuffer", "PointBuffer", "Points", true},  // TiXL PointBufferIn (forward + export source)
      {"ImportFilePath", "ImportFilePath", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false,
       1, false, "points.json"},
      {"ExportFilePath", "ExportFilePath", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false,
       1, false, "points.json"},
      {"Import", "Import", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, true},
      {"Export", "Export", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, true}},
     /*evaluate=*/nullptr},
    cookDataPointImportExport};

}  // namespace sw
