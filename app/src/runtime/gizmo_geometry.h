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

}  // namespace sw
