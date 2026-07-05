// selftests_datapointimportexport — DataPointImportExport golden (--selftest-datapointimportexport / -bug).
// HERMETIC (temp-file fixtures, absolute paths, removed after — the LoadObjAsPoints golden discipline).
//
// The pointlist String channel is FLAT-cook only, so the golden drives the op's cook fn DIRECTLY via a
// hand-built PointListCookCtx (inputStrings = [ImportFilePath, ExportFilePath], params = {Export}).
//
// EXPECTED VALUES — hand-derived from DataPointImportExport.cs (independent-of-impl):
//   ParseRecord (.cs:281-309): Position def 0 / Orientation def (0,0,0,W=1) / Scale def 1 / F1 def 0;
//   `new Point{...}` C# struct-init → Color=(0,0,0,0), F2=0 on every imported point.
//   ParseFloatString (.cs:339-346): strips 'm' and '°', trims, invariant parse.
//   Update (.cs:61): PointBufferOut = _pointBuffer ?? bufferIn → no import → FORWARD the input bag.
//   ExportFileAsync (.cs:118): the INPUT buffer wins over imported data as the export source.
//
// LEGS:
//   1 IMPORT   — authored JSON: full record / empty record (all defaults) / string-suffix record.
//                Probes off-defaults (1.5 / -2 / 0.25 / quaternion 0.1..0.4 / scale 2,3,4 / F1 7) —
//                a wrong field mapping, wrong default, or dropped suffix-strip goes RED.
//   2 FORWARD  — no import path + a real shared MTL bag of 2 known SwPoints → output == bag copy.
//   3 EXPORT   — import LEG1's file, Export=1 → written file re-imported (the importer is proven by
//                LEG1 against hand-authored bytes, so the round-trip is anchored, not A==A) → equal.
//   -bug: pointListInjectBug() clears the REAL cook output → LEG1/2 count asserts fail → RED.
#include <unistd.h>  // getpid

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <system_error>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/pointlist_op_registry.h"  // PointListCookCtx / findPointListOp / pointListInjectBug
#include "runtime/selftest_registry.h"      // REGISTER_SELFTESTS
#include "runtime/tixl_point.h"             // SwPoint

