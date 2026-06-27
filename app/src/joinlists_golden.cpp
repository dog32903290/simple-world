// joinlists_golden — golden for JoinLists (Result-only) on the PointList currency (N-way concat):
//   --selftest-joinlists   (JoinLists: N host lists → ONE concatenation, in WIRE order)
//
// JoinLists concatenates its MultiInput StructuredList<Point> sources in wire-declaration order
// (JoinLists.cs: Result = list0.Join(list1..listK)). The cook driver gathers the MultiInput PointList port
// into inputLists in wire order; here we synthesize inputLists directly (same posture as the repeatatpoints
// / pointstocpu goldens) and assert the concatenated SwPoint sequence is correct AND ordered.
//
// ★FORK: the 2nd TiXL output `Length` (int) is NOT ported (joinlists-length-deferred, value-out deferred
//   seam). This golden asserts the Result list only.
//
// HAND-COMPUTED GOLDEN: three lists of lengths 1, 2, 1 with DISTINGUISHABLE Position.x = its global index:
//   list0 = [P(10)]            (Position.x = 10)
//   list1 = [P(20), P(21)]
//   list2 = [P(30)]
//   Result (wire order list0 ++ list1 ++ list2) = [P(10), P(20), P(21), P(30)], length 4.
//   The asserted sequence (10,20,21,30) catches BOTH a dropped list AND a reorder.
//
// injectBug routes through pointListInjectBug() so the RED case drops the last point on the REAL cook
// (Result = [10,20,21] ≠ [10,20,21,30] → FAIL), teeth on the op not the expected value.
#include <cmath>
#include <cstdio>
#include <vector>

#include "runtime/pointlist_op_registry.h"  // PointListCookCtx / pointListInjectBug / findPointListOp
#include "runtime/tixl_point.h"             // SwPoint (64B stride) + swPointDefault

namespace sw {

namespace {

bool near1(float a, float b) { return std::fabs(a - b) <= 1e-5f; }

SwPoint mkPt(float tag) {
  SwPoint p = swPointDefault();
  p.Position = {tag, 0.0f, 0.0f};  // Position.x = a unique tag so order + identity are unambiguous
  return p;
}

// Direct-cook one JoinLists over hand-built input lists (wire order = vector order). Returns the concat.
std::vector<SwPoint> runJoin(const std::vector<std::vector<SwPoint>>& lists) {
  std::vector<std::vector<SwPoint>> inputLists = lists;  // gather already in wire order
  std::vector<SwPoint> out;
  PointListCookCtx pc;
  pc.inputLists = &inputLists;
  pc.output = &out;
  const PointListCookFn* fn = findPointListOp("JoinLists");
  if (fn && *fn) (*fn)(pc);
  return out;
}

bool seqEq(const std::vector<SwPoint>& got, const std::vector<float>& wantTags) {
  if (got.size() != wantTags.size()) return false;
  for (size_t i = 0; i < got.size(); ++i)
    if (!near1(got[i].Position.x, wantTags[i])) return false;
  return true;
}

}  // namespace

int runJoinListsSelfTest(bool injectBug) {
  bool ok = true;
  pointListInjectBug() = injectBug;

  std::vector<SwPoint> l0 = {mkPt(10)};
  std::vector<SwPoint> l1 = {mkPt(20), mkPt(21)};
  std::vector<SwPoint> l2 = {mkPt(30)};

  // CASE A — 3 lists (1,2,1) → concat [10,20,21,30] in wire order. EXPECTED is fixed (NOT bug-adjusted):
  // injectBug drops the last point on the REAL cook → [10,20,21] ≠ want → RED. The tag sequence catches
  // both a dropped list (size/tag mismatch) AND a reorder (e.g. [10,30,20,21]).
  {
    std::vector<SwPoint> got = runJoin({l0, l1, l2});
    bool pass = seqEq(got, {10, 20, 21, 30});
    ok = ok && pass;
    std::printf("[selftest-joinlists] A(1+2+1) size=%zu tags=[%s] -> %s\n", got.size(),
                got.size() == 4 ? "10,20,21,30" : "(mismatch)", pass ? "PASS" : "FAIL");
  }

  // CASE B — single list → Result == that list (TiXL Count==1 → TypedClone). No reorder possible; size 2.
  {
    std::vector<SwPoint> got = runJoin({l1});
    bool pass = seqEq(got, {20, 21});
    ok = ok && pass;
    std::printf("[selftest-joinlists] B(single) size=%zu -> %s\n", got.size(), pass ? "PASS" : "FAIL");
  }

  // CASE C — zero wired lists → empty Result (TiXL Count==0 → no Result). injectBug can't drop from empty.
  {
    std::vector<SwPoint> got = runJoin({});
    bool pass = got.empty();
    ok = ok && pass;
    std::printf("[selftest-joinlists] C(empty) size=%zu -> %s\n", got.size(), pass ? "PASS" : "FAIL");
  }

  // CASE D — an empty list in the middle contributes 0 points but does not break order: [10] ++ [] ++ [30].
  {
    std::vector<SwPoint> got = runJoin({l0, {}, l2});
    bool pass = seqEq(got, {10, 30});
    ok = ok && pass;
    std::printf("[selftest-joinlists] D(empty-middle) size=%zu -> %s\n", got.size(), pass ? "PASS" : "FAIL");
  }

  pointListInjectBug() = false;
  std::printf("[selftest-joinlists] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
