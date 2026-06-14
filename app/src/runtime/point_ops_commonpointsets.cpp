// CommonPointSets — batch 37 GENERATOR op. CPU-FILL NAMED FORK of external/tixl
// .../point/generate/CommonPointSets.cs (read line-by-line; quoted in the dossier).
//
// TiXL's CommonPointSets is a pure-CPU StructuredList generator: a `Set` enum picks one of
// seven hard-coded vertex tables (Cross/CrossXY/Cube/Quad/ArrowX/ArrowY/ArrowZ). Each table
// is a list of `new Point{ Position=Vector3(...), F1=1, Color=One, F2=1 }` rows plus
// Point.Separator() rows (which draw-line ops use to break the strip). Init() also fills
// Orientation=Identity and Color=One on every row (CommonPointSets.cs:49-51).
//
// NAMED FORK (who/why/authority): our cook is a CPU memcpy of the selected table straight into
// c.output->contents() — NO GPU shader, NO .metal. This is faithful: TiXL itself builds the CPU
// StructuredList first (CommonPointSets.cs:36-55) and only mirrors it to a GPU buffer
// (CommonPointSets.cs:57-85) for the GpuBuffer output slot. We consume the CPU side directly
// (point_graph_selftest.cpp stubGen is the CPU-fill template: `SwPoint* dst=(SwPoint*)c.output
// ->contents(); dst[i]=...`). Authority: CommonPointSets.cs:106-250 (the tables + S constant).
//
// COUNT POLICY: each table has a DIFFERENT length, so the bag size depends on Set. We use the
// established RepetitionPoints/PairPointsForLines file-static stash pattern: cookCommonPointSets
// resolves the Set, writes the selected table's length into g_cpsCount, and cpsCountTransform
// reads it back so PointGraph::nodeCount sizes the bag to the real vertex count. The first cook
// after a Set flip lags one cook (the driver sizes the bag BEFORE calling cook); the golden warms
// up before asserting. Single-threaded cook -> the static is selftest-safe.
//
// Self-contained leaf: own capture vector + registerDrawOp (mirrors point_ops_repetitionpoints.cpp).
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"        // Graph/Node/pinId
#include "runtime/point_graph.h"  // PointCookCtx, registerPointOp/DrawOp, PointGraph
#include "runtime/tixl_point.h"   // SwPoint (64B) + EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// CommonPointSets.cs:106 — `private const float S = 0.5f;`
constexpr float S = 0.5f;
// Point.Separator() (Core/DataTypes/Point.cs:37-48): Scale = (NaN,NaN,NaN); all else normal.
constexpr float NANF = std::numeric_limits<float>::quiet_NaN();

// One row of a vertex table. Only Position varies between rows; the rest are filled by the
// emit loop to the TiXL Point() / Init() defaults (F1=1, Orientation=Identity, Color=One,
// Scale=One, F2=1) — EXCEPT separator rows whose Scale is (NaN,NaN,NaN).
struct CpsRow {
  float x, y, z;
  bool  separator;
};

#define PT(px, py, pz) CpsRow{(px), (py), (pz), false}
#define SEP            CpsRow{0.0f, 0.0f, 0.0f, true}

// ---- The seven hard-coded tables, transcribed verbatim from CommonPointSets.cs ----

// CrossPoints — CommonPointSets.cs:108-120 (9 rows: 3 axis pairs + separators).
const CpsRow kCross[] = {
    PT(0, -S, 0), PT(0, S, 0), SEP,
    PT(-S, 0, 0), PT(S, 0, 0), SEP,
    PT(0, 0, -S), PT(0, 0, S), SEP,
};

// CrossXYPoints — CommonPointSets.cs:122-131 (6 rows).
const CpsRow kCrossXY[] = {
    PT(0, -S, 0), PT(0, S, 0), SEP,
    PT(-S, 0, 0), PT(S, 0, 0), SEP,
};

