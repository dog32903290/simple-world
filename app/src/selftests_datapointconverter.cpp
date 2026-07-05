// selftests_datapointconverter — DataPointConverter golden (--selftest-datapointconverter / -bug).
// HERMETIC (temp-file fixtures, absolute paths, removed after).
//
// EXPECTED VALUES — hand-derived from DataPointConverter.cs (independent-of-impl):
//   ParseCsvRecord (.cs:292-328): "Rotation W" column present → raw quaternion; ABSENT →
//     Quaternion.CreateFromYawPitchRoll(rotY, rotX, rotZ). The golden recomputes the reference
//     System.Numerics half-angle formula ON THE CPU from the authored euler values (cited in the leaf)
//     — NOT from sw's output. Scale defaults 1, position/F1 default 0; struct-init zeros for Color/F2.
//   GetColumnMapping (.cs:360-376): custom Csv*Mapping renames the column looked up (LEG3).
//   ParseJsonRecord (.cs:253-290): FLAT records keyed by the mapping values (LEG4).
//   SaveToCsv (.cs:233-251): euler-angle export via ToEulerAngles (.cs:378-396). NOTE (verified
//     numerically): TiXL's OWN export→import round-trip is LOSSY — ToEulerAngles' composition
//     convention does not invert CreateFromYawPitchRoll's — so round-trip identity is NOT the oracle.
//     LEG5 instead asserts the exported Rotation X/Y/Z columns EQUAL the .cs ToEulerAngles formula
//     applied to the converted quaternion (recomputed here as an independent reference), and the
//     position/F1/scale columns round-trip exactly.
//
// LEGS run under -bug: 1 & 2 (injectBug clears the REAL cook output → count asserts fail → RED).
#include <unistd.h>  // getpid

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <system_error>
#include <vector>

#include "runtime/pointlist_op_registry.h"  // PointListCookCtx / findPointListOp / pointListInjectBug
#include "runtime/selftest_registry.h"      // REGISTER_SELFTESTS
#include "runtime/tixl_point.h"             // SwPoint

