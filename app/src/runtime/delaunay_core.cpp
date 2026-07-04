// runtime/delaunay_core — impl. Authorities: DelaunayMesh.cs (poisson/polygon/boundary predicates,
// verbatim line cites below), .NET reference source Random.cs (the seeded Knuth subtractive
// generator), Bowyer-Watson per the standard incremental algorithm (fork-delaunay-bowyer-watson,
// header). runtime leaf: pure CPU.
#include "runtime/delaunay_core.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace sw {

// ─────────────────────────── DotNetRandom (.NET Random.cs, seeded path) ───────────────────────────
DotNetRandom::DotNetRandom(int seed) {
  // Random.cs GenerateSeedArray: MSEED = 161803398; subtraction guards Int32.MinValue.
  const int subtraction = (seed == std::numeric_limits<int>::min())
                              ? std::numeric_limits<int>::max()
                              : std::abs(seed);
  int mj = 161803398 - subtraction;
  seedArray_[55] = mj;
  int mk = 1;
  int ii = 0;
  for (int i = 1; i < 55; i++) {
    if ((ii += 21) >= 55) ii -= 55;  // ii = (21 * i) % 55
    seedArray_[ii] = mk;
    mk = mj - mk;
    if (mk < 0) mk += std::numeric_limits<int>::max();
    mj = seedArray_[ii];
  }
  for (int k = 1; k < 5; k++) {
    for (int i = 1; i < 56; i++) {
      int n = i + 30;
      if (n >= 55) n -= 55;
      // Random.cs: SeedArray[i] -= SeedArray[1 + (i + 30) % 55]  (as int64 in newer sources; the
      // classic int math never overflows here because both operands are in [0, Int32.Max)).
      seedArray_[i] -= seedArray_[1 + n];
      if (seedArray_[i] < 0) seedArray_[i] += std::numeric_limits<int>::max();
    }
  }
  inext_ = 0;
  inextp_ = 21;
}

int DotNetRandom::internalSample() {
  int locINext = inext_, locINextp = inextp_;
  if (++locINext >= 56) locINext = 1;
  if (++locINextp >= 56) locINextp = 1;
  int retVal = seedArray_[locINext] - seedArray_[locINextp];
  if (retVal == std::numeric_limits<int>::max()) retVal--;
  if (retVal < 0) retVal += std::numeric_limits<int>::max();
  seedArray_[locINext] = retVal;
  inext_ = locINext;
  inextp_ = locINextp;
  return retVal;
}

double DotNetRandom::sample() { return internalSample() * (1.0 / std::numeric_limits<int>::max()); }
int DotNetRandom::next(int maxValue) { return (int)(sample() * maxValue); }
double DotNetRandom::nextDouble() { return sample(); }

// ───────────────────────────── Bowyer-Watson (double-precision incircle) ─────────────────────────────
namespace {
struct TriD { int a, b, c; };

double orient2d(const DelaunayV2& p, const DelaunayV2& q, const DelaunayV2& r) {
  return ((double)q.x - p.x) * ((double)r.y - p.y) - ((double)q.y - p.y) * ((double)r.x - p.x);
}

// True iff d lies inside the circumcircle of CCW triangle (a,b,c) — the standard 3×3 determinant.
bool inCircumcircle(const DelaunayV2& a, const DelaunayV2& b, const DelaunayV2& c,
                    const DelaunayV2& d) {
  const double ax = (double)a.x - d.x, ay = (double)a.y - d.y;
  const double bx = (double)b.x - d.x, by = (double)b.y - d.y;
  const double cx = (double)c.x - d.x, cy = (double)c.y - d.y;
  const double det = (ax * ax + ay * ay) * (bx * cy - cx * by) -
                     (bx * bx + by * by) * (ax * cy - cx * ay) +
                     (cx * cx + cy * cy) * (ax * by - bx * ay);
  return det > 0.0;  // valid for CCW (a,b,c)
}
}  // namespace

