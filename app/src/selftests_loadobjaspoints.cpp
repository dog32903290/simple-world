// selftests_loadobjaspoints — LoadObjAsPoints golden (--selftest-loadobjaspoints / -bug). HERMETIC, byte-exact.
//
// LoadObjAsPoints (runtime/pointlist_ops_loadobjaspoints.cpp) loads a .OBJ and emits its vertices as a host
// SwPoint list (the FIRST consumer of the obj_parse seam + the pointlist String-path channel). The pointlist
// String channel is FLAT-cook only (fork-pointlist-flat-only-no-resident), so this golden drives the op's
// cook fn DIRECTLY via a hand-built PointListCookCtx — the SAME shape the flat cook driver builds (inputStrings
// = [Path], params = {Mode, Sorting}). The load-bearing path (objParseFromFile → faithful field mapping) runs
// verbatim.
//
// HERMETICITY: file I/O depends on what file exists where. To stay cwd/env-independent (--bite runs clean with
// no SW_ASSETS_DIR), the golden WRITES UNIQUE TEMP fixtures (temp_directory_path / sw_obj_*_<pid>.obj) with the
// SAME KNOWN bytes that live in the committed assets/meshes/cube_tri.obj + colored_tri.obj, cooks against the
// ABSOLUTE temp path, asserts the exact point values, then removes them. The committed assets are for manual /
// in-app exercise; the golden does NOT depend on their resolution (mirrors the ReadFile golden discipline).
//
// COMMITTED FIXTURES (author-verified byte-exact via od -c):
//   cube_tri.obj    (63B): "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvn 0 0 1\nf 1//1 2//1 3//1 4//1\n"
//                          → 4 positions, NO colors, a QUAD face (bites triangulation + 1-based + v//n).
//   colored_tri.obj (87B): "v -1 0 0 0.1 0.2 0.3\nv 1 0 0 0.4 0.5 0.6\nv 0 2 0 0.7 0.8 0.9\nvn 0 0 1\nf 1//1 2//1 3//1\n"
//                          → 3 positions WITH 7-token vertex colors (bites the color path + Quaternion(color)).
//
// GREEN legs (Mode=Vertices_WithColor=2, Sorting=Ignore=6):
//   • cube_tri    → EXACTLY 4 points, positions == the 4 v-lines (file order), Color=(1,1,1,1),
//                   Rotation=(1,1,1,1), FX1=1 (no vertex color → Vector4.One; Orientation=Quaternion(1,1,1,1)).
//   • colored_tri → EXACTLY 3 points, per-vertex Color=(r,g,b,1), Rotation=Quaternion(r,g,b,1)==Color, FX1=1,
//                   Position == the v-lines. DISTINCT r,g,b per vertex so a collapse cannot pass.
// BUG leg (-bug): pointListInjectBug() makes the REAL cook CLEAR its output → 0 points → every count/value
//   assertion fails → return 1. No expected-value inversion; the actual cook path is bitten.
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

// The canonical fixture bytes — IDENTICAL to the committed assets/meshes/*.obj (author-verified od -c).
const char* const kCubeObj =
    "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvn 0 0 1\nf 1//1 2//1 3//1 4//1\n";
const char* const kColoredObj =
    "v -1 0 0 0.1 0.2 0.3\nv 1 0 0 0.4 0.5 0.6\nv 0 2 0 0.7 0.8 0.9\nvn 0 0 1\nf 1//1 2//1 3//1\n";

// Drive LoadObjAsPoints' cook fn directly through a hand-built PointListCookCtx (the flat cook-driver shape).
// inputStrings = [path]; params = {Mode, Sorting}. injectBug toggles the leaf's REAL-output corruption hook.
std::vector<SwPoint> cookLoadObj(const std::string& path, float mode, float sorting, bool injectBug) {
  const PointListCookFn* fn = findPointListOp("LoadObjAsPoints");
  if (!fn || !*fn) return {};  // not registered → empty → loud failure below

  std::vector<std::string> inputs{path};  // inputStrings[0] = Path
  std::map<std::string, float> params{{"Mode", mode}, {"Sorting", sorting}};
  std::vector<SwPoint> out;
  PointListCookCtx ctx{};
  ctx.inputStrings = &inputs;
  ctx.params = &params;
  ctx.output = &out;

  pointListInjectBug() = injectBug;
  (*fn)(ctx);
  pointListInjectBug() = false;
  return out;
}

// Write `content` to a unique temp .obj, returning its absolute path ("" on failure).
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

