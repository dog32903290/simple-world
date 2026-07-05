// AnalyzeFloatList host-scalar op (the FIRST MULTI-OUTPUT host-scalar op — FloatList input → FOUR host
// Float outputs). It is the op that FORCED the outCache/extOut widen 3→8: its 4 outputs (Min/Max/
// AverageMean/AllValid) don't fit the old float[3] bridge. TiXL authority:
// external/tixl/Operators/Lib/numbers/floats/process/AnalyzeFloatList.cs (ported verbatim):
//
//   Update() (cs:28-67):
//     list = Input.GetValue(context);
//     if (list == null || list.Count == 0) {                       // cs:36
//         Min = Max = AverageMean = NaN; AllValid = false; return; // cs:38-41
//     }
//     sum=0; min=+Inf; max=-Inf; allValid=true;                    // cs:45-49
//     foreach v in list:                                           // cs:51
//         if (!float.IsFinite(v)) { allValid = false; continue; }  // cs:53-57
//         min = MathF.Min(min, v); max = MathF.Max(max, v); sum += v;  // cs:59-61
//     Min = min; Max = max; AverageMean = sum / list.Count; AllValid = allValid;  // cs:64-67
//
//   Outputs (declaration order = the output-port index the driver keys scalarOutputs / extOut / outCache):
//     [0] Min (Slot<float>) ; [1] Max (Slot<float>) ; [2] AverageMean (Slot<float>) ; [3] AllValid (Slot<bool>).
//   Input: [4] Input = InputSlot<List<float>>.
//
// EVAL-SIDE LAYOUT: a host-scalar CONSUMER (FloatList → host Float), like FloatListLength — but with FOUR
// Float outputs instead of one. The driver gathers its ONE FloatList input via cookFloatListNode →
// inputLists[0], runs this leaf, then distributes: *output (port 0 = Min) + scalarOutputs[1..3]
// (Max/AverageMean/AllValid) → Node::outCache[0..3] (flat) / ResidentNode::extOut[0..3] (resident). A
// downstream Float INPUT wired to AnalyzeFloatList.Max reads outCache[1] via evalFloat's !evaluate hatch.
//
// FORKS (named):
//   - fork-analyzefloatlist-multi-output: the SECOND rail (after PickStringPart on the STRING rail) to use
//     the multi-output sink (HostScalarCookCtx::scalarOutputs) — the outCache widen 3→8 exists for THIS op.
//   - fork-bool-dissolve-to-float: AllValid is Slot<bool> in TiXL; sw dissolves bool→Float (1.0 true /
//     0.0 false), Cut32 convention (same as every Int/bool-returning op).
//   - fork-average-divides-by-full-count: AverageMean = sum / list.Count where sum EXCLUDES non-finite
//     values but Count includes them (cs:61,66) — ported EXACTLY (a list with a NaN averages sum-of-
//     finite / total-count, NOT / finite-count). Load-bearing in the golden's non-finite leg.
//   - fork-skip-dirtyflag-earlyout: cs:30-33 early-returns when Input is not dirty (a TiXL caching opt).
//     sw re-cooks every frame (no per-op dirty flag on the host-scalar rail), so this leaf recomputes
//     each cook — same RESULT (the cached value would equal the recompute), just no cache. Named, benign.
//   - fork-empty-is-nan: an UNWIRED FloatList input yields no gathered list → inputLists empty → the
//     empty/null branch (all NaN + AllValid=0), faithful to cs:36-41 (null → NaN outputs).
#include <cmath>  // std::isfinite, std::fmin, std::fmax, NAN

#include "runtime/graph.h"                    // NodeSpec, PortSpec, Widget
#include "runtime/host_scalar_op_registry.h"  // HostScalarOp / HostScalarCookCtx / hostScalarInjectBug

namespace sw {

namespace {

// AnalyzeFloatList: compute Min/Max/AverageMean/AllValid over the ONE wired FloatList input.
void cookAnalyzeFloatList(HostScalarCookCtx& c) {
  if (!c.output) return;

  // The ONE FloatList input → inputLists[0] (empty if unwired). null/empty → all NaN + AllValid=false.
  const bool haveList = c.inputLists && !c.inputLists->empty() && !(*c.inputLists)[0].empty();

  float minV, maxV, meanV, allValid;
  if (!haveList) {
    minV = maxV = meanV = NAN;  // cs:38-40
    allValid = 0.0f;            // cs:41 AllValid = false → 0.0 (bool→Float)
  } else {
    const std::vector<float>& list = (*c.inputLists)[0];
    float sum = 0.0f;
    float mn = INFINITY;   // cs:46 min = +Inf
    float mx = -INFINITY;  // cs:47 max = -Inf
    bool valid = true;     // cs:49
    for (float v : list) {
      if (!std::isfinite(v)) { valid = false; continue; }  // cs:53-57 non-finite → not-all-valid, skipped
      mn = std::fmin(mn, v);  // cs:59
      mx = std::fmax(mx, v);  // cs:60
      sum += v;               // cs:61
    }
    minV = mn;
    maxV = mx;
    meanV = sum / (float)list.size();  // cs:66 — sum(finite) / FULL Count (fork-average-divides-by-full-count)
    allValid = valid ? 1.0f : 0.0f;    // cs:67 bool→Float
  }

  // Distribute: port 0 (Min) → *output; ports 1-3 → scalarOutputs (multi-output sink → outCache/extOut).
  *c.output = minV;  // Min (port 0)
  if (c.scalarOutputs) {
    (*c.scalarOutputs)[1] = maxV;      // Max (port 1)
    (*c.scalarOutputs)[2] = meanV;     // AverageMean (port 2)
    (*c.scalarOutputs)[3] = allValid;  // AllValid (port 3, bool→Float)
  }

  // Test-only: corrupt the REAL outputs on the actual cook path (knock every channel off its true value
  // with a sentinel) so the golden's RED bites downstream via evalFloat, NOT by flipping expected values.
  if (hostScalarInjectBug()) {
    *c.output = -999.0f;
    if (c.scalarOutputs) {
      (*c.scalarOutputs)[1] = -999.0f;
      (*c.scalarOutputs)[2] = -999.0f;
      (*c.scalarOutputs)[3] = -999.0f;
    }
  }
}

}  // namespace

// Self-registration. File-scope static HostScalarOp — independent leaf .cpp (no shared edit point).
//   Output ports FIRST (the port index = the scalarOutputs / outCache / extOut key evalFloat reads):
//     [0] "Min"         = Float output (port 0 → *output → outCache[0])
//     [1] "Max"         = Float output (port 1 → scalarOutputs[1] → outCache[1])
//     [2] "AverageMean" = Float output (port 2 → scalarOutputs[2] → outCache[2])
//     [3] "AllValid"    = Float output (port 3, bool dissolved → scalarOutputs[3] → outCache[3])
//   Input:
//     [4] "Input"       = FloatList input (the list to analyze).
static const HostScalarOp _reg_analyzefloatlist{
    {"AnalyzeFloatList", "AnalyzeFloatList",
     {{"Min", "Min", "Float", false},
      {"Max", "Max", "Float", false},
      {"AverageMean", "AverageMean", "Float", false},
      {"AllValid", "AllValid", "Float", false},
      {"Input", "Input", "FloatList", true}},
     /*evaluate=*/nullptr},  // host scalar comes from the cook driver, not the value-eval evaluate fn
    cookAnalyzeFloatList};

}  // namespace sw