std::vector<DelaunayTri> delaunayTriangulate(const std::vector<DelaunayV2>& pts) {
  std::vector<DelaunayTri> out;
  const int n = (int)pts.size();
  if (n < 3) return out;

  // Super-triangle generously enclosing the bbox.
  float minX = pts[0].x, maxX = pts[0].x, minY = pts[0].y, maxY = pts[0].y;
  for (const DelaunayV2& p : pts) {
    minX = std::fmin(minX, p.x); maxX = std::fmax(maxX, p.x);
    minY = std::fmin(minY, p.y); maxY = std::fmax(maxY, p.y);
  }
  const float dx = maxX - minX, dy = maxY - minY;
  const float dmax = std::fmax(std::fmax(dx, dy), 1e-3f);
  const float midX = (minX + maxX) * 0.5f, midY = (minY + maxY) * 0.5f;
  std::vector<DelaunayV2> work = pts;
  work.push_back({midX - 20.0f * dmax, midY - dmax});          // n
  work.push_back({midX, midY + 20.0f * dmax});                 // n+1
  work.push_back({midX + 20.0f * dmax, midY - dmax});          // n+2

  auto ccw = [&](TriD& t) {
    if (orient2d(work[(size_t)t.a], work[(size_t)t.b], work[(size_t)t.c]) < 0.0) std::swap(t.b, t.c);
  };
  std::vector<TriD> tris;
  TriD super{n, n + 1, n + 2};
  ccw(super);
  tris.push_back(super);

  struct Edge { int u, v; };
  for (int pi = 0; pi < n; ++pi) {
    const DelaunayV2& p = work[(size_t)pi];
    // Bad triangles (circumcircle contains p) → collect their boundary polygon.
    std::vector<TriD> keep;
    std::vector<Edge> poly;
    keep.reserve(tris.size());
    auto addEdge = [&](int u, int v) {
      // A shared edge appears twice with opposite direction — cancel it.
      for (size_t e = 0; e < poly.size(); ++e) {
        if (poly[e].u == v && poly[e].v == u) {
          poly.erase(poly.begin() + (long)e);
          return;
        }
      }
      poly.push_back({u, v});
    };
    for (const TriD& t : tris) {
      if (inCircumcircle(work[(size_t)t.a], work[(size_t)t.b], work[(size_t)t.c], p)) {
        addEdge(t.a, t.b);
        addEdge(t.b, t.c);
        addEdge(t.c, t.a);
      } else {
        keep.push_back(t);
      }
    }
    for (const Edge& e : poly) {
      TriD t{e.u, e.v, pi};
      ccw(t);
      keep.push_back(t);
    }
    tris.swap(keep);
  }

  out.reserve(tris.size());
  for (const TriD& t : tris) {
    if (t.a >= n || t.b >= n || t.c >= n) continue;  // drop super-vertex triangles
    out.push_back({t.a, t.b, t.c});                  // already CCW-normalized
  }
  return out;
}

// ─────────────────────────── DelaunayMesh.cs geometric predicates ───────────────────────────
bool pointInPolygon(const DelaunayV2& p, const std::vector<DelaunayV2>& polygon) {
  // DelaunayMesh.cs:417-432 IsPointInPolygon (ray casting), verbatim comparisons.
  bool inside = false;
  const int n = (int)polygon.size();
  for (int i = 0, j = n - 1; i < n; j = i++) {
    if ((polygon[(size_t)i].y > p.y) != (polygon[(size_t)j].y > p.y) &&
        p.x < (polygon[(size_t)j].x - polygon[(size_t)i].x) * (p.y - polygon[(size_t)i].y) /
                      (polygon[(size_t)j].y - polygon[(size_t)i].y) +
                  polygon[(size_t)i].x) {
      inside = !inside;
    }
  }
  return inside;
}

bool tooCloseToBoundary(const DelaunayV2& p, float minDistance,
                        const std::vector<DelaunayV2>& boundary) {
  // BoundaryGrid.IsTooCloseToBoundary predicate (DelaunayMesh.cs:659-691) — same squared-distance
  // strict `<` compare, linear scan instead of the grid (fork-delaunay-lineargrid, value-identical).
  const float minSq = minDistance * minDistance;
  for (const DelaunayV2& b : boundary) {
    const float dx = p.x - b.x, dy = p.y - b.y;
    if (dx * dx + dy * dy < minSq) return true;
  }
  return false;
}

