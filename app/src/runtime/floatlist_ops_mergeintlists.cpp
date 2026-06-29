// MergeIntLists floatlist op (floatlist self-registration seam leaf — the INT twin of MergeFloatLists,
// riding the SAME FloatList currency because sw has no Int rail). TiXL authority:
// external/tixl/Operators/Lib/numbers/ints/MergeIntLists.cs (structurally identical to MergeFloatLists.cs;
// see floatlist_ops_mergefloatlists.cpp for the full .cs trace + scope/defer notes).
//
// The ONLY behavioural difference from MergeFloatLists is the Average mode: MergeIntLists.cs:298 does
// INTEGER division `(int)(sum / count)` (truncate toward zero), where the float twin does float division.
// Everything else (Append/Htp gather, Enabled gate, MaxSize, the StartIndices + Ltp/FailOver DEFERRALS)
// is shared verbatim via cookMergeListsShared(c, integerAverage=true).
//
// FORKS (named):
//   - fork-mergeintlists-dissolves-to-floatlist: TiXL's lists are List<int>; on sw they ride the FloatList
//     currency (the same dissolve as IntListToFloatList / MergeIntLists-StartIndices). The integral values
//     are carried exactly as floats (e.g. 3.0); only the Average truncation reintroduces integer semantics.
//   - sub-seam-merge-startindices / sub-seam-merge-stateful-modes: SAME deferrals as MergeFloatLists —
//     StartIndices (a second List<int> input) is OMITTED, Ltp/FailOver fall through to Append. See the
//     float leaf's header for the full naming.
#include "runtime/floatlist_op_registry.h"  // FloatListOp / FloatListCookCtx
#include "runtime/graph.h"                   // NodeSpec, PortSpec, Widget

namespace sw {

// Defined in floatlist_ops_mergefloatlists.cpp (the shared Append/Htp/Average/gather body).
void cookMergeListsShared(FloatListCookCtx& c, bool integerAverage);

namespace {

void cookMergeIntLists(FloatListCookCtx& c) { cookMergeListsShared(c, /*integerAverage=*/true); }

}  // namespace

// Self-registration. Ports identical to MergeFloatLists (the int values ride the FloatList currency).
// StartIndices OMITTED (deferred); MergeMode enum {Append,Htp,Ltp,FailOver,Average} — Ltp/FailOver
// stateful, currently Append-fallthrough.
static const FloatListOp _reg_mergeintlists{
    {"MergeIntLists", "MergeIntLists",
     {{"InputLists", "InputLists", "FloatList", true, 0.0f, 0.0f, 0.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/true},
      {"Enabled", "Enabled", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
      {"MaxSize", "MaxSize", "Float", true, -1.0f, -1.0f, 1048576.0f, Widget::Slider, {}, true},
      {"MergeMode", "MergeMode", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"Append", "Htp", "Ltp", "FailOver", "Average"}, true},
      {"out", "out", "FloatList", false}},
     /*evaluate=*/nullptr},
    cookMergeIntLists};

}  // namespace sw