namespace sw {
namespace {

bool nearf(float a, float b, float t = 1e-5f) { return std::fabs(a - b) < t; }

// Reference System.Numerics Quaternion.CreateFromYawPitchRoll — the SAME published formula the leaf
// cites; computed here from the AUTHORED euler values (the fixture bytes), not from sw output.
void refYawPitchRoll(float yaw, float pitch, float roll, float q[4]) {
  float sr = std::sin(roll * 0.5f), cr = std::cos(roll * 0.5f);
  float sp = std::sin(pitch * 0.5f), cp = std::cos(pitch * 0.5f);
  float sy = std::sin(yaw * 0.5f), cy = std::cos(yaw * 0.5f);
  q[0] = cy * sp * cr + sy * cp * sr;
  q[1] = sy * cp * cr - cy * sp * sr;
  q[2] = cy * cp * sr - sy * sp * cr;
  q[3] = cy * cp * cr + sy * sp * sr;
}

// Fixtures. LEG1: quaternion CSV (Rotation W present) + suffix-stripped value + missing Scale → 1.
const char* const kQuatCsv =
    "Position X,Position Y,Position Z,Rotation X,Rotation Y,Rotation Z,Rotation W,F1\n"
    "1.5,-2,0.25,0.1,0.2,0.3,0.4,7\n"
    "3m,0,0,0,0,0,1,0\n";
// LEG2: euler CSV (NO Rotation W column) → YawPitchRoll path. Angles in RADIANS (the .cs never
// converts degrees). rotX=0.3 rotY=-0.4 rotZ=0.7.
const char* const kEulerCsv =
    "Position X,Position Y,Position Z,Rotation X,Rotation Y,Rotation Z,F1,Scale X,Scale Y,Scale Z\n"
    "10,20,30,0.3,-0.4,0.7,5,2,2,2\n";
// LEG3: custom mapping — the position-X column is named "px".
const char* const kMappedCsv = "px,Position Y\n42,43\n";
// LEG4: flat JSON records keyed by the DEFAULT mapping values; r1 has a quaternion (Rotation W
// present), r2 euler-only (falls through YawPitchRoll with rotY=0.5 only).
const char* const kFlatJson =
    "[\n"
    "  { \"Position X\": 1, \"Position Y\": 2, \"Position Z\": 3,\n"
    "    \"Rotation X\": 0.1, \"Rotation Y\": 0.2, \"Rotation Z\": 0.3, \"Rotation W\": 0.4, \"F1\": 9 },\n"
    "  { \"Position X\": -1, \"Rotation Y\": 0.5 }\n"
    "]\n";

std::filesystem::path writeTemp(const char* content, const char* stem, const char* ext) {
  std::error_code ec;
  std::filesystem::path base = std::filesystem::temp_directory_path(ec);
  if (ec) return {};
  std::filesystem::path file =
      base / (std::string(stem) + "_" + std::to_string(::getpid()) + ext);
  std::ofstream o(file, std::ios::binary);
  if (!o.is_open()) return {};
  o << content;
  return file;
}

// Drive the cook fn directly (flat-cook shape). strs = [FilePath, ExportFilePath, 11 mappings].
std::vector<SwPoint> cookOp(const std::string& filePath, const std::string& exportPath, float exportOn,
                            const std::vector<std::pair<int, std::string>>& mappingOverrides,
                            bool injectBug) {
  const PointListCookFn* fn = findPointListOp("DataPointConverter");
  if (!fn || !*fn) return {};
  static const char* kDefaults[11] = {"Position X", "Position Y", "Position Z", "Rotation X",
                                      "Rotation Y", "Rotation Z", "Rotation W", "Scale X",
                                      "Scale Y",    "Scale Z",    "F1"};
  std::vector<std::string> strs{filePath, exportPath};
  for (int m = 0; m < 11; ++m) strs.push_back(kDefaults[m]);
  for (const auto& ov : mappingOverrides) strs[(size_t)(2 + ov.first)] = ov.second;
  std::map<std::string, float> params{{"Export", exportOn}};
  std::vector<SwPoint> out;
  PointListCookCtx ctx{};
  ctx.inputStrings = &strs;
  ctx.params = &params;
  ctx.output = &out;
  pointListInjectBug() = injectBug;
  (*fn)(ctx);
  pointListInjectBug() = false;
  return out;
}

int runDataPointConverterSelftestImpl(bool injectBug) {
  bool ok = true;
  std::filesystem::path quatCsv = writeTemp(kQuatCsv, "sw_dpc_quat", ".csv");
  std::filesystem::path eulerCsv = writeTemp(kEulerCsv, "sw_dpc_euler", ".csv");
  std::filesystem::path mappedCsv = writeTemp(kMappedCsv, "sw_dpc_mapped", ".csv");
  std::filesystem::path flatJson = writeTemp(kFlatJson, "sw_dpc_flat", ".json");
  std::filesystem::path exportCsv;
  {
    std::error_code ec;
    exportCsv = std::filesystem::temp_directory_path(ec) /
                ("sw_dpc_export_" + std::to_string(::getpid()) + ".csv");
  }

  // ===== LEG 1 — quaternion CSV (Rotation W present → raw quaternion; 'm' suffix; Scale default 1) ====
  {
    std::vector<SwPoint> got = cookOp(quatCsv.string(), "", 0.0f, {}, injectBug);
    bool pass = got.size() == 2;
    if (pass) {
      const SwPoint& a = got[0];
      pass = pass && nearf(a.Position.x, 1.5f) && nearf(a.Position.y, -2.0f) &&
             nearf(a.Position.z, 0.25f) && nearf(a.Rotation.x, 0.1f) && nearf(a.Rotation.y, 0.2f) &&
             nearf(a.Rotation.z, 0.3f) && nearf(a.Rotation.w, 0.4f) && nearf(a.FX1, 7.0f) &&
             nearf(a.Scale.x, 1.0f) && nearf(a.Scale.y, 1.0f) && nearf(a.Scale.z, 1.0f) &&  // no cols → 1
             nearf(a.Color.x, 0.0f) && nearf(a.FX2, 0.0f);  // struct-init zeros
      pass = pass && nearf(got[1].Position.x, 3.0f);        // "3m" → 3 (suffix strip)
    }
    ok = ok && pass;
    std::printf("[selftest-datapointconverter] LEG1 quat-csv n=%zu want=2 -> %s\n", got.size(),
                pass ? "PASS" : "FAIL");
  }

  // ===== LEG 2 — euler CSV (no Rotation W) → CreateFromYawPitchRoll(rotY=-0.4, rotX=0.3, rotZ=0.7) ====
  {
    std::vector<SwPoint> got = cookOp(eulerCsv.string(), "", 0.0f, {}, injectBug);
    float q[4];
    refYawPitchRoll(-0.4f, 0.3f, 0.7f, q);  // (yaw=rotY, pitch=rotX, roll=rotZ) — .cs:313 arg order
    bool pass = got.size() == 1;
    if (pass) {
      const SwPoint& p = got[0];
      pass = pass && nearf(p.Position.x, 10.0f) && nearf(p.Position.y, 20.0f) &&
             nearf(p.Position.z, 30.0f) && nearf(p.Rotation.x, q[0]) && nearf(p.Rotation.y, q[1]) &&
             nearf(p.Rotation.z, q[2]) && nearf(p.Rotation.w, q[3]) && nearf(p.FX1, 5.0f) &&
             nearf(p.Scale.x, 2.0f);
    }
    ok = ok && pass;
    std::printf("[selftest-datapointconverter] LEG2 euler-csv q=(%.4f,%.4f,%.4f,%.4f) -> %s\n",
                got.empty() ? 0.f : got[0].Rotation.x, got.empty() ? 0.f : got[0].Rotation.y,
                got.empty() ? 0.f : got[0].Rotation.z, got.empty() ? 0.f : got[0].Rotation.w,
                pass ? "PASS" : "FAIL");
  }

  // ===== LEG 3 — custom mapping (CsvPosXMapping="px") =====
  if (!injectBug) {
    std::vector<SwPoint> got = cookOp(mappedCsv.string(), "", 0.0f, {{0, "px"}}, false);
    bool pass = got.size() == 1 && nearf(got[0].Position.x, 42.0f) && nearf(got[0].Position.y, 43.0f);
    ok = ok && pass;
    std::printf("[selftest-datapointconverter] LEG3 custom-mapping -> %s\n", pass ? "PASS" : "FAIL");
  }

  // ===== LEG 4 — flat JSON with mapped keys (quaternion + defaults-with-euler records) =====
  if (!injectBug) {
    std::vector<SwPoint> got = cookOp(flatJson.string(), "", 0.0f, {}, false);
    float q2[4];
    refYawPitchRoll(0.5f, 0.0f, 0.0f, q2);  // r2: only Rotation Y=0.5 → yaw-only quaternion
    bool pass = got.size() == 2;
    if (pass) {
      pass = pass && nearf(got[0].Position.x, 1.0f) && nearf(got[0].Rotation.x, 0.1f) &&
             nearf(got[0].Rotation.w, 0.4f) && nearf(got[0].FX1, 9.0f) &&
             nearf(got[1].Position.x, -1.0f) && nearf(got[1].Rotation.y, q2[1]) &&
             nearf(got[1].Rotation.w, q2[3]) && nearf(got[1].Scale.x, 1.0f);
    }
    ok = ok && pass;
    std::printf("[selftest-datapointconverter] LEG4 flat-json n=%zu want=2 -> %s\n", got.size(),
                pass ? "PASS" : "FAIL");
  }

  // ===== LEG 5 — CSV export: exported euler == .cs ToEulerAngles(q) reference; pos/F1/scale exact ====
  if (!injectBug) {
    std::vector<SwPoint> first = cookOp(eulerCsv.string(), exportCsv.string(), 1.0f, {}, false);
    std::vector<SwPoint> second = cookOp(exportCsv.string(), "", 0.0f, {}, false);
    // Independent euler reference: the .cs:378-396 formula on the LEG2-verified quaternion.
    float q[4];
    refYawPitchRoll(-0.4f, 0.3f, 0.7f, q);
    double x = q[0], y = q[1], z = q[2], w = q[3];
    float refE[3];
    refE[0] = (float)std::atan2(2.0 * (w * x + y * z), 1.0 - 2.0 * (x * x + y * y));
    double sinp = 2.0 * (w * y - z * x);
    refE[1] = std::fabs(sinp) >= 1.0 ? (float)std::copysign(M_PI / 2.0, sinp) : (float)std::asin(sinp);
    refE[2] = (float)std::atan2(2.0 * (w * z + x * y), 1.0 - 2.0 * (y * y + z * z));
    // The re-converted point's quaternion must equal YawPitchRoll(refE[1], refE[0], refE[2]) — i.e. the
    // exported file carried EXACTLY the reference euler (TiXL's lossy convention reproduced, not "fixed").
    float qRe[4];
    refYawPitchRoll(refE[1], refE[0], refE[2], qRe);
    bool pass = first.size() == 1 && second.size() == 1;
    if (pass) {
      pass = pass && nearf(second[0].Rotation.x, qRe[0], 1e-4f) &&
             nearf(second[0].Rotation.y, qRe[1], 1e-4f) && nearf(second[0].Rotation.z, qRe[2], 1e-4f) &&
             nearf(second[0].Rotation.w, qRe[3], 1e-4f) &&
             nearf(first[0].Position.x, second[0].Position.x) && nearf(first[0].FX1, second[0].FX1) &&
             nearf(first[0].Scale.x, second[0].Scale.x);
    }
    ok = ok && pass;
    std::printf("[selftest-datapointconverter] LEG5 csv-export-euler-ref -> %s\n", pass ? "PASS" : "FAIL");
  }

  // cleanup
  std::error_code ec;
  for (const auto& f : {quatCsv, eulerCsv, mappedCsv, flatJson, exportCsv})
    if (!f.empty()) std::filesystem::remove(f, ec);

  if (injectBug) {
    if (ok) {
      std::printf("[selftest-datapointconverter] FAIL: injectBug did not trip any probe (tooth has no "
                  "bite)\n");
      return 0;  // did-not-trip → return 0 (NO-BITE list catches a dead tooth — GOLDEN_STANDARD P1)
    }
    std::printf("[selftest-datapointconverter] injectBug correctly RED\n");
    return 1;
  }
  std::printf("[selftest-datapointconverter] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace

REGISTER_SELFTESTS(/*orderBase=*/363, {"datapointconverter", runDataPointConverterSelftestImpl});

}  // namespace sw