std::vector<DelaunayV2> poissonDiscSamples(float minX, float maxX, float minY, float maxY,
                                           float radius, int seed) {
  // DelaunayMesh.cs:435-558 GeneratePoissonDiscSamples — verbatim walk, DotNetRandom for :439.
  std::vector<DelaunayV2> samples;
  std::vector<DelaunayV2> activeList;
  DotNetRandom random(seed);                                     // :439 new Random(seed)

  const float cellSize = radius / std::sqrt(2.0f);               // :442
  const int gridWidth = (int)std::ceil((maxX - minX) / cellSize);   // :443
  const int gridHeight = (int)std::ceil((maxY - minY) / cellSize);  // :444
  if (gridWidth <= 0 || gridHeight <= 0) return samples;
  std::vector<int> grid((size_t)gridWidth * gridHeight, -1);     // :447-449

  auto gridIndex = [&](float x, float y) -> int {                // :452-459
    int gx = (int)((x - minX) / cellSize);
    int gy = (int)((y - minY) / cellSize);
    if (gx < 0 || gx >= gridWidth || gy < 0 || gy >= gridHeight) return -1;
    return gx + gy * gridWidth;
  };

  const DelaunayV2 firstPoint{minX + (maxX - minX) * 0.5f, minY + (maxY - minY) * 0.5f};  // :462-465
  samples.push_back(firstPoint);
  activeList.push_back(firstPoint);
  {
    const int gi = gridIndex(firstPoint.x, firstPoint.y);
    if (gi >= 0) grid[(size_t)gi] = 0;                           // :470-472
  }

  const int maxAttempts = 30;                                    // :475
  while (!activeList.empty()) {                                  // :477
    const int randomIndex = random.next((int)activeList.size()); // :479
    const DelaunayV2 point = activeList[(size_t)randomIndex];
    bool foundCandidate = false;

    for (int i = 0; i < maxAttempts; i++) {                      // :483
      const float angle = (float)(random.nextDouble() * 2.0 * 3.14159265358979323846f);  // :486 MathF.PI
      const float distance = radius + (float)(random.nextDouble() * radius);             // :487
      const float newX = point.x + distance * std::cos(angle);   // :489
      const float newY = point.y + distance * std::sin(angle);
      const DelaunayV2 newPoint{newX, newY};
      if (newX < minX || newX >= maxX || newY < minY || newY >= maxY) continue;  // :494
      const int newGridIdx = gridIndex(newX, newY);              // :498
      if (newGridIdx < 0) continue;

      bool tooClose = false;
      const float radiusSquared = radius * radius;               // :505
      const int gridX = (int)((newX - minX) / cellSize);         // :508-509
      const int gridY = (int)((newY - minY) / cellSize);
      for (int dy2 = -2; dy2 <= 2 && !tooClose; dy2++) {         // :511 (5×5 neighborhood)
        for (int dx2 = -2; dx2 <= 2; dx2++) {
          const int checkX = gridX + dx2, checkY = gridY + dy2;
          if (checkX < 0 || checkX >= gridWidth || checkY < 0 || checkY >= gridHeight) continue;
          const int sampleIdx = grid[(size_t)(checkX + checkY * gridWidth)];
          if (sampleIdx >= 0) {
            const DelaunayV2& s = samples[(size_t)sampleIdx];
            const float dxv = newPoint.x - s.x, dyv = newPoint.y - s.y;
            if (dxv * dxv + dyv * dyv < radiusSquared) {         // :529-534
              tooClose = true;
              break;
            }
          }
        }
      }
      if (!tooClose) {                                           // :541-548
        samples.push_back(newPoint);
        activeList.push_back(newPoint);
        grid[(size_t)newGridIdx] = (int)samples.size() - 1;
        foundCandidate = true;
        break;
      }
    }
    if (!foundCandidate) activeList.erase(activeList.begin() + randomIndex);  // :551-554
  }
  return samples;
}

}  // namespace sw
