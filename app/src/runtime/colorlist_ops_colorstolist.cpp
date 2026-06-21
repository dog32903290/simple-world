// ColorsToList colorlist op (colorlist self-registration seam leaf — MultiInput<Vector4> -> List<Vector4>).
// TiXL authority: external/tixl/Operators/Lib/numbers/floats/basic/ColorsToList.cs (verbatim below).
//
//   ColorsToList.cs Update():
//     Result.Value.Clear();                                          // cs:16
//     foreach (var input in Colors.GetCollectedTypedInputs())        // cs:17
//         Result.Value.Add(input.GetValue(context));                 // cs:19
//     Colors.DirtyFlag.Clear();                                      // cs:22 (dirty bookkeeping)
//
//   Ports: Colors = MultiInputSlot<Vector4> (the ONE variable-length list of colors).
//   Output: Result = Slot<List<Vector4>> (the host color list — the ColorList channel's first producer).
//
// This is the EXACT vec4 analog of FloatsToList (floatlist_ops_floatstolist.cpp): the cleanest, simplest
// faithful prover for the ColorList currency — a pure HOST op, no GPU readback. Its body is literally
// the .cs: clear the output list, then append each gathered color in wire-declaration order.
//
// FORK (named, load-bearing): fork-colorstolist-vec4-as-4-parallel-multiinputs.
//   TiXL's `Colors` is a MultiInputSlot<Vector4> — N Vector4 wires, each one collected color. This
//   codebase has NO node-to-node Vec4 currency: a Vector4 is the ESTABLISHED vecN-as-N-floats fork (4
//   Float ports per color, head Widget::Vec + vecArity, read via cookVecN). Applying that fork to a
//   MULTI-input: `Colors` becomes 4 PARALLEL Float MultiInput component ports (Colors.x/.y/.z/.w). The
//   i-th collected color = (x[i], y[i], z[i], w[i]) — the driver gathers each component port's wires in
//   wire-declaration order (the SAME gather discipline as FloatsToList's scalar MultiInput) and the leaf
//   ZIPS them per index. Faithful to GetCollectedTypedInputs (CONNECTED inputs only → unwired component
//   contributes nothing → that color slot is 0 for that channel). The element COUNT = the max length
//   across the 4 component channels (a color with only x wired still produces a color; the unwired
//   channels read 0 for that index — mirrors a Vector4 with only its X driven, the rest at default 0).
//
// The output rides the new "ColorList" dataType port back to a downstream ColorList consumer (the
// CombineColorLists / KeepColors / ReadPointColors family, OUT of this seam's scope). Pure HOST data.
#include <array>

#include <simd/simd.h>

#include "runtime/colorlist_op_registry.h"  // ColorListOp / ColorListCookCtx / colorListInjectBug
#include "runtime/graph.h"                   // NodeSpec, PortSpec, Widget

namespace sw {

int runColorsToListSelfTest(bool injectBug);

namespace {

// ColorsToList: Result.Value.Clear(); foreach gathered color -> Result.Value.Add(color).
// The driver hands the 4 parallel scalar component channels (x,y,z,w) as inputColorScalars; zip index i
// across the 4 into one float4, in wire-declaration order, count = max channel length.
void cookColorsToList(ColorListCookCtx& c) {
  if (!c.output) return;
  c.output->clear();  // ColorsToList.cs:16 — Result.Value.Clear()
  if (c.inputColorScalars) {
    const std::array<std::vector<float>, 4>& ch = *c.inputColorScalars;
    size_t n = 0;
    for (int k = 0; k < 4; ++k) n = std::max(n, ch[k].size());
    for (size_t i = 0; i < n; ++i) {
      // Per-color zip across the 4 component channels. A channel shorter than i (an unwired or
      // shorter component) reads 0 for that slot — faithful to a Vector4 whose component is at default.
      float x = i < ch[0].size() ? ch[0][i] : 0.0f;
      float y = i < ch[1].size() ? ch[1][i] : 0.0f;
      float z = i < ch[2].size() ? ch[2][i] : 0.0f;
      float w = i < ch[3].size() ? ch[3][i] : 0.0f;
      c.output->push_back(simd::make_float4(x, y, z, w));  // cs:19 — Add each collected color, in order
    }
  }
  // Test-only: corrupt the REAL output on the actual cook path (drop the last color) so the golden's RED
  // case bites here, NOT by flipping the expected value. Off in production.
  if (colorListInjectBug() && !c.output->empty())
    c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static ColorListOp — independent leaf .cpp (no shared edit point).
// Feeds colorListSpecSink() + colorListCookFns() during pre-main dynamic init.
//   Ports: "Colors.x"/".y"/".z"/".w" = the 4 parallel Float MultiInput component channels (the vec4-as-
//          4-floats MultiInput fork; head Widget::Vec, vecArity counts down 4..1 like every Vec group);
//          "out" = the ColorList output (the new host color-list channel's currency).
// PortSpec field order: id,name,dataType,isInput,def,minV,maxV,widget,labels,pinless,vecArity,multiInput.
static const ColorListOp _reg_colorstolist{
    {"ColorsToList", "ColorsToList",
     {{"Colors.x", "Colors", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, false, 4,
       /*multiInput=*/true},
      {"Colors.y", "Colors.y", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, false, 3,
       /*multiInput=*/true},
      {"Colors.z", "Colors.z", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, false, 2,
       /*multiInput=*/true},
      {"Colors.w", "Colors.w", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, false, 1,
       /*multiInput=*/true},
      {"out", "out", "ColorList", false}},
     /*evaluate=*/nullptr},  // ColorList output cannot ride NodeSpec::evaluate (returns ONE float)
    cookColorsToList};

}  // namespace sw
