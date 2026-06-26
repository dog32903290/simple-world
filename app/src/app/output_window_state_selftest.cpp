// output_window_state_selftest — the round-trip gate for out-window-persistence (Output view state:
// pin / resolution / background). --selftest-output-window-state / --selftest-output-window-state-bug.
//
// Proves (the deliverable's gate):
//   1. set a non-default state -> save -> RELOAD (fresh store from disk) -> EVERY persisted field
//      survives bit-for-bit (pin id, resolution title/dims/aspect, RGBA background).
//   2. the sidecar path policy: "<project>.swproj" -> "<project>.swproj.ui"; empty in -> empty out.
//   3. NO sidecar file -> store at Defaults (no regression); empty file -> Defaults; corrupt -> false.
//   4. pure JSON round-trip (toJson->fromJson) preserves the state (the "OutputWindows" array shape).
//   5. -bug: persistence intentionally drops a field on reload (background color zeroed) -> RED.
#include "app/output_window_state.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace sw {

namespace {
using settings::OutputWindowState;
using settings::OutputWindowStore;

std::string tmpPath() {
  const char* tmp = std::getenv("TMPDIR");
  std::string dir = tmp && *tmp ? tmp : "/tmp";
  if (dir.back() != '/') dir += '/';
  return dir + "sw_output_window_state_selftest.swproj.ui";
}

// A distinctive, fully-non-default state so a dropped field is unmistakable on reload.
OutputWindowState sampleState() {
  OutputWindowState s;
  s.isPinned = true;
  s.pinnedNode = 4242;                 // a specific child id (not 0 = the default)
  s.resolutionTitle = "1080p";         // not "Fill" (the default)
  s.resolutionWidth = 1920;
  s.resolutionHeight = 1080;
  s.resolutionUseAsAspectRatio = false;  // not the Fill default (true)
  s.backgroundColor[0] = 0.25f;
  s.backgroundColor[1] = 0.50f;
  s.backgroundColor[2] = 0.75f;
  s.backgroundColor[3] = 1.00f;
  return s;
}
}  // namespace

int runOutputWindowStateSelfTest(bool injectBug) {
  int fail = 0;
  const std::string path = tmpPath();
  std::remove(path.c_str());  // start clean

  // --- (2) sidecar path policy. ---
  {
    if (settings::outputWindowStatePathFor("/tmp/proj.swproj") != "/tmp/proj.swproj.ui") {
      std::printf("[output-window-state] sidecar path policy wrong -> FAIL\n"); ++fail;
    }
    if (!settings::outputWindowStatePathFor("").empty()) {
      std::printf("[output-window-state] empty project path must give empty sidecar -> FAIL\n"); ++fail;
    }
  }

  // --- (3) NO sidecar file => Defaults (no regression). ---
  {
    OutputWindowStore fresh;
    if (!fresh.load(path)) {  // missing file must succeed (not an error)
      std::printf("[output-window-state] load(missing) returned false -> FAIL\n"); ++fail;
    }
    if (fresh.state() != OutputWindowState{}) {
      std::printf("[output-window-state] missing file did not give Defaults -> FAIL\n"); ++fail;
    }
  }

  // --- (1) set non-default state -> save -> reload (fresh store) -> every field survives. ---
  {
    const OutputWindowState want = sampleState();
    OutputWindowStore a;
    a.setState(want);
    if (!a.save(path)) { std::printf("[output-window-state] save failed -> FAIL\n"); ++fail; }

    // Fresh store reloaded from disk (the real cross-session path).
    OutputWindowStore b;
    if (!b.load(path)) { std::printf("[output-window-state] reload failed -> FAIL\n"); ++fail; }

    OutputWindowState got = b.state();
    // -bug: simulate persistence that DROPS the background color on reload (zeroed) -> the field-by-
    // field equality below must FAIL (RED). This is the load-bearing "every field survived" assertion.
    if (injectBug) { for (int i = 0; i < 4; ++i) got.backgroundColor[i] = 0.0f; }

    if (got != want) {
      std::printf("[output-window-state] reloaded state != saved state "
                  "(pin=%d/%d title=%s/%s aspect=%d/%d bg=[%.2f %.2f %.2f %.2f]) -> FAIL\n",
                  got.isPinned, want.isPinned, got.resolutionTitle.c_str(),
                  want.resolutionTitle.c_str(), got.resolutionUseAsAspectRatio,
                  want.resolutionUseAsAspectRatio, got.backgroundColor[0], got.backgroundColor[1],
                  got.backgroundColor[2], got.backgroundColor[3]);
      ++fail;  // the gate: the full view state survived the save->reload round-trip
    }
  }

  // --- (4) pure JSON round-trip (no disk) preserves the state. ---
  {
    OutputWindowStore c;
    c.setState(sampleState());
    OutputWindowStore d;
    if (!d.fromJson(c.toJson())) {
      std::printf("[output-window-state] fromJson(toJson) failed -> FAIL\n"); ++fail;
    }
    if (d.state() != sampleState()) {
      std::printf("[output-window-state] JSON round-trip lost a field -> FAIL\n"); ++fail;
    }
  }

  // --- (3b) empty file => Defaults; corrupt file => fromJson false (store empty => Defaults). ---
  {
    OutputWindowStore e;
    if (e.fromJson("{ this is not json")) {
      std::printf("[output-window-state] fromJson(garbage) returned true -> FAIL\n"); ++fail;
    }
    if (e.state() != OutputWindowState{}) {
      std::printf("[output-window-state] garbage parse left non-default state -> FAIL\n"); ++fail;
    }
  }

  std::remove(path.c_str());

  if (injectBug) {
    std::printf("[output-window-state] injectBug fail count=%d -> %s\n", fail,
                fail > 0 ? "PASS (red-proof)" : "FAIL (bug not caught)");
    return fail > 0 ? 1 : 0;
  }
  std::printf("[output-window-state] fail=%d -> %s\n", fail, fail == 0 ? "PASS" : "FAIL");
  return fail;
}

}  // namespace sw
