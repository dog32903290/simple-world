// MergeFloatLists floatlist op (floatlist self-registration seam leaf — MultiInput<List<float>> ->
// List<float>, the multi-mode merger). TiXL authority:
// external/tixl/Operators/Lib/numbers/floats/process/MergeFloatLists.cs (and its int twin MergeIntLists.cs).
//
//   MergeFloatLists.cs Update():
//     inputListSlots = InputLists.GetCollectedTypedInputs();
//     if (count==0) { clear; reset state; return; }
//     resultList.Clear();
//     if (!Enabled) { UpdateAppend(...); return; }          // ← DEFAULT (Enabled defaults false)
//     mode = (MergeModes)MergeMode;                          // Append(0)/Htp(1)/Ltp(2)/FailOver(3)/Average(4)
//     switch (mode) { Htp→UpdateHtp; Ltp→UpdateLtp; FailOver→UpdateFailOver; Average→UpdateAverage;
//                     Append/default→UpdateAppend }
//
//   UpdateHtp:     per index i over max length → MAX of sourceLists[k][i] (missing→skip), else 0.
//   UpdateAverage: per index i over max length → mean of present sourceLists[k][i] (int twin: integer
//                  division (int)(sum/count)); empty → skip; no present → 0.
//   UpdateAppend (StartIndices EMPTY — the default + the only path the FloatList ctx can see):
//     maxSize = MaxSize; useMaxSize = maxSize >= 0;
//     useMaxSize  → list = maxSize zeros; writeIndex=0; foreach source: write contiguously, capped at maxSize.
//     !useMaxSize → writeIndex=0; foreach NON-EMPTY source: append contiguously (writeIndex persists
//                   across lists) → CONCATENATION in wire order (identical to CombineFloatLists).
//
//   Ports: Enabled=bool(false); InputLists=MultiInputSlot<List<float>>; MaxSize=int(-1);
//          MergeMode=int enum(0=Append); StartIndices=InputSlot<List<int>>([]).
//   Output: Result = Slot<List<float>>.
//
// SCOPE — what this leaf implements vs DEFERS (named, honest — NOT 硬湊):
//   IMPLEMENTED (stateless, ctx-visible): Enabled gate, MergeMode {Append, Htp, Average}, MaxSize.
//   DEFERRED:
//     • sub-seam-merge-startindices: StartIndices is a SECOND List<int> input (InputSlot<List<int>>),
//       distinct from the InputLists MultiInput. FloatListCookCtx carries only the homogeneous InputLists
//       gather — a second per-list IntList input port would need a dedicated driver gather (the IntList-
//       as-FloatList second-channel seam). With StartIndices EMPTY (the .t3 default) it never affects
//       UpdateAppend, so the DEFAULT cook is byte-faithful; only an explicitly-wired StartIndices
//       (per-list write offsets) is out of reach → the StartIndices port is OMITTED here, deferred.
//     • sub-seam-merge-stateful-modes: Ltp (last-takes-precedence, _ltpCombinedList accumulator) and
//       FailOver (change-detection, _previousSourceLists + _activeFailoverIndex) carry CROSS-FRAME state.
//       The FloatList rail HAS a state slot (FloatListState) but these two modes' state shapes (a combined
//       list + a list-of-previous-lists + an active index) are not modelled here — deferred to a stateful
//       MergeLists cut ([[sw-stateful-node-parity-gap]] discipline: dedicated state golden first). MergeMode
//       2/3 fall through to UpdateAppend here (a named divergence — Ltp/FailOver are NOT yet faithful).
//
// FORKS (named):
//   - fork-mergefloatlists-enum-dissolve: MergeMode {0..4} dissolves to a Float param (std::lround).
//   - fork-mergefloatlists-int-dissolve: MergeIntLists (List<int>) rides this SAME FloatList currency
//     (sw has no Int rail). The int twin's UpdateAverage uses INTEGER division ((int)(sum/count)); this
//     float leaf does float division. MergeIntLists is a SEPARATE leaf (mergeintlists.cpp) carrying the
//     integer-Average fork; this float leaf is the float-division version.
//   - fork-merge-append-default-is-concat: with default params (Enabled=false, MaxSize=-1, StartIndices
//     empty) UpdateAppend = pure wire-order concatenation (skip empties) — identical to CombineFloatLists.
#include <algorithm>  // std::max
#include <cmath>      // std::lround
#include <limits>     // std::numeric_limits

#include "runtime/floatlist_op_registry.h"  // FloatListOp / FloatListCookCtx / floatListInjectBug / floatListParam
#include "runtime/graph.h"                   // NodeSpec, PortSpec, Widget