// CubePoints — CommonPointSets.cs:133-182 (36 rows: 12 edges, each a 2-vertex pair + separator).
const CpsRow kCube[] = {
    PT(-S, -S, S), PT(S, -S, S), SEP,
    PT(-S, S, S),  PT(S, S, S),  SEP,
    PT(-S, -S, -S), PT(S, -S, -S), SEP,
    PT(-S, S, -S), PT(S, S, -S), SEP,
    PT(-S, -S, S), PT(-S, S, S), SEP,
    PT(S, -S, S),  PT(S, S, S),  SEP,
    PT(-S, -S, -S), PT(-S, S, -S), SEP,
    PT(S, -S, -S), PT(S, S, -S), SEP,
    PT(-S, -S, -S), PT(-S, -S, S), SEP,
    PT(S, -S, -S), PT(S, -S, S), SEP,
    PT(-S, S, -S), PT(-S, S, S), SEP,
    PT(S, S, -S),  PT(S, S, S),  SEP,
};

// QuadPoints — CommonPointSets.cs:184-192 (6 rows: a closed square loop + separator).
const CpsRow kQuad[] = {
    PT(-S, -S, 0), PT(+S, -S, 0), PT(+S, +S, 0), PT(-S, +S, 0), PT(-S, -S, 0), SEP,
};

// ArrowXPoints — CommonPointSets.cs:194-204 (7 rows: shaft pair + sep + 3-point head + sep).
const CpsRow kArrowX[] = {
    PT(-S, 0, 0), PT(+S, 0, 0), SEP,
    PT(S / 1.5f, -S / 4, 0), PT(+S, 0, 0), PT(S / 1.5f, S / 4, 0), SEP,
};

// ArrowYPoints — CommonPointSets.cs:206-216 (7 rows).
const CpsRow kArrowY[] = {
    PT(0, -S, 0), PT(0, +S, 0), SEP,
    PT(-S / 4, S / 1.5f, 0), PT(0, +S, 0), PT(S / 4, S / 1.5f, 0), SEP,
};

// ArrowZPoints — CommonPointSets.cs:218-228 (7 rows).
const CpsRow kArrowZ[] = {
    PT(0, 0, -S), PT(0, 0, +S), SEP,
    PT(-S / 4, 0, S / 1.5f), PT(0, 0, +S), PT(S / 4, 0, S / 1.5f), SEP,
};

#undef PT
#undef SEP

// Definitions list — CommonPointSets.cs:241-250 (table order == Shapes enum order).
struct CpsTable { const CpsRow* rows; uint32_t count; };
const CpsTable kTables[] = {
    {kCross,   (uint32_t)(sizeof(kCross) / sizeof(CpsRow))},     // 0 Cross
    {kCrossXY, (uint32_t)(sizeof(kCrossXY) / sizeof(CpsRow))},   // 1 CrossXY
    {kCube,    (uint32_t)(sizeof(kCube) / sizeof(CpsRow))},      // 2 Cube
    {kQuad,    (uint32_t)(sizeof(kQuad) / sizeof(CpsRow))},      // 3 Quad
    {kArrowX,  (uint32_t)(sizeof(kArrowX) / sizeof(CpsRow))},    // 4 ArrowX
    {kArrowY,  (uint32_t)(sizeof(kArrowY) / sizeof(CpsRow))},    // 5 ArrowY
    {kArrowZ,  (uint32_t)(sizeof(kArrowZ) / sizeof(CpsRow))},    // 6 ArrowZ
};
constexpr uint32_t kSetCount = (uint32_t)(sizeof(kTables) / sizeof(CpsTable));

// Resolve the Set enum to a valid table index (TiXL GetEnumValue clamps via the enum range;
// we clamp defensively against a corrupted .swproj — same posture as forcekindcorrupt probe).
uint32_t resolveSet(const PointCookCtx& c) {
  float setF = cookParam(c, "Set", 0.0f);
  int   set  = (int)std::lround(setF);
  if (set < 0) set = 0;
  if (set >= (int)kSetCount) set = (int)kSetCount - 1;
  return (uint32_t)set;
}

// Vertex count of the LAST-cooked CommonPointSets node — written by cook, read by
// cpsCountTransform so the bag is sized to the selected table's length. Single-threaded -> safe.
uint32_t g_cpsCount = kTables[0].count;

