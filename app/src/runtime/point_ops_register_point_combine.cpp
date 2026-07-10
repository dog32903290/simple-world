// runtime/point_ops_register_point_combine — per-family registrar for point COMBINE ops
// (multi-input -> one bag). Split from point_ops.cpp's central registerBuiltinPointOps
// (node_registry.cpp pattern, ARCHITECTURE rule 7). Adding a combine op edits ONLY this file.
//
// Zero behaviour change: op name + cook binding verbatim from the original central function.
#include "runtime/point_graph.h"  // registerPointOp (via leaf fn)

namespace sw {

// Leaf register fns (defined in their respective leaf cpp files, no shared header).
// (CombineBuffers flat atom RETIRED — replaced by the .t3 compound; see node_registry_point_combine.cpp.)
// (SnapToPoints flat atom RETIRED 2026-07-10, 廢棄節點退場 — the .t3 compound (guid 5822b0d8…) provides
//  it now; mathv verifies the kernel, t3-snaptopoints-retire the takeover. §5.)
void registerPairPointsForLinesOp();  // batch 24: pair A+B with NaN divider -> DrawLines
void registerPickPointListOp();       // batch 24: multi-input select by Index
void registerPairPointsForSplinesOp();        // batch 10: Hermite spline strip per pair
void registerSplinePointsOp();                // even arc-length resample of a cardinal cubic spline
void registerPairPointsForGridWalkLinesOp();  // batch 10: 11-step grid-walk polyline per pair
// (BlendPoints flat atom RETIRED 2026-07-10, 廢棄節點退場 — the .t3 compound (guid 2dc5c9d1…) provides
//  it now; mathv verifies the kernel, t3-blendpoints-retire the takeover. §5.)
void registerMultiUpdatePointsOp();           // point lane: pass-through last wired buffer (TiXL _internal)
void registerRepeatAtPointsOp();              // count-product seam: cartesian product source.N * target.N
void registerGrowStrainsOp();                 // 2-input product + GrowthMap texture (count=(A+1)*B)

void registerPointCombinePointOps() {
  // CombineBuffers flat atom RETIRED (廢棄節點退場 pilot #2) — the .t3 compound provides it now.
  // SnapToPoints flat atom RETIRED (廢棄節點退場) — the .t3 compound provides it now.
  registerPairPointsForLinesOp();
  registerPickPointListOp();
  registerPairPointsForSplinesOp();
  registerSplinePointsOp();
  registerPairPointsForGridWalkLinesOp();
  // BlendPoints flat atom RETIRED (廢棄節點退場) — the .t3 compound provides it now.
  registerMultiUpdatePointsOp();
  registerRepeatAtPointsOp();
  registerGrowStrainsOp();
}

}  // namespace sw