int runLoadObjAsPointsSelftestImpl(bool injectBug) {
  bool ok = true;
  const float kVerticesWithColor = 2.0f, kIgnore = 6.0f;

  std::filesystem::path cubeFile = writeTempObj(kCubeObj, "sw_obj_cube");
  std::filesystem::path colorFile = writeTempObj(kColoredObj, "sw_obj_color");

  // ===== LEG 1 — cube_tri (no color): 4 points, file-order positions, Color/Rotation=(1,1,1,1), FX1=1. =====
  {
    std::vector<SwPoint> got = cookLoadObj(cubeFile.string(), kVerticesWithColor, kIgnore, injectBug);
    // Expected 4 v-lines in file order (Sorting=Ignore): (0,0,0)(1,0,0)(1,1,0)(0,1,0).
    const float expPos[4][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
    bool pass = got.size() == 4;
    if (pass) {
      for (int i = 0; i < 4; ++i) {
        const SwPoint& p = got[(size_t)i];
        pass = pass && nearf(p.Position.x, expPos[i][0]) && nearf(p.Position.y, expPos[i][1]) &&
               nearf(p.Position.z, expPos[i][2]) && nearf(p.FX1, 1.0f) &&
               nearf(p.Color.x, 1.0f) && nearf(p.Color.y, 1.0f) && nearf(p.Color.z, 1.0f) &&
               nearf(p.Color.w, 1.0f) &&  // no vertex color → Vector4.One
               nearf(p.Rotation.x, 1.0f) && nearf(p.Rotation.y, 1.0f) && nearf(p.Rotation.z, 1.0f) &&
               nearf(p.Rotation.w, 1.0f);  // Orientation = Quaternion(1,1,1,1)
      }
    }
    ok = ok && pass;
    std::printf("[selftest-loadobjaspoints] LEG1 cube_tri n=%zu want=4 Color/Rot=(1,1,1,1) -> %s\n",
                got.size(), pass ? "PASS" : "FAIL");
  }

  // ===== LEG 2 — colored_tri (7-token colors): 3 points, per-vertex Color + Rotation=Quaternion(color). ====
  {
    std::vector<SwPoint> got = cookLoadObj(colorFile.string(), kVerticesWithColor, kIgnore, injectBug);
    // Expected 3 v-lines with colors (file order): pos / (r,g,b,1).
    const float expPos[3][3] = {{-1, 0, 0}, {1, 0, 0}, {0, 2, 0}};
    const float expCol[3][4] = {{0.1f, 0.2f, 0.3f, 1.0f}, {0.4f, 0.5f, 0.6f, 1.0f}, {0.7f, 0.8f, 0.9f, 1.0f}};
    bool pass = got.size() == 3;
    if (pass) {
      for (int i = 0; i < 3; ++i) {
        const SwPoint& p = got[(size_t)i];
        pass = pass && nearf(p.Position.x, expPos[i][0]) && nearf(p.Position.y, expPos[i][1]) &&
               nearf(p.Position.z, expPos[i][2]) && nearf(p.FX1, 1.0f) &&
               nearf(p.Color.x, expCol[i][0]) && nearf(p.Color.y, expCol[i][1]) &&
               nearf(p.Color.z, expCol[i][2]) && nearf(p.Color.w, expCol[i][3]) &&
               // Orientation = Quaternion(c.X,c.Y,c.Z,c.W) == the color (color-as-rotation quirk)
               nearf(p.Rotation.x, expCol[i][0]) && nearf(p.Rotation.y, expCol[i][1]) &&
               nearf(p.Rotation.z, expCol[i][2]) && nearf(p.Rotation.w, expCol[i][3]);
      }
    }
    ok = ok && pass;
    std::printf("[selftest-loadobjaspoints] LEG2 colored_tri n=%zu want=3 Rot==Color(per-vertex) -> %s\n",
                got.size(), pass ? "PASS" : "FAIL");
  }

  // ===== LEG 3 — empty path → empty list (hermetic, not bug-dependent). =====
  {
    std::vector<SwPoint> got = cookLoadObj("", kVerticesWithColor, kIgnore, /*injectBug=*/false);
    bool pass = got.empty();
    ok = ok && pass;
    std::printf("[selftest-loadobjaspoints] LEG3 empty path n=%zu want=0 -> %s\n", got.size(),
                pass ? "PASS" : "FAIL");
  }

  // cleanup temp fixtures
  std::error_code ec;
  if (!cubeFile.empty()) std::filesystem::remove(cubeFile, ec);
  if (!colorFile.empty()) std::filesystem::remove(colorFile, ec);

  std::printf("[selftest-loadobjaspoints] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace

// Self-register as --selftest-loadobjaspoints / -bug. orderBase 360 = a free slot (the prior block list
// shows 350 then 500; 360 sorts deterministically into --selftest-list). NO shared-file edit — this manifest
// is globbed via src/selftests*.cpp (app/CMakeLists.txt SW_SELFTEST_SRCS).
REGISTER_SELFTESTS(/*orderBase=*/360, {"loadobjaspoints", runLoadObjAsPointsSelftestImpl});

}  // namespace sw
