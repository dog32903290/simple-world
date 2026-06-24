// keymap_prefs_selftest — the round-trip gate for #11 (user keymap JSON overrides factory).
// --selftest-keymap-persist / --selftest-keymap-persist-bug.
//
// Proves (deliverable's gate):
//   1. set override -> save -> RELOAD (fresh store from disk) -> override is applied (effective
//      chord = the new key), and a NON-overridden action still resolves to its factory default.
//   2. NO user file -> store empty -> every action resolves to factory (no regression).
//   3. JSON round-trip is pure/byte-stable (toJson -> fromJson preserves every chord).
//   4. setOverride replaces (one binding per action = TiXL AddBinding RemoveAll+Add).
//   5. -bug: the persistence is intentionally broken (override dropped on reload) -> RED.
#include "app/keymap_prefs.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace sw {

namespace {
using km::KeyChord;
using km::KeymapPrefs;

// ImGuiKey integer values we reference by name without pulling in imgui (headless test). These match
// the imgui enum; the store treats `key` as an opaque int, so any stable nonzero value round-trips.
constexpr int kKeySpace = 524;  // ImGuiKey_Space (value is irrelevant to the store; just must round-trip)
constexpr int kKeyP     = 545;  // ImGuiKey_P
constexpr int kKeyEnter = 525;  // ImGuiKey_Enter

std::string tmpPath() {
  const char* tmp = std::getenv("TMPDIR");
  std::string dir = tmp && *tmp ? tmp : "/tmp";
  if (dir.back() != '/') dir += '/';
  return dir + "sw_keymap_persist_selftest.json";
}
}  // namespace

int runKeymapPersistSelfTest(bool injectBug) {
  int fail = 0;
  const std::string path = tmpPath();
  std::remove(path.c_str());  // start clean

  const KeyChord factorySpace{kKeySpace, false, false, false};   // PlaybackToggle factory = bare Space
  const KeyChord factoryPinP {kKeyP,     false, false, false};   // PinToOutput factory = bare P
  const KeyChord rebindEnter {kKeyEnter, true,  false, false};   // user rebinds PlaybackToggle -> Cmd+Enter

  // --- (2) NO user file => empty store => factory passes through (no regression). ---
  {
    KeymapPrefs fresh;
    if (!fresh.load(path)) {  // missing file must succeed (not an error)
      std::printf("[keymap-persist] load(missing) returned false -> FAIL\n"); ++fail;
    }
    if (fresh.size() != 0) {
      std::printf("[keymap-persist] missing file gave %d overrides (want 0) -> FAIL\n", fresh.size()); ++fail;
    }
    if (fresh.effective("PlaybackToggle", factorySpace) != factorySpace) {
      std::printf("[keymap-persist] no-file PlaybackToggle != factory -> FAIL\n"); ++fail;
    }
    if (fresh.hasOverride("PlaybackToggle")) {
      std::printf("[keymap-persist] no-file reports an override -> FAIL\n"); ++fail;
    }
  }

  // --- (1) set override -> save -> reload (fresh store) -> override applied. ---
  {
    KeymapPrefs a;
    a.setOverride("PlaybackToggle", rebindEnter);
    // (4) replace semantics: setting again must not duplicate.
    a.setOverride("PlaybackToggle", rebindEnter);
    if (a.size() != 1) {
      std::printf("[keymap-persist] double setOverride gave size=%d (want 1) -> FAIL\n", a.size()); ++fail;
    }
    if (!a.save(path)) { std::printf("[keymap-persist] save failed -> FAIL\n"); ++fail; }

    // Fresh store reloaded from disk (the real cross-session path).
    KeymapPrefs b;
    if (!b.load(path)) { std::printf("[keymap-persist] reload failed -> FAIL\n"); ++fail; }

    KeyChord eff = b.effective("PlaybackToggle", factorySpace);
    // -bug: simulate persistence that silently drops the override on reload -> effective stays factory.
    if (injectBug) eff = factorySpace;

    if (!b.hasOverride("PlaybackToggle")) {
      std::printf("[keymap-persist] reloaded store missing the override -> FAIL\n");
      if (!injectBug) ++fail;  // under -bug this is the expected break
    }
    if (eff != rebindEnter) {
      std::printf("[keymap-persist] reloaded PlaybackToggle effective != Cmd+Enter "
                  "(key=%d ctrl=%d) -> FAIL\n", eff.key, eff.ctrl);
      ++fail;  // the load-bearing assertion: override survived the round-trip
    }
    // A non-overridden action still falls back to factory after reload.
    if (b.effective("PinToOutput", factoryPinP) != factoryPinP) {
      std::printf("[keymap-persist] non-overridden PinToOutput != factory after reload -> FAIL\n"); ++fail;
    }
  }

  // --- (3) pure JSON round-trip preserves every chord + modifier bit. ---
  {
    KeymapPrefs c;
    c.setOverride("PlaybackToggle", rebindEnter);
    c.setOverride("PinToOutput", KeyChord{kKeyP, false, true, true});  // P + Shift + Alt
    KeymapPrefs d;
    if (!d.fromJson(c.toJson())) { std::printf("[keymap-persist] fromJson(toJson) failed -> FAIL\n"); ++fail; }
    if (d.size() != 2) { std::printf("[keymap-persist] round-trip size=%d (want 2) -> FAIL\n", d.size()); ++fail; }
    if (d.override_("PlaybackToggle") != rebindEnter) {
      std::printf("[keymap-persist] round-trip lost PlaybackToggle chord -> FAIL\n"); ++fail;
    }
    if (d.override_("PinToOutput") != KeyChord{kKeyP, false, true, true}) {
      std::printf("[keymap-persist] round-trip lost PinToOutput modifiers -> FAIL\n"); ++fail;
    }
  }

  // --- clearOverride reverts to factory. ---
  {
    KeymapPrefs e;
    e.setOverride("PlaybackToggle", rebindEnter);
    e.clearOverride("PlaybackToggle");
    if (e.hasOverride("PlaybackToggle") || e.effective("PlaybackToggle", factorySpace) != factorySpace) {
      std::printf("[keymap-persist] clearOverride did not revert to factory -> FAIL\n"); ++fail;
    }
  }

  // Corrupt-file handling: a present-but-garbage file => fromJson false, store empty (=> factory).
  {
    KeymapPrefs f;
    if (f.fromJson("{ this is not json")) {
      std::printf("[keymap-persist] fromJson(garbage) returned true -> FAIL\n"); ++fail;
    }
    if (f.size() != 0) {
      std::printf("[keymap-persist] garbage parse left %d overrides -> FAIL\n", f.size()); ++fail;
    }
  }

  std::remove(path.c_str());

  if (injectBug) {
    std::printf("[keymap-persist] injectBug fail count=%d -> %s\n", fail,
                fail > 0 ? "PASS (red-proof)" : "FAIL (bug not caught)");
    return fail > 0 ? 1 : 0;
  }
  std::printf("[keymap-persist] fail=%d -> %s\n", fail, fail == 0 ? "PASS" : "FAIL");
  return fail;
}

}  // namespace sw