uint32_t cpsCountTransform(uint32_t /*natural*/) {
  // The natural count (from a Count Float port) is irrelevant here: the table length IS the
  // count. Return the stashed length so PointGraph::nodeCount sizes the bag exactly.
  return g_cpsCount;
}

void cookCommonPointSets(PointCookCtx& c) {
  // Resolve the Set FIRST so the static is up to date for the next sizing pass.
  uint32_t set = resolveSet(c);
  const CpsTable& tbl = kTables[set];
  g_cpsCount = tbl.count;

  if (!c.output || c.count == 0) return;

  SwPoint* dst = (SwPoint*)c.output->contents();
  // The driver sized the bag via cpsCountTransform; guard against a stale/lagging bag size.
  uint32_t n = c.count < tbl.count ? c.count : tbl.count;
  for (uint32_t i = 0; i < n; ++i) {
    const CpsRow& r = tbl.rows[i];
    SwPoint& p = dst[i];
    p.Position = {r.x, r.y, r.z};        // verbatim Vector3 from the .cs table
    p.FX1      = 1.0f;                    // F1 = 1 (Point() default / table init)
    p.Rotation = {0.0f, 0.0f, 0.0f, 1.0f};  // Orientation = Quaternion.Identity
    p.Color    = {1.0f, 1.0f, 1.0f, 1.0f};  // Color = Vector4.One
    p.FX2      = 1.0f;                    // F2 = 1
    if (r.separator) {
      p.Scale = {NANF, NANF, NANF};      // Point.Separator(): Scale = (NaN,NaN,NaN)
    } else {
      p.Scale = {1.0f, 1.0f, 1.0f};      // Scale = Vector3.One
    }
  }
}

// --- golden plumbing (self-contained: own capture vector + draw op) ---
std::vector<SwPoint>* g_capCps = nullptr;
void captureDrawCps(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_capCps || !pts || c.count == 0) return;
  g_capCps->assign(c.count, SwPoint{});
  std::memcpy(g_capCps->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

}  // namespace

void registerCommonPointSetsOp() {
  registerPointOp("CommonPointSets", cookCommonPointSets,
                  /*stateNew=*/nullptr, /*stateFree=*/nullptr,
                  cpsCountTransform,
                  /*countFromFirstPointsInput=*/false);
}

