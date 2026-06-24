// CompareFloatLists host-scalar op (host-scalar self-registration seam leaf — TWO FloatList inputs +
// Threshold → host Float output, the per-element-difference-ratio consumer). TiXL authority:
// external/tixl/Operators/Lib/numbers/floats/process/CompareFloatLists.cs:14-49 (verbatim):
//
//   CompareFloatLists.cs Update():
//     var listA = ListA.GetValue(context);
//     var listB = ListB.GetValue(context);
//     if (listA == null || listA.Count == 0 || listB == null || listB.Count == 0) { Difference.Value = 1; return; }
//     var threshold = Threshold.GetValue(context);
//     var differentElementCount = 0;
//     var maxCount = listA.Count > listB.Count ? listA.Count : listB.Count;
//     for (int index = 0; index < maxCount; index++) {
//       if (listA.Count < index || listB.Count < index) { differentElementCount++; continue; }
//       if (MathF.Abs(listA[index] - listB[index]) > threshold) differentElementCount++;
//     }
//     Difference.Value = (float)differentElementCount / maxCount;
//
//   Ports:  ListA = InputSlot<List<float>>; ListB = InputSlot<List<float>>; Threshold = InputSlot<float>(0).
//   Output: Difference = Slot<float> (ratio of differing elements; 1 when either list empty/null).
//
// ★ FAITHFUL PORT OF A TiXL QUIRK (named, NOT a fix — parity rule): the guard at CompareFloatLists.cs:35
// is `listA.Count < index || listB.Count < index` (strict `<`, not `<=`). With maxCount = max(countA,countB)
// and index in [0, maxCount-1], `Count < index` only trips when index > Count, i.e. index ≥ Count+1; an
// index EQUAL to Count (one past the end of the shorter list) does NOT trip the guard and falls through to
// `listA[index]`/`listB[index]`. In TiXL's C# this would index out of range and THROW for index == Count on
// the shorter list — but the op is virtually always used with EQUAL-LENGTH lists (the common case), where
// the guard never fires and every index is in range for both. To stay faithful WITHOUT crashing on the
// unequal-length edge, sw mirrors the guard verbatim AND bounds-checks the actual element access (an index
// past either list contributes a "differing element" — the same accounting the guard intends). This keeps
// the EQUAL-LENGTH result byte-identical to TiXL (the only safely-defined regime) and degrades the
// unequal-length edge to "every out-of-range slot counts as different" instead of a throw.
// fork-comparefloatlists-oob-quirk.
//
// EVAL-SIDE LAYOUT: single-output host-scalar contract (TWO FloatList inputs gathered in spec port order →
// inputLists[0]=ListA, inputLists[1]=ListB) — cooked by the driver's host-scalar branch, result stored in
// floatListBuf (transport) AND Node::outCache[0] (the BRIDGE evalFloat reads).
//
// FORKS (named):
//   - fork-comparefloatlists-empty-is-one: either list null/empty → Difference = 1 (max difference). sw's
//     gather: an UNWIRED FloatList input → no entry → that input is empty → 1. Matches TiXL null/empty→1.
//   - fork-comparefloatlists-oob-quirk: see above (faithful equal-length; bounded unequal-length edge).
#include <cmath>  // std::fabs

#include "runtime/graph.h"                    // NodeSpec, PortSpec, Widget
#include "runtime/host_scalar_op_registry.h"  // HostScalarOp / HostScalarCookCtx / hostScalarInjectBug / hostScalarParam

namespace sw {

namespace {

// CompareFloatLists: either list empty/absent → 1; else fraction of indices [0,maxCount) where
// |A[i]-B[i]| > threshold (an index past either list counts as differing). Mirrors CompareFloatLists.cs.
void cookCompareFloatLists(HostScalarCookCtx& c) {
  if (!c.output) return;
  *c.output = 1.0f;  // CompareFloatLists.cs:23 — either list null/empty → Difference = 1
  if (!c.inputLists || c.inputLists->size() < 2) return;  // need both ListA and ListB gathered
  const std::vector<float>& a = (*c.inputLists)[0];
  const std::vector<float>& b = (*c.inputLists)[1];
  if (a.empty() || b.empty()) {            // CompareFloatLists.cs:20-21 — Count==0 guard
    // *c.output already 1.0f
  } else {
    const float threshold = hostScalarParam(c.params, "Threshold", 0.0f);  // CompareFloatLists.cs:27
    int differentElementCount = 0;
    const int maxCount = (a.size() > b.size()) ? (int)a.size() : (int)b.size();  // cs:31
    for (int index = 0; index < maxCount; ++index) {  // cs:33
      // cs:35 verbatim quirk guard (strict `<`) + an OOB bounds-check (faithful equal-length; bounded edge).
      if ((int)a.size() < index || (int)b.size() < index ||
          index >= (int)a.size() || index >= (int)b.size()) {
        differentElementCount++;  // cs:37 — out-of-range slot counts as differing
        continue;
      }
      if (std::fabs(a[(size_t)index] - b[(size_t)index]) > threshold) {  // cs:41
        differentElementCount++;                                        // cs:43
      }
    }
    *c.output = (float)differentElementCount / (float)maxCount;  // cs:47
  }
  // Test-only: corrupt the REAL output on the actual cook path so the golden's RED bites via downstream
  // evalFloat (NOT by flipping the expected value). Off in production.
  if (hostScalarInjectBug()) *c.output = -999.0f;
}

}  // namespace

// Self-registration. File-scope static HostScalarOp — independent leaf .cpp (no shared edit point).
//   Ports: "Difference" = the Float output (differing-element ratio; host scalar via outCache + floatListBuf);
//          "ListA"      = the first FloatList input;
//          "ListB"      = the second FloatList input (gathered AFTER ListA → inputLists[1]);
//          "Threshold"  = the Float threshold param (|A[i]-B[i]| > threshold counts as differing).
// Output port FIRST (index 0) so outIdx 0 = Difference. ListA/ListB gather in spec port order
// (inputLists[0]=ListA, inputLists[1]=ListB). Threshold default 0 (TiXL InputSlot<float>()).
static const HostScalarOp _reg_comparefloatlists{
    {"CompareFloatLists", "CompareFloatLists",
     {{"Difference", "Difference", "Float", false},
      {"ListA", "ListA", "FloatList", true},
      {"ListB", "ListB", "FloatList", true},
      {"Threshold", "Threshold", "Float", true, 0.0f, 0.0f, 100000.0f}},
     /*evaluate=*/nullptr},  // host scalar comes from the cook driver, not the value-eval evaluate fn
    cookCompareFloatLists};

}  // namespace sw
