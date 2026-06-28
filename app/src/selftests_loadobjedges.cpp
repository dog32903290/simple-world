// selftests_loadobjedges — LoadObjEdges golden (--selftest-loadobjedges / -bug). HERMETIC, byte-exact.
//
// LoadObjEdges (runtime/pointlist_ops_loadobjedges.cpp) loads a .OBJ and emits its UNIQUE FACE EDGES as
// from → to → Separator point triples (2nd consumer of the obj_parse seam, pointlist String-path channel).
// The pointlist String channel is FLAT-cook only (fork-pointlist-flat-only-no-resident), so this golden
// drives the op's cook fn DIRECTLY via a hand-built PointListCookCtx (inputStrings = [Path]) — the SAME
// shape the flat cook driver builds. The load-bearing path (objParseFromFile → edge dedup → triple emit)
// runs verbatim.
//
// HERMETICITY: the golden WRITES UNIQUE TEMP fixtures (temp_directory_path / sw_objedge_*_<pid>.obj) with
// KNOWN bytes, cooks against the ABSOLUTE temp path, asserts edges, then removes them — cwd/env-independent
// so --bite runs clean (no SW_ASSETS_DIR). Mirrors the selftests_loadobjaspoints discipline.
//
// EMIT-ORDER FORK: TiXL iterates a HashSet<uint> (sw: std::unordered_set) whose order is unspecified, so the
// SEQUENCE of triples may differ per build/run (fork-loadobjedges-emit-order). The golden therefore asserts
// the order-INDEPENDENT SET of (fromPos → toPos) edges + the structural invariant (every 3rd point is a
// NaN-Scale separator), NEVER a fixed sequence.
//
// EDGE MATH (single-triangle fixture, face v0=0,v1=1,v2=2):
//   InsertVertexPair swaps so `from` is the LARGER index, packs (smaller<<16)+larger:
//     (0,1)->pack=1, (1,2)->pack=65538, (2,0)->pack=2  → 3 unique edges → 9 points.
//   On emit fromIndex = pair&0xffff (LARGER), toIndex = pair>>16 (SMALLER):
//     pack 1     → from=pos[1]=( 1,0,0)  to=pos[0]=(-1,0,0)
//     pack 65538 → from=pos[2]=( 0,2,0)  to=pos[1]=( 1,0,0)
//     pack 2     → from=pos[2]=( 0,2,0)  to=pos[0]=(-1,0,0)
//   Expected edge SET {(1,0,0)->(-1,0,0), (0,2,0)->(1,0,0), (0,2,0)->(-1,0,0)} — distinct endpoints so a
//   collapse / mis-pack cannot pass.
#include <unistd.h>  // getpid (unique temp-fixture file name)

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <system_error>
#include <vector>

#include "runtime/pointlist_op_registry.h"  // PointListCookCtx / findPointListOp / pointListInjectBug / swPointDefault
#include "runtime/selftest_registry.h"      // REGISTER_SELFTESTS
#include "runtime/tixl_point.h"             // SwPoint (64B)

