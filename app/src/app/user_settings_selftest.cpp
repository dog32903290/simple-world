// user_settings_selftest — the round-trip gate for #12 (editor user-settings: recent-files MRU).
// --selftest-user-settings / --selftest-user-settings-bug.
//
// Proves (deliverable's gate):
//   1. push files -> save -> RELOAD (fresh store from disk) -> recent list survives in MRU order
//      (most-recent-first), deduped, capped at maxRecent().
//   2. dedup: re-pushing an existing path MOVES it to the front (no duplicate, size unchanged).
//   3. cap: pushing > maxRecent() distinct paths keeps only the newest maxRecent(), oldest dropped.
//   4. NO settings file -> empty list (no regression); empty/corrupt file -> empty list.
//   5. -bug: persistence intentionally broken (MRU order reversed on reload) -> RED.
#include "app/user_settings.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace sw {

namespace {
using settings::UserSettings;

std::string tmpPath() {
  const char* tmp = std::getenv("TMPDIR");
  std::string dir = tmp && *tmp ? tmp : "/tmp";
  if (dir.back() != '/') dir += '/';
  return dir + "sw_user_settings_selftest.json";
}

bool eq(const std::vector<std::string>& a, const std::vector<std::string>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (a[i] != b[i]) return false;
  return true;
}
}  // namespace

int runUserSettingsSelfTest(bool injectBug) {
  int fail = 0;
  const std::string path = tmpPath();
  std::remove(path.c_str());  // start clean

  const std::string fA = "/tmp/projA.swproj";
  const std::string fB = "/tmp/projB.swproj";
  const std::string fC = "/tmp/projC.swproj";

  // --- (4) NO settings file => empty list (no regression). ---
  {
    UserSettings fresh;
    if (!fresh.load(path)) {  // missing file must succeed (not an error)
      std::printf("[user-settings] load(missing) returned false -> FAIL\n"); ++fail;
    }
    if (!fresh.recentFiles().empty()) {
      std::printf("[user-settings] missing file gave %d recent (want 0) -> FAIL\n",
                  (int)fresh.recentFiles().size()); ++fail;
    }
  }

  // --- (1) push files -> save -> reload (fresh store) -> MRU order survives. ---
  {
    UserSettings a;
    a.pushRecentFile(fA);
    a.pushRecentFile(fB);
    a.pushRecentFile(fC);  // C is most-recent -> front; expected order: [C, B, A]
    const std::vector<std::string> want = {fC, fB, fA};
    if (!eq(a.recentFiles(), want)) {
      std::printf("[user-settings] push order != [C,B,A] -> FAIL\n"); ++fail;
    }
    if (!a.save(path)) { std::printf("[user-settings] save failed -> FAIL\n"); ++fail; }

    // Fresh store reloaded from disk (the real cross-session path).
    UserSettings b;
    if (!b.load(path)) { std::printf("[user-settings] reload failed -> FAIL\n"); ++fail; }

    std::vector<std::string> got = b.recentFiles();
    // -bug: simulate persistence that reverses MRU order on reload -> most-recent ends up last.
    if (injectBug) std::reverse(got.begin(), got.end());

    if (!eq(got, want)) {
      std::printf("[user-settings] reloaded MRU order wrong (front=%s want %s) -> FAIL\n",
                  got.empty() ? "(empty)" : got.front().c_str(), fC.c_str());
      ++fail;  // the load-bearing assertion: MRU order survived the round-trip
    }
  }

  // --- (2) dedup: re-pushing an existing path moves it to the front (no duplicate). ---
  {
    UserSettings c;
    c.pushRecentFile(fA);
    c.pushRecentFile(fB);
    c.pushRecentFile(fA);  // re-push A -> A to front, B after; size stays 2
    const std::vector<std::string> want = {fA, fB};
    if (!eq(c.recentFiles(), want)) {
      std::printf("[user-settings] dedup move-to-front failed (size=%d) -> FAIL\n",
                  (int)c.recentFiles().size()); ++fail;
    }
  }

  // --- (3) cap: > maxRecent() distinct paths keeps only the newest maxRecent(). ---
  {
    UserSettings d;
    const int cap = UserSettings::maxRecent();
    for (int i = 0; i < cap + 5; ++i)
      d.pushRecentFile("/tmp/p" + std::to_string(i) + ".swproj");
    if ((int)d.recentFiles().size() != cap) {
      std::printf("[user-settings] cap not enforced (size=%d want %d) -> FAIL\n",
                  (int)d.recentFiles().size(), cap); ++fail;
    }
    // newest is at front; the very first ones (p0..p4) must have been dropped.
    const std::string newest = "/tmp/p" + std::to_string(cap + 4) + ".swproj";
    if (d.recentFiles().empty() || d.recentFiles().front() != newest) {
      std::printf("[user-settings] cap dropped the newest instead of oldest -> FAIL\n"); ++fail;
    }
  }

  // --- (4b) empty path ignored; removeRecentFile drops a path. ---
  {
    UserSettings e;
    e.pushRecentFile("");          // ignored
    e.pushRecentFile(fA);
    e.pushRecentFile(fB);
    e.removeRecentFile(fA);
    const std::vector<std::string> want = {fB};
    if (!eq(e.recentFiles(), want)) {
      std::printf("[user-settings] empty-ignore / remove failed -> FAIL\n"); ++fail;
    }
  }

  // --- pure JSON round-trip preserves the list + order. ---
  {
    UserSettings f;
    f.pushRecentFile(fA);
    f.pushRecentFile(fB);
    UserSettings g;
    if (!g.fromJson(f.toJson())) { std::printf("[user-settings] fromJson(toJson) failed -> FAIL\n"); ++fail; }
    if (!eq(g.recentFiles(), f.recentFiles())) {
      std::printf("[user-settings] JSON round-trip lost order -> FAIL\n"); ++fail;
    }
  }

  // --- corrupt file => fromJson false, store empty (=> empty list). ---
  {
    UserSettings h;
    if (h.fromJson("{ this is not json")) {
      std::printf("[user-settings] fromJson(garbage) returned true -> FAIL\n"); ++fail;
    }
    if (!h.recentFiles().empty()) {
      std::printf("[user-settings] garbage parse left %d recent -> FAIL\n",
                  (int)h.recentFiles().size()); ++fail;
    }
  }

  std::remove(path.c_str());

  if (injectBug) {
    std::printf("[user-settings] injectBug fail count=%d -> %s\n", fail,
                fail > 0 ? "PASS (red-proof)" : "FAIL (bug not caught)");
    return fail > 0 ? 1 : 0;
  }
  std::printf("[user-settings] fail=%d -> %s\n", fail, fail == 0 ? "PASS" : "FAIL");
  return fail;
}

}  // namespace sw