namespace sw {
namespace {

bool nearf(float a, float b, float t = 1e-5f) { return std::fabs(a - b) < t; }

// Authored fixture: 3 records exercising full values / all defaults / string+suffix parsing.
const char* const kPointsJson =
    "[\n"
    "  { \"Position\": {\"X\": 1.5, \"Y\": -2, \"Z\": 0.25},\n"
    "    \"Orientation\": {\"X\": 0.1, \"Y\": 0.2, \"Z\": 0.3, \"W\": 0.4},\n"
    "    \"Scale\": {\"X\": 2, \"Y\": 3, \"Z\": 4}, \"F1\": 7 },\n"
    "  { },\n"
    "  { \"Position\": {\"X\": \"1.25m\", \"Y\": \" -0.5 \", \"Z\": \"90\xC2\xB0\"} }\n"
    "]\n";

std::filesystem::path writeTemp(const char* content, const char* stem) {
  std::error_code ec;
  std::filesystem::path base = std::filesystem::temp_directory_path(ec);
  if (ec) return {};
  std::filesystem::path file = base / (std::string(stem) + "_" + std::to_string(::getpid()) + ".json");
  std::ofstream o(file, std::ios::binary);
  if (!o.is_open()) return {};
  o << content;
  return file;
}

std::vector<SwPoint> cookOp(const std::string& importPath, const std::string& exportPath, float exportOn,
                            const MTL::Buffer* bag, uint32_t bagCount, bool injectBug) {
  const PointListCookFn* fn = findPointListOp("DataPointImportExport");
  if (!fn || !*fn) return {};
  std::vector<std::string> strs{importPath, exportPath};
  std::map<std::string, float> params{{"Export", exportOn}};
  std::vector<SwPoint> out;
  PointListCookCtx ctx{};
  ctx.inputStrings = &strs;
  ctx.params = &params;
  ctx.output = &out;
  ctx.inputPointsBag = bag;
  ctx.inputPointsCount = bagCount;
  pointListInjectBug() = injectBug;
  (*fn)(ctx);
  pointListInjectBug() = false;
  return out;
}

int runDataPointImportExportSelftestImpl(bool injectBug) {
  bool ok = true;
  std::filesystem::path jsonFile = writeTemp(kPointsJson, "sw_dpimport");
  std::filesystem::path exportFile;
  {
    std::error_code ec;
    exportFile = std::filesystem::temp_directory_path(ec) /
                 ("sw_dpexport_" + std::to_string(::getpid()) + ".json");
  }

  // ===== LEG 1 — IMPORT (hand-authored bytes → exact ParseRecord values) =====
  {
    std::vector<SwPoint> got = cookOp(jsonFile.string(), "", 0.0f, nullptr, 0, injectBug);
    bool pass = got.size() == 3;
    if (pass) {
      const SwPoint& a = got[0];  // full record
      pass = pass && nearf(a.Position.x, 1.5f) && nearf(a.Position.y, -2.0f) &&
             nearf(a.Position.z, 0.25f) && nearf(a.Rotation.x, 0.1f) && nearf(a.Rotation.y, 0.2f) &&
             nearf(a.Rotation.z, 0.3f) && nearf(a.Rotation.w, 0.4f) && nearf(a.Scale.x, 2.0f) &&
             nearf(a.Scale.y, 3.0f) && nearf(a.Scale.z, 4.0f) && nearf(a.FX1, 7.0f) &&
             nearf(a.Color.x, 0.0f) && nearf(a.Color.w, 0.0f) && nearf(a.FX2, 0.0f);  // struct-init zeros
      const SwPoint& b = got[1];  // empty record → all defaults
      pass = pass && nearf(b.Position.x, 0.0f) && nearf(b.Position.y, 0.0f) &&
             nearf(b.Rotation.x, 0.0f) && nearf(b.Rotation.w, 1.0f) &&  // W default 1
             nearf(b.Scale.x, 1.0f) && nearf(b.Scale.y, 1.0f) && nearf(b.Scale.z, 1.0f) &&
             nearf(b.FX1, 0.0f);
      const SwPoint& s = got[2];  // string values with 'm' / '°' suffixes
      pass = pass && nearf(s.Position.x, 1.25f) && nearf(s.Position.y, -0.5f) &&
             nearf(s.Position.z, 90.0f) && nearf(s.Rotation.w, 1.0f) && nearf(s.Scale.x, 1.0f);
    }
    ok = ok && pass;
    std::printf("[selftest-datapointimportexport] LEG1 import n=%zu want=3 -> %s\n", got.size(),
                pass ? "PASS" : "FAIL");
  }

  // ===== LEG 2 — FORWARD (no import path → output == input bag) =====
  {
    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
    MTL::Device* dev = MTL::CreateSystemDefaultDevice();
    bool pass = false;
    if (dev) {
      MTL::Buffer* bag = dev->newBuffer(2 * sizeof(SwPoint), MTL::ResourceStorageModeShared);
      SwPoint* bp = (SwPoint*)bag->contents();
      bp[0] = SwPoint{};
      bp[0].Position = {5.0f, 6.0f, 7.0f};
      bp[0].FX1 = 0.5f;
      bp[1] = SwPoint{};
      bp[1].Position = {-1.0f, -2.0f, -3.0f};
      bp[1].Color = {0.25f, 0.5f, 0.75f, 1.0f};
      std::vector<SwPoint> got = cookOp("", "", 0.0f, bag, 2, injectBug);
      pass = got.size() == 2 && nearf(got[0].Position.x, 5.0f) && nearf(got[0].FX1, 0.5f) &&
             nearf(got[1].Position.z, -3.0f) && nearf(got[1].Color.y, 0.5f);
      bag->release();
      dev->release();
    }
    ok = ok && pass;
    std::printf("[selftest-datapointimportexport] LEG2 forward-bag -> %s\n", pass ? "PASS" : "FAIL");
  }

  // ===== LEG 3 — EXPORT round-trip (bug-off: the export write is not the injected tooth) =====
  if (!injectBug) {
    std::vector<SwPoint> first = cookOp(jsonFile.string(), exportFile.string(), 1.0f, nullptr, 0, false);
    std::vector<SwPoint> second = cookOp(exportFile.string(), "", 0.0f, nullptr, 0, false);
    bool pass = first.size() == 3 && second.size() == 3;
    if (pass) {
      for (size_t i = 0; i < 3; ++i) {
        pass = pass && nearf(first[i].Position.x, second[i].Position.x) &&
               nearf(first[i].Position.y, second[i].Position.y) &&
               nearf(first[i].Position.z, second[i].Position.z) &&
               nearf(first[i].Rotation.x, second[i].Rotation.x) &&
               nearf(first[i].Rotation.w, second[i].Rotation.w) &&
               nearf(first[i].Scale.x, second[i].Scale.x) && nearf(first[i].FX1, second[i].FX1);
      }
    }
    ok = ok && pass;
    std::printf("[selftest-datapointimportexport] LEG3 export-roundtrip -> %s\n", pass ? "PASS" : "FAIL");
  }

  // cleanup
  std::error_code ec;
  if (!jsonFile.empty()) std::filesystem::remove(jsonFile, ec);
  std::filesystem::remove(exportFile, ec);

  if (injectBug) {
    if (ok) {
      std::printf("[selftest-datapointimportexport] FAIL: injectBug did not trip any probe (tooth has "
                  "no bite)\n");
      return 0;  // did-not-trip → return 0 (NO-BITE list catches a dead tooth — GOLDEN_STANDARD P1)
    }
    std::printf("[selftest-datapointimportexport] injectBug correctly RED\n");
    return 1;
  }
  std::printf("[selftest-datapointimportexport] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace

REGISTER_SELFTESTS(/*orderBase=*/362, {"datapointimportexport", runDataPointImportExportSelftestImpl});

}  // namespace sw