// Golden — three teeth, all through the real pg.cook + readback (capture draw op):
//
//   CASE A (Cross, default Set=0): assert count==9 AND the exact coordinate table:
//     rows 0,1 = (0,∓S,0); rows 3,4 = (∓S,0,0); rows 6,7 = (0,0,∓S); rows 2,5,8 = separators
//     (Scale.x == NaN). Proves the verbatim Cross table + separator placement.
//
//   CASE B (Cube, Set=2): assert count==36 AND a sampled corner: row 0 = (-S,-S,S),
//     row 1 = (S,-S,S). Different count AND coords from Cross -> proves enum ROUTING.
//
//   CASE C (route distinctness): cook Set=3 (Quad) -> count==6, row 0 = (-S,-S,0),
//     row 3 = (-S,+S,0) (the top-left corner), row 4 = (-S,-S,0) (the closing-loop vertex,
//     == row 0), row 5 = separator. Confirms a THIRD
//     distinct table (count differs from both A's 9 and B's 36 — separates count-collision).
//
//   injectBug: with the bug flag, CASE A asserts the WRONG Cross table (row 0 expected at
//     (S,0,0) instead of (0,-S,0)) -> the faithful fill mismatches -> FAIL. A real table flip,
//     not an inverted assert.
int runCommonPointSetsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  MTL::Device*       dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue*   q = dev->newCommandQueue();
  NS::Error*         err = nullptr;
  MTL::Library*      lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-commonpointsets] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerCommonPointSetsOp();
  std::vector<SwPoint> captured;
  g_capCps = &captured;
  registerDrawOp("DrawPoints", captureDrawCps);

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  // build a 1-node CommonPointSets graph + DrawPoints capture; cook twice so cpsCountTransform
  // settles the bag size to the selected table BEFORE we assert (the driver sizes the bag with
  // the PREVIOUS cook's stash).
  auto cookSet = [&](float setVal) {
    PointGraph pg(dev, lib, q, 64, 64);
    Node gen; gen.id = 1; gen.type = "CommonPointSets";
    gen.params["Set"] = setVal;
    Graph g;
    g.nodes.push_back(gen);
    Node drw; drw.id = 2; drw.type = "DrawPoints"; g.nodes.push_back(drw);
    g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});
    pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));  // warm-up: settle g_cpsCount
    pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));  // assert pass
  };

  auto near = [](float a, float b) { return std::fabs(a - b) < 1e-4f; };
  bool pass = true;

  // ===== CASE A: Cross (Set=0) =====
  {
    cookSet(0.0f);
    bool countOk = (captured.size() == 9);
    bool coordOk = countOk, sepOk = countOk;
    if (countOk) {
      // separators at 2,5,8
      sepOk = std::isnan(captured[2].Scale.x) && std::isnan(captured[5].Scale.x) &&
              std::isnan(captured[8].Scale.x);
      // injectBug flips the expected row-0 to a wrong table entry -> mismatch -> FAIL.
      float ex0y = injectBug ? 0.0f : -S;
      float ex0x = injectBug ? S    : 0.0f;
      coordOk = near(captured[0].Position.x, ex0x) && near(captured[0].Position.y, ex0y) &&
                near(captured[1].Position.x, 0.0f) && near(captured[1].Position.y, S) &&
                near(captured[3].Position.x, -S) && near(captured[3].Position.y, 0.0f) &&
                near(captured[4].Position.x, S) &&
                near(captured[6].Position.z, -S) && near(captured[7].Position.z, S);
      // real (non-sep) rows must NOT be NaN and carry the defaults.
      if (!std::isnan(captured[0].Scale.x)) {
        coordOk = coordOk && near(captured[0].Scale.x, 1.0f) && near(captured[0].FX1, 1.0f) &&
                  near(captured[0].Rotation.w, 1.0f) && near(captured[0].Color.x, 1.0f);
      }
    }
    printf("[selftest-commonpointsets] A(Cross) count=%zu coord=%s sep=%s\n",
           captured.size(), coordOk ? "ok" : "NO", sepOk ? "ok" : "NO");
    pass = pass && countOk && coordOk && sepOk;
  }

  // ===== CASE B: Cube (Set=2) =====
  {
    cookSet(2.0f);
    bool countOk = (captured.size() == 36);
    bool coordOk = countOk;
    if (countOk) {
      coordOk = near(captured[0].Position.x, -S) && near(captured[0].Position.y, -S) &&
                near(captured[0].Position.z, S) &&
                near(captured[1].Position.x, S) && near(captured[1].Position.y, -S) &&
                near(captured[1].Position.z, S) &&
                std::isnan(captured[2].Scale.x);  // edge separator
    }
    printf("[selftest-commonpointsets] B(Cube) count=%zu coord=%s\n",
           captured.size(), coordOk ? "ok" : "NO");
    pass = pass && countOk && coordOk;
  }

  // ===== CASE C: Quad (Set=3) — third distinct count/table =====
  {
    cookSet(3.0f);
    bool countOk = (captured.size() == 6);
    bool coordOk = countOk;
    if (countOk) {
      coordOk = near(captured[0].Position.x, -S) && near(captured[0].Position.y, -S) &&
                near(captured[3].Position.x, -S) && near(captured[3].Position.y, S) &&
                near(captured[4].Position.x, -S) && near(captured[4].Position.y, -S) &&
                std::isnan(captured[5].Scale.x);  // trailing separator
    }
    printf("[selftest-commonpointsets] C(Quad) count=%zu coord=%s\n",
           captured.size(), coordOk ? "ok" : "NO");
    pass = pass && countOk && coordOk;
  }

  printf("[selftest-commonpointsets] -> %s\n", pass ? "PASS" : "FAIL");

  g_capCps = nullptr;
  g_cpsCount = kTables[0].count;
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
