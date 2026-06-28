// selftests_filesinfolder — FilesInFolder golden (--selftest-filesinfolder / -bug). HERMETIC, byte-exact.
//
// FilesInFolder (runtime/stringlist_ops_filesinfolder.cpp) lists the FULL PATHS of the regular files in a
// folder, filtered by a SUBSTRING, SORTED lexicographically (sw fork — TiXL's Directory.GetFiles order is
// non-deterministic), and reports a count (= list size; the int output itself is count-deferred, asserted
// here at the op boundary). It rides the EXISTING StringList cook flow (the same channel SplitString
// produces on); the StringList rail is FLAT-cook (fork-filesinfolder-flat-only-no-resident), so this golden
// drives the op's cook DIRECTLY via a hand-built StringListCookCtx (the same shape the cook driver hands a
// stringlist leaf) — the load-bearing computation (directory listing + substring filter + sort) is in the
// leaf's cook and is exercised verbatim.
//
// HERMETICITY: the cook emits FULL paths, whose absolute prefix depends on where the test runs. To stay
// cwd-independent we (a) create a UNIQUE TEMP fixture directory (std::filesystem::temp_directory_path) with
// THREE known files — a.txt, b.txt, c.dat — populate + cook + assert, then remove it; and (b) assert on the
// BASENAMES extracted from the emitted full paths (deterministic regardless of the absolute prefix) plus the
// exact count. A committed fixture also lives at assets/folder_fixture/{a.txt,b.txt,c.dat} for manual /
// in-app exercise; the golden does not depend on its resolution (cwd-free).
//
// GREEN legs (against the temp fixture):
//   • Filter ""      → sorted basenames [a.txt, b.txt, c.dat], count 3   (empty filter keeps all)
//   • Filter ".txt"  → sorted basenames [a.txt, b.txt],        count 2   (substring select)
//   • non-existent folder → empty list, count 0
// BUG leg (-bug): stringListInjectBug() makes the REAL cook drop its last path → count + membership wrong
//   (e.g. ".txt" → [a.txt], count 1 ≠ 2) → FAIL. No expected-value inversion; the actual cook path is bitten.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <system_error>
#include <vector>

#include <unistd.h>  // getpid (unique temp-fixture dir name)

#include "runtime/selftest_registry.h"        // REGISTER_SELFTESTS
#include "runtime/stringlist_op_registry.h"  // StringListCookCtx / findStringListOp / stringListInjectBug

namespace sw {

namespace {

// Basename (filename component) of a full path — last '/' tail, or the whole string when none.
std::string baseName(const std::string& path) {
  std::filesystem::path p(path);
  return p.filename().string();
}

// Cook FilesInFolder with the given Folder/Filter via a hand-built ctx (the cook-driver shape), returning
// the emitted full-path list. Uses the REGISTERED cook fn (findStringListOp) so the production dispatch is
// what runs. injectBug toggles the leaf's REAL-output corruption hook around the cook.
std::vector<std::string> cookList(const std::string& folder, const std::string& filter, bool injectBug) {
  std::vector<std::string> out;
  const StringListCookFn* fn = findStringListOp("FilesInFolder");
  if (!fn || !*fn) return out;  // not registered → empty (will fail the green assert loudly)

  std::vector<std::string> inputs = {folder, filter};  // inputStrings[0]=Folder, [1]=Filter
  StringListCookCtx c{};
  c.inputStrings = &inputs;
  c.output = &out;

  stringListInjectBug() = injectBug;
  (*fn)(c);
  stringListInjectBug() = false;
  return out;
}

// Sorted basenames of an emitted full-path list (the cook already sorts full paths; basenames of a sorted
// full-path list under a single common directory are themselves sorted — re-sort defensively for clarity).
std::vector<std::string> sortedBaseNames(const std::vector<std::string>& paths) {
  std::vector<std::string> names;
  names.reserve(paths.size());
  for (const auto& p : paths) names.push_back(baseName(p));
  std::sort(names.begin(), names.end());
  return names;
}

bool eq(const std::vector<std::string>& a, const std::vector<std::string>& b) { return a == b; }

std::string join(const std::vector<std::string>& v) {
  std::string s = "[";
  for (size_t i = 0; i < v.size(); ++i) { if (i) s += ","; s += v[i]; }
  return s + "]";
}

int runFilesInFolderSelftestImpl(bool injectBug) {
  // --- build a unique temp fixture: <tmp>/sw_filesinfolder_fixture_<pid>/{a.txt,b.txt,c.dat} ---
  std::error_code ec;
  std::filesystem::path base = std::filesystem::temp_directory_path(ec);
  std::filesystem::path dir = base / ("sw_filesinfolder_fixture_" + std::to_string(::getpid()));
  std::filesystem::remove_all(dir, ec);             // clean any stale leftover
  std::filesystem::create_directories(dir, ec);
  for (const char* name : {"a.txt", "b.txt", "c.dat"}) {
    std::FILE* f = std::fopen((dir / name).string().c_str(), "wb");
    if (f) { std::fputc('x', f); std::fclose(f); }  // 1-byte content; the op lists names, not content
  }
  const std::string folder = dir.string();

  bool ok = true;

  // LEG 1: empty filter → all three, sorted basenames [a.txt,b.txt,c.dat], count 3.
  {
    std::vector<std::string> got = cookList(folder, "", injectBug);
    std::vector<std::string> names = sortedBaseNames(got);
    const std::vector<std::string> want = {"a.txt", "b.txt", "c.dat"};
    bool pass = eq(names, want) && got.size() == 3;
    ok = ok && pass;
    std::printf("[selftest-filesinfolder] LEG1 filter=\"\" got=%s count=%zu want=%s count=3 -> %s\n",
                join(names).c_str(), got.size(), join(want).c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 2: filter ".txt" → [a.txt,b.txt], count 2 (substring select; c.dat excluded).
  {
    std::vector<std::string> got = cookList(folder, ".txt", injectBug);
    std::vector<std::string> names = sortedBaseNames(got);
    const std::vector<std::string> want = {"a.txt", "b.txt"};
    bool pass = eq(names, want) && got.size() == 2;
    ok = ok && pass;
    std::printf("[selftest-filesinfolder] LEG2 filter=\".txt\" got=%s count=%zu want=%s count=2 -> %s\n",
                join(names).c_str(), got.size(), join(want).c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 3: non-existent folder → empty list, count 0. (injectBug no-op: empty output, the tooth bites
  // non-empty — so this leg passes under -bug too, which is correct: it is not the biting leg.)
  {
    std::vector<std::string> got = cookList((dir / "does_not_exist").string(), "", injectBug);
    bool pass = got.empty();
    ok = ok && pass;
    std::printf("[selftest-filesinfolder] LEG3 non-existent folder got count=%zu want 0 -> %s\n",
                got.size(), pass ? "PASS" : "FAIL");
  }

  // cleanup the temp fixture
  std::filesystem::remove_all(dir, ec);

  std::printf("[selftest-filesinfolder] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace

// Self-register as --selftest-filesinfolder for isolated runs. orderBase 268 = a free slot after the
// string-rail family (wrapstring=260); the row sorts deterministically into --selftest-list (see
// selftest_registry.h ORDER note). NO shared-file edit — this manifest is globbed via src/selftests*.cpp.
REGISTER_SELFTESTS(/*orderBase=*/268, {"filesinfolder", runFilesInFolderSelftestImpl});

}  // namespace sw
