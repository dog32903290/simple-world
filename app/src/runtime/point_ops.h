// runtime/point_ops — the built-in point operators (cook fns) for the lane-A point graph.
// Each is a faithful port of its TiXL counterpart (external/tixl .../point/**): crawl the
// .cs/.hlsl/.help, port the kernel + a cook fn, prove it with a golden selftest against
// TiXL's own formula. They register into point_graph's table via registerBuiltinPointOps()
// (declared in point_graph.h) — called once at app startup and by each cook selftest.
// Adding an operator = one cook fn + one registerPointOp line + its .metal + its golden.
#pragma once
namespace sw {
// Headless golden proof of the RadialPoints cook op THROUGH the point-graph: cook
// RadialPoints -> a capture draw, assert the bag lies on a circle of the requested radius
// and is spread around it. injectBug sets Cycles=0 so all points collapse to one angle
// (spread -> 0) and the test FAILS — real degenerate, not a flipped assertion.
int runRadialOpSelfTest(bool injectBug);
// Golden proof of the DrawPoints draw op: cook RadialPoints -> DrawPoints (real renderer),
// assert a lit ring + black center in the target texture. injectBug (0 points) -> all black.
int runDrawOpSelfTest(bool injectBug);
// Golden proof of the ParticleSystem sim op: cook RadialPoints->ParticleSystem(sim)->capture,
// step N frames, assert turbulence pushed points off the ring. injectBug (Amount=0) -> no flow.
int runSimOpSelfTest(bool injectBug);
}  // namespace sw
