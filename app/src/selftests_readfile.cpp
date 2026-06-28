// selftests_readfile — ReadFile golden (--selftest-readfile / -bug). HERMETIC, byte-exact.
//
// ReadFile (runtime/string_ops_readfile.cpp) reads a TEXT file at a path string and emits its WHOLE
// contents as a host string. It rides the EXISTING String cook flow (a single String input FilePath →
// single String output Result); the String rail is FLAT-cook (fork-readfile-flat-only-no-resident,
// string_op_registry.h:24 — it does NOT enter resident_eval_graph), so this golden drives the op's cook
// DIRECTLY via a hand-built StringCookCtx (the same shape the cook driver hands a string leaf — the SAME
// pattern the BlendStrings golden uses). The load-bearing computation (whole-file read) runs verbatim.
//
// HERMETICITY: file I/O depends on what file exists where. To stay cwd/env-independent (so --bite runs
// clean with no SW_ASSETS_DIR), the golden (a) writes a UNIQUE TEMP fixture
// (std::filesystem::temp_directory_path / sw_readfile_fixture_<pid>.txt) with the SAME KNOWN bytes that
// live in the committed asset assets/text/readfile_fixture.txt — content "line one\nline two\nthird
// line\n" (29 bytes) — cooks ReadFile against its ABSOLUTE path, asserts the exact bytes, then removes it.
// A committed fixture also lives at assets/text/readfile_fixture.txt (byte-identical content) for manual /
// in-app exercise; the golden does NOT depend on its resolution (cwd-free, mirroring the FilesInFolder
// golden's discipline).
//
// GREEN legs:
//   • read the temp fixture        → exact bytes "line one\nline two\nthird line\n"
//   • empty path                   → "" (ReadFile.cs: empty/null path → null → our "")
//   • non-existent path            → "" (open failure → TryLoad false → null → "")
// BUG leg (-bug): stringInjectBug() makes the REAL cook drop the last byte of the read contents → mismatch
//   (the 29-byte fixture reads back 28 bytes) → FAIL. No expected-value inversion; the actual read path is
//   bitten. The empty/non-existent legs emit "" (untouched by the tooth) so they are not the biting legs.
#include <unistd.h>  // getpid (unique temp-fixture file name)

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "runtime/selftest_registry.h"   // REGISTER_SELFTESTS
#include "runtime/string_op_registry.h"  // StringCookCtx / findStringOp / stringInjectBug

namespace sw {

namespace {

// The canonical fixture content — IDENTICAL to the committed assets/text/readfile_fixture.txt bytes.
// (Author-time verified: 29 bytes, od -c shows "line one\nline two\nthird line\n".)
const char* const kFixtureContent = "line one\nline two\nthird line\n";

// Invoke the ReadFile cook fn directly through a hand-built StringCookCtx (the cook-driver shape). inputs =
// [FilePath]. injectBug toggles the leaf's REAL-output corruption hook around the cook.
std::string cookReadFile(const std::string& filePath, bool injectBug) {
  const StringCookFn* fn = findStringOp("ReadFile");
  if (!fn || !*fn) return {"ERR:NO_FN"};  // not registered → loud green-assert failure

  std::vector<std::string> inputs{filePath};  // inputStrings[0] = FilePath
  std::string out;
  StringCookCtx ctx{};
  ctx.inputStrings = &inputs;
  ctx.output = &out;

  stringInjectBug() = injectBug;
  (*fn)(ctx);
  stringInjectBug() = false;
  return out;
}

int runReadFileSelftestImpl(bool injectBug) {
  // --- build a unique temp fixture: <tmp>/sw_readfile_fixture_<pid>.txt with the canonical bytes ---
  std::error_code ec;
  std::filesystem::path base = std::filesystem::temp_directory_path(ec);
  std::filesystem::path file = base / ("sw_readfile_fixture_" + std::to_string(::getpid()) + ".txt");
  std::filesystem::remove(file, ec);  // clean any stale leftover
  {
    std::FILE* f = std::fopen(file.string().c_str(), "wb");
    if (f) { std::fputs(kFixtureContent, f); std::fclose(f); }
  }
  const std::string fixturePath = file.string();
  const std::string want = kFixtureContent;

  bool ok = true;

  // LEG 1: read the fixture → exact whole-file bytes.
  {
    std::string got = cookReadFile(fixturePath, injectBug);
    bool pass = (got == want);
    ok = ok && pass;
    std::printf("[selftest-readfile] LEG1 read fixture got=\"%s\" (%zuB) want=\"%s\" (%zuB) -> %s\n",
                got.c_str(), got.size(), want.c_str(), want.size(), pass ? "PASS" : "FAIL");
  }

  // LEG 2: empty path → "" (empty/null path → null → ""). injectBug no-op on "" (tooth bites non-empty).
  {
    std::string got = cookReadFile("", injectBug);
    bool pass = got.empty();
    ok = ok && pass;
    std::printf("[selftest-readfile] LEG2 empty path got count=%zu want 0 -> %s\n",
                got.size(), pass ? "PASS" : "FAIL");
  }

  // LEG 3: non-existent path → "" (open failure → null → ""). injectBug no-op (empty output untouched).
  {
    std::string got = cookReadFile((base / "sw_readfile_does_not_exist_xyz.txt").string(), injectBug);
    bool pass = got.empty();
    ok = ok && pass;
    std::printf("[selftest-readfile] LEG3 non-existent path got count=%zu want 0 -> %s\n",
                got.size(), pass ? "PASS" : "FAIL");
  }

  // cleanup the temp fixture
  std::filesystem::remove(file, ec);

  std::printf("[selftest-readfile] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace

// Self-register as --selftest-readfile for isolated runs. orderBase 269 = a free slot after the
// string-rail family (wrapstring=260, filesinfolder=268); the row sorts deterministically into
// --selftest-list (see selftest_registry.h ORDER note). NO shared-file edit — this manifest is globbed
// via src/selftests*.cpp (app/CMakeLists.txt SW_SELFTEST_SRCS).
REGISTER_SELFTESTS(/*orderBase=*/269, {"readfile", runReadFileSelftestImpl});

}  // namespace sw
