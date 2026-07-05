// runtime/node_registry_math_pointdata — self-registering MATH NodeSpec leaf for the numbers.data.utils
// StructuredList<Point> READERS. Sibling of node_registry_math_anim.cpp (which was at its line-count cap,
// ARCHITECTURE rule 4); picked up by the SAME node_registry_math_*.cpp glob → no CMake edit. Its sibling
// GetPointDataFromList stays in node_registry_math_anim.cpp (grandfathered there); this leaf carries the
// field-attribute reader added alongside it.
//
// These nodes have NO pure evaluate() (they read a cooked point buffer the value pull cannot reach) —
// evaluate==nullptr; cookPointValueOutputNodes (resident_point_value_output_cook.cpp — the point-into-frame
// pass run AFTER pg.cookResident) indexes the cooked Shared buffer host-side and writes the value onto
// extOut. Same rail as GetPointDataFromList / PointToMatrix.
#include "runtime/graph.h"             // NodeSpec, PortSpec, Widget
#include "runtime/math_op_registry.h"  // MathOp / mathSpecSink

namespace sw {
namespace {

// TiXL GetListItemAttribute (Lib/numbers/data/utils/GetListItemAttribute.cs) — read ONE float FIELD of ONE
// point in a StructuredList<Point>. The op calls StructuredListUtils.GetValueOfFieldWithType<float>
// (StructuredListUtils.cs:12-54): item = list[ItemIndex % NumElements] (cs:19), then reflect over
// list.Type.GetFields() — return the field whose reflection INDEX == FieldIndex OR whose NAME ==
// OrFieldName (index checked first, cs:32-44), but ONLY if that field is a float (`is float`, cs:36 —
// non-float fields are skipped). Result = that float, else default 0 (cs:16-17/45). The cook lives in
// cookPointValueOutputNodes' GetListItemAttribute branch.
//   FORK fork-getlistitemattr-point-float-fields: TiXL reflects over ANY StructuredList type via C#
// reflection. sw has no generic reflective StructuredList currency — its StructuredList currency IS the
// Point list (SwPoint, = TiXL Point). So this reads the Point's TWO float-typed reflected fields:
// GetFields() index 1 = F1 (SwPoint.FX1 @12) and index 5 = F2 (SwPoint.FX2 @60) — the SAME fields TiXL's
// `is float` test admits (Position/Orientation/Color/Scale are non-float → skipped, return 0). OrFieldName
// "F1"/"F2" map to the SAME two. A FieldIndex/name resolving to a non-float field (or an unknown one)
// yields 0, faithful to the miss/`is float`-fail default. Named: a window over the Point layout's float
// fields, not generic reflection (a generic reflective StructuredList currency is a later seam).
// .t3 defaults (GetListItemAttribute.t3): ItemIndex=0, FieldIndex=0, OrFieldName="". index.Mod = C# `%`.
static const MathOp _reg_GetListItemAttribute{
    {"GetListItemAttribute", "GetListItemAttribute",
     {{"Result", "Result", "Float", false},                      // extOut[0] — the selected float field
      {"DataList", "DataList", "Points", true},                  // cs:9CF1AE77 (StructuredList = Point list)
      {"ItemIndex", "ItemIndex", "Float", true, 0.0f, 0.0f, 1000.0f, Widget::Slider},  // cs:09F5501D
      {"FieldIndex", "FieldIndex", "Float", true, 0.0f, 0.0f, 16.0f, Widget::Slider},  // cs:6A988C18
      {"OrFieldName", "OrFieldName", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false,
       ""}},  // cs:C6B58AB1 — index-OR-name; index wins first (cs:32-44)
     nullptr,
     "numbers.data.utils"}
};

}  // namespace
}  // namespace sw
