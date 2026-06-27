// JoinLists pointlist op (pointlist seam leaf — N StructuredList<Point> in -> ONE concatenated out).
// TiXL authority: external/tixl/Operators/Lib/numbers/data/utils/JoinLists.cs (verbatim below).
//
//   JoinLists.cs Update():
//     var connectedLists = Lists.CollectedInputs.Select(c => c.GetValue(context)).Where(c => c != null).ToList();
//     if (connectedLists.Count == 0) { Length.Value = 0; return; }               // nothing wired -> no Result
//     if (connectedLists.Count == 1) { Result.Value = connectedLists[0].TypedClone(); Length.Value = 1; return; }
//     Result.Value = connectedLists[0].Join(connectedLists.GetRange(1, ...).ToArray());  // concat all
//     Length.Value = Result.Value.NumElements;
//
//   Inputs:  Lists = MultiInputSlot<StructuredList>  (N wires; CollectedInputs filters out NULL values).
//   Outputs: Result = Slot<StructuredList>  (the concatenation, in WIRE order).
//            Length = Slot<int>  ★NOT PORTED (see fork below).
//
//   .t3 DEFAULTS (JoinLists.t3): Lists = null (single MultiInput, no defaults to record).
//
//   StructuredList.Join(others) concatenates the element arrays in argument order: list[0] then each of
//   the rest, preserving per-element order within each list. So Result = list0 ++ list1 ++ ... ++ listK,
//   in the order the wires were declared.
//
// ───────────────── NAMED FORK (value-out deferred) ─────────────────
// FORK — joinlists-length-deferred (RESULT-ONLY port): TiXL's 2nd output `Length` (Slot<int>) rides the
//   VALUE rail (a scalar emitted FROM a list's NumElements). The "emit a value FROM a host list" path is a
//   deferred 柏為-domain seam (the list→value crossing is not built on the pointlist rail — pointlist ops
//   only produce PointList outputs, NOT scalar value outputs). We port the RESULT-ONLY op: the
//   concatenated PointList. `Length` is omitted and LOUDLY NAMED here (the established "value-out deferred"
//   fork, same posture as JoinLists' int-output drop). When the list→value emit seam lands, Length attaches
//   as a value output reading Result.size() — out of THIS leaf's scope.
//
// EVAL-SIDE LAYOUT: the cook driver gathers the MultiInput PointList port into inputLists, one entry per
// WIRED source, in WIRE-DECLARATION order (point_graph_hostvalue_cook.cpp:285-291). So inputLists IS the
// CollectedInputs list (an unwired MultiInput contributes zero entries; a wired-but-empty upstream list
// contributes an empty vector — TiXL's CollectedInputs keeps non-null lists, and a cooked empty list is
// non-null). Concatenate every entry's SwPoints in order → Result.
//
// FORK (named):
//   - joinlists-length-deferred: the `Length` int output is dropped (value-out deferred seam), named above.
//   - joinlists-empty-list-kept: TiXL filters NULL lists (CollectedInputs Where c!=null), but a cooked
//     empty list is NON-null (NumElements==0, valid StructuredList) → it is kept and contributes 0 points.
//     The gather already drops UNWIRED ports (no entry), so inputLists == the non-null CollectedInputs set.
#include "runtime/graph.h"                   // NodeSpec, PortSpec, Widget
#include "runtime/pointlist_op_registry.h"   // PointListOp / PointListCookCtx / pointListInjectBug
#include "runtime/tixl_point.h"              // SwPoint full def (the host point currency)

namespace sw {

namespace {

void cookJoinLists(PointListCookCtx& c) {
  if (!c.output) return;
  c.output->clear();

  // inputLists = the gathered MultiInput sources (wire-declaration order) = TiXL's CollectedInputs.
  // count==0 → empty Result (TiXL: connectedLists.Count==0 → return with no Result set; an empty list is
  // the honest "no points produced"). count>=1 → concat list0 ++ list1 ++ ... in wire order.
  if (!c.inputLists) return;
  size_t total = 0;
  for (const std::vector<SwPoint>& l : *c.inputLists) total += l.size();
  c.output->reserve(total);
  for (const std::vector<SwPoint>& l : *c.inputLists)
    c.output->insert(c.output->end(), l.begin(), l.end());

  // Test-only: corrupt the REAL output (drop the last point) so the golden's RED case bites on the cook.
  if (pointListInjectBug() && !c.output->empty()) c.output->pop_back();
}

}  // namespace

// Self-registration. ONE MultiInput PointList input ("Lists", multiInput=true per TiXL MultiInputSlot).
// Output "Result" is a PointList (the concatenation). The `Length` int output is NOT a port here
// (joinlists-length-deferred — value-out deferred seam). The cook driver gathers Lists into inputLists in
// wire-declaration order, so Result = inputLists[0] ++ inputLists[1] ++ ... ++ inputLists[N-1].
// PortSpec field order: id,name,dataType,isInput,def,minV,maxV,widget,labels,pinless,vecArity,multiInput.
static const PointListOp _reg_joinlists{
    {"JoinLists", "JoinLists",
     {{"Result", "Result", "PointList", false},
      {"Lists", "Lists", "PointList", true, 0.0f, 0.0f, 0.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/true}},
     /*evaluate=*/nullptr},
    cookJoinLists};

}  // namespace sw
