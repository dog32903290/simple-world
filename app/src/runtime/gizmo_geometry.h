// gizmo_geometry — pure-CPU line-segment endpoint generators for the gizmo draw family (camera3d C3).
//
// SEAM ROLE (C3_GIZMO_BLUEPRINT.md §2): every wireframe gizmo (cone/grid/box/sphere/locator) is the same
// shape: "generate a StructuredList<Point> of line-segment endpoints with Point.Separator() breaks, then
// hand it to the Lines path." TiXL's ConeGizmo (ConeGizmo.cs:48-107) is the canonical pattern — build a
// point list with separators, output it, downstream DrawLines rasterizes. This header is the SW analogue:
// a small CPU helper each gizmo cook calls. It is the Tranche-0 seam, owner-locked first, then released
// for the Tranche-1 fan-out (grid/box/sphere/locator each add one emit fn here + one ~30-line cook leaf).
//
// PURE CPU: zero camera, zero Metal, zero register-table touch. It just appends SwPoints into a host
// vector. It does NOT know about NodeSpec / cook flows — the gizmo leaf owns that wiring (ConeGizmo
// registers as a PointListOp, exactly like LinePointsCpu). The helper only produces geometry.
//
// FORK (named): cpupoint-reuses-swpoint — TiXL Point.Separator() (Point.cs:45) sets Scale=(NaN,NaN,NaN);
//   on SwPoint (the 64B host point, tixl_point.h) the same break is Scale = NaN on all three packed_float3
//   components. The Lines rasterizer (draw_lines.metal) pushes a NaN-Scale point offscreen, so the trailing
//   separator draws nothing = a polyline break (same convention LinePointsCpu uses, verified there).
//
// YAGNI (blueprint §4 warning): Tranche 0 ships ONLY what ConeGizmo needs (emitConeLines). The rest of the
// family (emitGridLines / emitBoxEdges / emitSphereRings / emitAxisCross) is Tranche-1 work — NOT stubbed
// here. Adding the next gizmo adds its emit fn alongside emitConeLines, no speculative scaffold now.
#pragma once

#include <vector>

struct SwPoint;  // full def runtime/tixl_point.h (64B host point); incomplete type is fine for the decl.