namespace sw {

namespace {

bool nearf(float a, float b, float t = 1e-5f) { return std::fabs(a - b) < t; }

// Single triangle: 3 distinct positions, one face → 3 unique edges (see EDGE MATH above).
const char* const kTriObj =
    "v -1 0 0\nv 1 0 0\nv 0 2 0\nvn 0 0 1\nf 1//1 2//1 3//1\n";
// A QUAD (cube_tri.obj bytes): 4 positions, one quad face → triangulated to (0,1,2)+(0,2,3) → edges
// (0,1)(1,2)(2,0)(0,2)(2,3)(3,0) → 5 unique after dedup ((2,0)==(0,2)) → 15 points.
const char* const kQuadObj =
    "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvn 0 0 1\nf 1//1 2//1 3//1 4//1\n";
// Out-of-range face index (PART A bounds guard): only 3 positions, but face refs vertex 999 → the parse
// load-gate must FAIL (TiXL: IndexOutOfRange → catch → false) → LoadObjEdges emits an EMPTY list.
const char* const kBadObj =
    "v 0 0 0\nv 1 0 0\nv 0 1 0\nvn 0 0 1\nf 1//1 999//1 3//1\n";

std::vector<SwPoint> cookLoadObjEdges(const std::string& path, bool injectBug) {
  const PointListCookFn* fn = findPointListOp("LoadObjEdges");
  if (!fn || !*fn) return {};  // not registered → empty → loud failure below
  std::vector<std::string> inputs{path};  // inputStrings[0] = Path
  std::vector<SwPoint> out;
  PointListCookCtx ctx{};
  ctx.inputStrings = &inputs;
  ctx.output = &out;
  pointListInjectBug() = injectBug;
  (*fn)(ctx);
  pointListInjectBug() = false;
  return out;
}

std::filesystem::path writeTempObj(const char* content, const char* stem) {
  std::error_code ec;
  std::filesystem::path base = std::filesystem::temp_directory_path(ec);
  if (ec) return {};
  std::filesystem::path file = base / (std::string(stem) + "_" + std::to_string(::getpid()) + ".obj");
  std::ofstream o(file, std::ios::binary);
  if (!o.is_open()) return {};
  o << content;
  o.close();
  return file;
}

// True if `got` triples form exactly the expected edge SET (order-independent) AND every 3rd point is a
// NaN-Scale separator with from/to carrying TiXL's new-Point defaults (F1=1, Rotation=Identity).
struct Edge { float fx, fy, fz, tx, ty, tz; };
bool matchEdgeSet(const std::vector<SwPoint>& got, std::vector<Edge> want) {
  if (got.size() != want.size() * 3) return false;
  for (size_t e = 0; e < got.size() / 3; ++e) {
    const SwPoint& a = got[e * 3 + 0];      // from
    const SwPoint& b = got[e * 3 + 1];      // to
    const SwPoint& s = got[e * 3 + 2];      // separator
    // structural: from/to are real points (Scale finite, F1=1, Rotation=Identity); sep has NaN scale.
    if (!std::isfinite(a.Scale.x) || !std::isfinite(b.Scale.x)) return false;  // from/to scale finite (not sep)
    if (!nearf(a.FX1, 1.0f) || !nearf(b.FX1, 1.0f)) return false;
    if (!nearf(a.Rotation.w, 1.0f) || !nearf(a.Rotation.x, 0.0f)) return false;  // Orientation=Identity
    if (!std::isnan(s.Scale.x) || !std::isnan(s.Scale.y) || !std::isnan(s.Scale.z)) return false;  // separator
    // match this (from,to) against an unconsumed want entry.
    bool found = false;
    for (size_t w = 0; w < want.size(); ++w) {
      Edge& e0 = want[w];
      if (e0.fx > 1e30f) continue;  // already consumed (sentinel)
      if (nearf(a.Position.x, e0.fx) && nearf(a.Position.y, e0.fy) && nearf(a.Position.z, e0.fz) &&
          nearf(b.Position.x, e0.tx) && nearf(b.Position.y, e0.ty) && nearf(b.Position.z, e0.tz)) {
        e0.fx = 1e31f;  // consume
        found = true;
        break;
      }
    }
    if (!found) return false;
  }
  return true;
}

int runLoadObjEdgesSelftestImpl(bool injectBug) {
  bool ok = true;

  std::filesystem::path triFile = writeTempObj(kTriObj, "sw_objedge_tri");
  std::filesystem::path quadFile = writeTempObj(kQuadObj, "sw_objedge_quad");
  std::filesystem::path badFile = writeTempObj(kBadObj, "sw_objedge_bad");

  // ===== LEG 1 — single triangle: 3 unique edges → 9 points, exact edge SET (order-independent). =====
  {
    std::vector<SwPoint> got = cookLoadObjEdges(triFile.string(), injectBug);
    std::vector<Edge> want = {
        {1, 0, 0, -1, 0, 0},   // pack 1
        {0, 2, 0, 1, 0, 0},    // pack 65538
        {0, 2, 0, -1, 0, 0},   // pack 2
    };
    bool pass = matchEdgeSet(got, want);
    ok = ok && pass;
    std::printf("[selftest-loadobjedges] LEG1 triangle n=%zu want=9 (3 edges, set-match+separators) -> %s\n",
                got.size(), pass ? "PASS" : "FAIL");
  }

  // ===== LEG 2 — quad (triangulated): 5 unique edges → 15 points (count-only; dedup of (2,0)==(0,2)). ===
  {
    std::vector<SwPoint> got = cookLoadObjEdges(quadFile.string(), injectBug);
    // 5 unique edges → 15 points; every 3rd point a NaN-Scale separator. (Bug leg → 0, fails the count.)
    bool pass = got.size() == 15;
    if (pass) {
      for (size_t e = 0; e < got.size() / 3; ++e) {
        const SwPoint& s = got[e * 3 + 2];
        pass = pass && std::isnan(s.Scale.x);  // every 3rd point is a separator
      }
    }
    ok = ok && pass;
    std::printf("[selftest-loadobjedges] LEG2 quad n=%zu want=15 (5 edges, separators) -> %s\n",
                got.size(), pass ? "PASS" : "FAIL");
  }

  // ===== LEG 3 — PART A bounds guard: out-of-range face index → parse load FAILS → EMPTY list. =====
  // (Hermetic, NOT bug-dependent: proves the obj_parse bounds guard rejects f 1 999 3 → no mesh → empty.)
  {
    std::vector<SwPoint> got = cookLoadObjEdges(badFile.string(), /*injectBug=*/false);
    bool pass = got.empty();
    ok = ok && pass;
    std::printf("[selftest-loadobjedges] LEG3 out-of-range face idx n=%zu want=0 (load fails) -> %s\n",
                got.size(), pass ? "PASS" : "FAIL");
  }

  // ===== LEG 4 — empty path → empty list (hermetic, not bug-dependent). =====
  {
    std::vector<SwPoint> got = cookLoadObjEdges("", /*injectBug=*/false);
    bool pass = got.empty();
    ok = ok && pass;
    std::printf("[selftest-loadobjedges] LEG4 empty path n=%zu want=0 -> %s\n", got.size(),
                pass ? "PASS" : "FAIL");
  }

  std::error_code ec;
  if (!triFile.empty()) std::filesystem::remove(triFile, ec);
  if (!quadFile.empty()) std::filesystem::remove(quadFile, ec);
  if (!badFile.empty()) std::filesystem::remove(badFile, ec);

  std::printf("[selftest-loadobjedges] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace

// Self-register as --selftest-loadobjedges / -bug. orderBase 361 = the slot right after loadobjaspoints (360).
// NO shared-file edit — this manifest is globbed via src/selftests*.cpp (app/CMakeLists.txt SW_SELFTEST_SRCS).
REGISTER_SELFTESTS(/*orderBase=*/361, {"loadobjedges", runLoadObjEdgesSelftestImpl});

}  // namespace sw