namespace sw {

// Shared merge implementation for the float + int twins. `integerAverage` selects MergeIntLists's
// (int)(sum/count) truncation (Average mode) vs MergeFloatLists's float mean. Both share the identical
// Append/Htp/gather logic; only Average's division differs.
void cookMergeListsShared(FloatListCookCtx& c, bool integerAverage) {
  if (!c.output) return;
  c.output->clear();
  if (!c.inputLists || c.inputLists->empty()) return;  // 0 connections → empty (MergeFloatLists.cs:58-65)

  const std::vector<std::vector<float>>& sources = *c.inputLists;
  const bool enabled = floatListParam(c.params, "Enabled", 0.0f) > 0.5f;  // bool dissolved to Float
  const int mode = enabled ? (int)std::lround(floatListParam(c.params, "MergeMode", 0.0f)) : 0;  // !Enabled→Append

  // --- Htp (1): per-index MAX over present sources ---
  if (mode == 1) {
    size_t maxLen = 0;
    for (const auto& s : sources) maxLen = std::max(maxLen, s.size());
    c.output->reserve(maxLen);
    for (size_t i = 0; i < maxLen; ++i) {
      float maxV = std::numeric_limits<float>::lowest();
      bool found = false;
      for (const auto& s : sources)
        if (i < s.size()) { if (!found || s[i] > maxV) maxV = s[i]; found = true; }
      c.output->push_back(found ? maxV : 0.0f);
    }
  }
  // --- Average (4): per-index mean over present sources (int twin: integer division) ---
  else if (mode == 4) {
    size_t maxLen = 0;
    for (const auto& s : sources) maxLen = std::max(maxLen, s.size());
    c.output->reserve(maxLen);
    for (size_t i = 0; i < maxLen; ++i) {
      double sum = 0.0;
      int cnt = 0;
      for (const auto& s : sources)
        if (i < s.size()) { sum += s[i]; ++cnt; }
      if (cnt == 0) { c.output->push_back(0.0f); continue; }
      if (integerAverage) {
        // MergeIntLists.cs:298 — (int)(sum / count): integer division (truncate toward zero).
        long long isum = 0;
        for (const auto& s : sources) if (i < s.size()) isum += (long long)s[i];
        c.output->push_back((float)(int)(isum / cnt));
      } else {
        c.output->push_back((float)(sum / cnt));  // MergeFloatLists.cs:298 — (float)(sum/count)
      }
    }
  }
  // --- Append (0) + the deferred Ltp(2)/FailOver(3) fallthrough: StartIndices-EMPTY UpdateAppend ---
  else {
    const int maxSize = (int)std::lround(floatListParam(c.params, "MaxSize", -1.0f));
    const bool useMaxSize = maxSize >= 0;
    if (useMaxSize) {
      c.output->assign((size_t)maxSize, 0.0f);  // list = maxSize zeros (MergeFloatLists.cs:308-316)
      int writeIndex = 0;
      for (const auto& src : sources) {
        if (src.empty()) continue;  // skip null/empty (cs:324)
        for (size_t k = 0; k < src.size() && writeIndex < maxSize; ++k)
          (*c.output)[(size_t)writeIndex++] = src[k];
      }
    } else {
      // !useMaxSize → contiguous append, writeIndex PERSISTS across lists = wire-order concatenation.
      for (const auto& src : sources) {
        if (src.empty()) continue;  // cs:324 source==null||Count==0 → continue
        c.output->insert(c.output->end(), src.begin(), src.end());
      }
    }
  }

  // Test-only: corrupt the REAL output (drop last) so the golden's RED bites on the actual cook path.
  if (floatListInjectBug() && !c.output->empty())
    c.output->pop_back();
}

namespace {

void cookMergeFloatLists(FloatListCookCtx& c) { cookMergeListsShared(c, /*integerAverage=*/false); }

}  // namespace

// Self-registration. Ports: InputLists (FloatList MultiInput) + Enabled/MaxSize/MergeMode scalar params.
// StartIndices OMITTED (sub-seam-merge-startindices, deferred). MergeMode enum {Append,Htp,Ltp,FailOver,
// Average}; Ltp/FailOver are STATEFUL and currently fall through to Append (sub-seam-merge-stateful-modes).
static const FloatListOp _reg_mergefloatlists{
    {"MergeFloatLists", "MergeFloatLists",
     {{"InputLists", "InputLists", "FloatList", true, 0.0f, 0.0f, 0.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/true},
      {"Enabled", "Enabled", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
      {"MaxSize", "MaxSize", "Float", true, -1.0f, -1.0f, 1048576.0f, Widget::Slider, {}, true},
      {"MergeMode", "MergeMode", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"Append", "Htp", "Ltp", "FailOver", "Average"}, true},
      {"out", "out", "FloatList", false}},
     /*evaluate=*/nullptr},
    cookMergeFloatLists};

}  // namespace sw
