// runtime/delaunay_core — the CPU pieces DelaunayMesh cooks with (mesh_ops_delaunaymesh.cpp):
//   • DotNetRandom  — transcription of .NET System.Random(seed) (Knuth subtractive generator; the
//     seeded path is the Net5-compat/.NET-Framework algorithm, stable across .NET versions). TiXL's
//     Poisson sampler seeds `new System.Random(seed)` (DelaunayMesh.cs:439) — fill-point PARITY
//     requires this exact sequence, not any stdlib RNG.
//   • delaunayTriangulate — Bowyer-Watson Delaunay triangulation over 2D points, each output triangle
//     normalized CCW. ★NAMED FORK (fork-delaunay-bowyer-watson): TiXL triangulates via the
//     DelaunatorSharp LIBRARY (DelaunayMesh.cs:4,:284). The Delaunay triangulation of a point set in
//     general position is UNIQUE, so the triangle SET matches; the triangle ORDER and each triangle's
//     starting vertex are implementation-specific and NOT parity surface (a mesh with the same
//     triangle set renders identically). Degenerate co-circular sets may tessellate differently.
//   • poissonDiscSamples — verbatim port of DelaunayMesh.cs:435-558 GeneratePoissonDiscSamples
//     (grid-accelerated dart throwing, maxAttempts=30, first sample = bbox center), driven by
//     DotNetRandom for sequence parity.
//   • pointInPolygon — DelaunayMesh.cs:417-432 IsPointInPolygon (ray casting), verbatim.
//   • tooCloseToBoundary — the BoundaryGrid.IsTooCloseToBoundary PREDICATE (DelaunayMesh.cs:659-691)
//     as a linear scan. ★NAMED FORK (fork-delaunay-lineargrid): TiXL's spatial grid is a pure
//     accelerator — same min-distance predicate, value-identical result; only wall-clock differs.
// runtime leaf: pure CPU, no Metal/UI/upward deps.
#pragma once

#include <cstdint>
#include <vector>

namespace sw {

struct DelaunayV2 { float x, y; };
struct DelaunayTri { int a, b, c; };  // CCW-normalized

// .NET System.Random(seed) — Knuth subtractive lagged-Fibonacci (transcribed from the .NET reference
// source Random.Net5CompatSeedImpl / Random.cs; the seeded ctor keeps this algorithm for compat).
class DotNetRandom {
 public:
  explicit DotNetRandom(int seed);
  int next(int maxValue);   // Random.Next(maxValue) = (int)(Sample() * maxValue)
  double nextDouble();      // Random.NextDouble() = Sample()
 private:
  int internalSample();
  double sample();
  int seedArray_[56] = {0};
  int inext_ = 0;
  int inextp_ = 21;
};

// Bowyer-Watson over `pts` (double-precision circumcircle test); returns CCW triangles indexing pts.
// Duplicate/degenerate inputs yield whatever survives the epsilon guards (callers feed real geometry).
std::vector<DelaunayTri> delaunayTriangulate(const std::vector<DelaunayV2>& pts);

// DelaunayMesh.cs:417-432 (ray casting; strict `>` / `<` comparisons verbatim).
bool pointInPolygon(const DelaunayV2& p, const std::vector<DelaunayV2>& polygon);

// BoundaryGrid.IsTooCloseToBoundary predicate (DelaunayMesh.cs:659-691) as a linear scan
// (fork-delaunay-lineargrid): true iff any boundary point is nearer than minDistance.
bool tooCloseToBoundary(const DelaunayV2& p, float minDistance,
                        const std::vector<DelaunayV2>& boundary);

// DelaunayMesh.cs:435-558 GeneratePoissonDiscSamples, verbatim (radius = fillDensity; seed →
// DotNetRandom). First sample = bbox center; candidates rejected outside [min,max) or within
// `radius` of an accepted sample (5×5 grid neighborhood, exactly the .cs walk).
std::vector<DelaunayV2> poissonDiscSamples(float minX, float maxX, float minY, float maxY,
                                           float radius, int seed);

}  // namespace sw
