// KeepColors colorlist op (colorlist self-registration seam leaf — the FIRST cross-frame state consumer
// on the COLORLIST rail). TiXL authority: external/tixl/Operators/Lib/numbers/color/KeepColors.cs
// (verbatim Update() below; cited inline).
//
//   KeepColors.cs Update() (cs:16-44):
//     var addColor = AddColorToList.GetValue(context);                       // cs:18
//     var length   = MaxLength.GetValue(context).Clamp(1, 100000);           // cs:19
//     var newColor = Color.GetValue(context);                               // cs:20
//     var reset    = Reset.GetValue(context);                               // cs:22
//     if (reset) _list.Clear();                                            // cs:24-25 (BEFORE insert)
//     if (addColor) _list.Insert(0, newColor);                            // cs:29-30 (newest at FRONT)
//     if (_list.Count > length)                                           // cs:32
//         _list.RemoveRange(length, _list.Count - length);               // cs:34 (drop the OLDEST tail)
//     Result.Value = _list;                                              // cs:37
//   Field: private readonly List<Vector4> _list = [];                    // cs:46 (PERSISTENT per node)
//
// THE SEAM THIS LEAF PROVES: _list is a PER-NODE field that SURVIVES across frames — Insert(0,…) prepends
// each frame, RemoveRange caps to MaxLength, Reset clears. Every other colorlist op is single-frame (its
// host buffer is scratch, cleared each cook). So KeepColors needed a new cross-frame STATE slot on the
// cook ctx: ColorListCookCtx::state (the colorlist analog of the FeedbackPair/feedbackToggle seam). The
// driver owns + threads it — flat: Impl::colorListState[flatKey(id)] (point_graph.cpp); resident
// (production): s_colorListState[path] (frame_cook.cpp, mirror of cookStatefulValueNodes's s_svState).
// This leaf reads + mutates *state, then COPIES it to *output (output = the readback channel each frame).
//
// FORK (named, load-bearing): fork-keepcolors-vec4-as-4-floats-single-input.
//   TiXL's `Color` is a single InputSlot<Vector4>. This codebase has NO node-to-node Vec4 currency: a
//   Vector4 is the ESTABLISHED vecN-as-N-floats fork (4 Float ports per color, head Widget::Vec
//   vecArity=4, the DefineGradient Color1 precedent — gradient_ops_definegradient.cpp:103). So `Color`
//   becomes 4 Float ports Color.x/.y/.z/.w resolved into the params map; the leaf reads them via
//   colorListParam and rebuilds the float4. (NOT the 4-PARALLEL-MultiInput shape of ColorsToList — that
//   fork is for a MultiInput<Vector4>; KeepColors's Color is a SINGLE Vector4.)
//
// .cs DEFAULTS (the InputSlots have no explicit default → C# field default of the value type):
//   Color = (0,0,0,0); AddColorToList = false; MaxLength = 0 (→ Clamp(1,100000) ⇒ 1); Reset = false.
//   NOTE: AddColorToList default false ⇒ a freshly-placed node accumulates NOTHING until the user wires/
//   sets it true — faithful to the .cs (the gate is the whole point; the golden drives it true).
#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include <simd/simd.h>

#include "runtime/colorlist_op_registry.h"  // ColorListOp / ColorListCookCtx / colorListParam / injectBug
#include "runtime/graph.h"                   // NodeSpec, PortSpec, Widget

namespace sw {

int runKeepColorsSelfTest(bool injectBug);

namespace {

// KeepColors: maintain the PERSISTENT per-node accumulator (*c.state) across frames, then publish it to
// *c.output. Body is literally KeepColors.cs:18-37.
void cookKeepColors(ColorListCookCtx& c) {
  if (!c.output) return;
  std::vector<simd::float4>* st = c.state;  // the cross-frame accumulator (KeepColors._list, cs:46)

  // cs:18-22 — read the four scalar inputs (bool/int ride the Float value rail: bool via >0.5, int via cast).
  const bool addColor = colorListParam(c.params, "AddColorToList", 0.0f) > 0.5f;   // cs:18 (default false)
  // cs:19 — MaxLength.Clamp(1, 100000). Default 0 → clamps up to 1 (TiXL .Clamp(1,100000) on the int).
  int length = (int)colorListParam(c.params, "MaxLength", 0.0f);
  length = std::max(1, std::min(length, 100000));
  const simd::float4 newColor = simd::make_float4(             // cs:20 — Color (vec4-as-4-floats fork)
      colorListParam(c.params, "Color.x", 0.0f), colorListParam(c.params, "Color.y", 0.0f),
      colorListParam(c.params, "Color.z", 0.0f), colorListParam(c.params, "Color.w", 0.0f));
  const bool reset = colorListParam(c.params, "Reset", 0.0f) > 0.5f;               // cs:22 (default false)

  if (st) {
    if (reset) st->clear();                          // cs:24-25 — clear BEFORE insert (reset+add ⇒ [new])
    // injectBug (golden teeth): break the insert/cap on the REAL accumulator (skip the Insert) so the
    // accumulated list comes out wrong → the golden's RED frame bites here, NOT by flipping expected.
    if (addColor && !colorListInjectBug())
      st->insert(st->begin(), newColor);             // cs:29-30 — Insert(0, newColor): newest at FRONT
    if ((int)st->size() > length)                    // cs:32
      st->erase(st->begin() + length, st->end());    // cs:34 — RemoveRange(length, Count-length): drop tail
    *c.output = *st;                                 // cs:37 — Result.Value = _list (publish the accumulator)
  } else {
    // No state slot supplied (a hand-built ctx with no driver-owned state) → behave as a single empty
    // frame: an addColor with nowhere to persist still publishes a one-shot list (faithful to frame 0).
    c.output->clear();
    if (addColor && !colorListInjectBug() && length >= 1) c.output->push_back(newColor);
  }
}

}  // namespace

// Self-registration. File-scope static ColorListOp — independent leaf .cpp (no shared edit point).
//   Ports: "Color.x/.y/.z/.w" = the single Vector4 input (vec4-as-4-floats fork; head Widget::Vec
//          vecArity=4, DefineGradient Color1 precedent — sub-ports plain Float/Slider);
//          "AddColorToList" = bool gate (Widget::Bool, default 0/false);
//          "MaxLength"      = int cap (Widget::Slider, default 0 → clamps to 1; pinless param-only);
//          "Reset"          = bool clear (Widget::Bool, default 0/false);
//          "out"            = the ColorList output (the accumulated host color list).
// PortSpec field order: id,name,dataType,isInput,def,minV,maxV,widget,labels,pinless,vecArity,multiInput.
static const ColorListOp _reg_keepcolors{
    {"KeepColors", "KeepColors",
     {{"Color.x", "Color", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 4},
      {"Color.y", "Color.y", "Float", true, 0.0f, -100.0f, 100.0f},
      {"Color.z", "Color.z", "Float", true, 0.0f, -100.0f, 100.0f},
      {"Color.w", "Color.w", "Float", true, 0.0f, -100.0f, 100.0f},
      {"AddColorToList", "AddColorToList", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      {"MaxLength", "MaxLength", "Float", true, 0.0f, 0.0f, 100000.0f, Widget::Slider, {},
       /*pinless=*/true},
      {"Reset", "Reset", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      {"out", "out", "ColorList", false}},
     /*evaluate=*/nullptr},  // ColorList output cannot ride NodeSpec::evaluate (returns ONE float)
    cookKeepColors};

}  // namespace sw