namespace sw {

// Append the SwPoint that marks a polyline break (Point.Separator(), Point.cs:45 → Scale=NaN). Renderers
// / line builders must not connect a segment across this point; the Lines shader pushes it offscreen so it
// draws nothing. Public so each gizmo cook (and its golden) emits breaks the same way.
void gizmoEmitSeparator(std::vector<SwPoint>& out);

// emitConeLines — transcribe ConeGizmo.cs:30-107 verbatim. Generates the wireframe audio-cone point list:
//   - base circle: `segments` line segments (2 points + 1 separator each), radius = tan(angle/2)·length,
//     in the plane z = -length (the cone extends -Z = forward, ConeGizmo.cs:36).
//   - apex rays: `rayCount` lines from the apex (origin) to a base-circle point (2 points + 1 separator).
// angleDegrees is the FULL cone spread (BASS convention, 0-360); the geometry uses the HALF angle
// (ConeGizmo.cs:31-33). The helper clamps segments>=3 and rayCount>=0 exactly as ConeGizmo.cs:27-28.
// out is CLEARED first (the cook rewrites its whole list every frame, like LinePointsCpu).
void emitConeLines(std::vector<SwPoint>& out, float angleDegrees, float length, int segments, int rayCount);

// ── Tranche 1 (C3_GIZMO_BLUEPRINT §4) — wireframe gizmo emitters ────────────────────────────────
// All four follow the ConeGizmo (emitConeLines) pattern: build a StructuredList<Point> of line-segment
// endpoints with Point.Separator() breaks (gizmoEmitSeparator), output CLEARED first. Downstream a
// ListToBuffer→DrawLines path rasterizes them (DrawLines connects Points[i]→Points[i+1], collapses a
// segment touching a NaN-Scale separator — draw_lines.metal:67-74). Geometry transcribed from TiXL.

// emitGridLines — a wireframe grid plane: `segsX`×`segsY` cells → (segsX+1) lines one way + (segsY+1)
// the other, each a 2-point + 1-separator segment, spanning [-0.5,0.5] in the chosen plane (extent ±0.5
// matches CommonPointSets' S=0.5 unit-cell convention; the cook scales by UniformScale). `orientation`
// picks the plane: 0 = XY (z=0), 1 = XZ (y=0), 2 = YZ (x=0).
//   NAMED FORK [fork-gizmo-grid-composite]: TiXL DrawLineGrid.t3 builds the grid by REPEATING a
//   LinePoints generator at 101 points via RepeatAtPoints (+ axis cross + center markers + per-line
//   color), a multi-node sub-graph SW has no .t3 inliner to expand (blueprint §0 fact 3). This emitter
//   COMPOSES the SAME VISIBLE geometry (a regular wireframe grid) directly in CPU, the blueprint-§2
//   "transcribe the visible wireframe, ride DrawLines" approach — the internal node decomposition is
//   not reproduced (no per-line color gradient, no ShowAxis cross — geometry only, white points).
//   segsX/segsY are clamped >=1 (Math.Max(1) — a grid needs at least one cell).
void emitGridLines(std::vector<SwPoint>& out, int segsX, int segsY, int orientation);

// emitBoxEdges — the 12 edges of a unit cube [-0.5,0.5]^3, each a 2-point + 1-separator segment.
// VERBATIM transcription of CommonPointSets.CubePoints (CommonPointSets.cs:104-167, the Set=2 buffer
// DrawBoxGizmo.t3 wires into DrawLines): S=0.5, the 12 edges in CommonPointSets' exact order. The cook
// applies Stretch/Scale/Position via the downstream transform (DrawBoxGizmo.t3 Transform child).
void emitBoxEdges(std::vector<SwPoint>& out);

// emitSphereRings — a wireframe sphere as `rings` latitude circles + `rings` longitude circles, each a
// closed loop of `segments` line-segments (2 points + separator per segment), on the unit sphere
// (radius applied by the cook). A standard lat/long wireframe ball.
//   NAMED FORK [fork-gizmo-sphere-composite]: TiXL DrawSphereGizmo.t3 builds the ball from RadialPoints
//   (a 50-point closed circle) REPEATED around an axis via RepeatAtPoints + a DrawPoints dot layer +
//   inner-radius scaling — a multi-node composite (blueprint §0 fact 3). This emitter composes the SAME
//   VISIBLE geometry (a lat/long wireframe sphere) directly, per blueprint §2; the InnerRadius dot layer
//   and the exact RepeatAtPoints ring placement are not reproduced (geometry only, white points).
void emitSphereRings(std::vector<SwPoint>& out, int rings, int segments);

// emitAxisCross — a 3-axis cross marker: a line on each of ±X, ±Y, ±Z through the origin (2 points + 1
// separator per axis), spanning [-0.5,0.5]. VERBATIM transcription of CommonPointSets.CrossPoints
// (CommonPointSets.cs:73-83, Set=0): the Locator's marker geometry (Locator.t3 wires a cross into
// DrawLines). The cook applies Position/Size via the downstream transform.
//   NAMED FORK [fork-gizmo-screen-constant]: TiXL Locator outputs DistanceToCamera (Locator.cs:13), a
//   screen-constant tell, AND a bitmapfont Label (Locator.cs:38, Examples:fonts/Roboto-Black.fnt). The
//   label needs the bitmapfont seam (CAMERA3D_BLUEPRINT §downstream-gated) → DROPPED (geometry only).
//   The cross geometry itself is world-space (no distance-divide in the point generation), so v1 sizing
//   is world-space, NOT fixed-pixel — the constant-size-on-screen behaviour is out-of-C3-scope (§6).
void emitAxisCross(std::vector<SwPoint>& out);

}  // namespace sw
