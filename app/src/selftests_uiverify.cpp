// app/src/selftests_uiverify.cpp — area manifest leaf for the --selftest router: ui (timeline/keymap/...) + verify (eye/map/hand)
//
// Shell-tier (app/src/ root, like selftests.cpp): may name selftest fns from any zone via
// selftests_decls.h. Self-registers its rows into selftestRegistry() during pre-main dynamic init;
// selftests.cpp reads that sink. Adding a selftest to this area = add ONE row below — selftests.cpp
// is never touched. ORDER_BASE is the global index of the first row (keeps --selftest-list identical
// to the pre-split kTable order; see selftest_registry.h). Rows kept verbatim from the old kTable.
#include "runtime/selftest_registry.h"
#include "selftests_decls.h"

namespace sw {
REGISTER_SELFTESTS(/*orderBase=*/217,
    {"timeline", ui::runTimelineSelfTest},
    {"nodestyle", ui::runNodeStyleSelfTest},
    {"anndraw",   ui::runAnnotationDrawSelfTest},
    {"canvasids", ui::runCanvasIdsSelfTest},
    {"cjkfont", ui::runCjkFontSelfTest},
    {"eye", eye::runSelfTest},
    {"map", eye::runMapSelfTest},
    {"hand", hand::runSelfTest},
    {"audioingest", runAudioIngestSelfTest},
    {"soundtrack", soundtrack::runSoundtrackSelfTest},
    {"keymap",    ui::km::runKeymapSelfTest},
    {"quickadd",  ui::runQuickAddSelfTest},
    {"fencepreview", ui::runFenceSelfTest},
);
// New teeth append at a fresh high order (>340, clear of every existing block) so they sort LAST
// and never reorder the verbatim-from-kTable rows above (--selftest-list stays stable). The router
// reads the list dynamically, so an appended tooth is purely additive.
REGISTER_SELFTESTS(/*orderBase=*/500,
    {"nodeval", ui::runNodeValSelfTest},  // experience-parity: body value-string format + zoom gating
    {"keymap-persist", runKeymapPersistSelfTest},  // #11: user keymap JSON overrides factory (round-trip)
    {"user-settings", runUserSettingsSelfTest},    // #12: recent-files MRU persistence (round-trip)
    {"output-window-state", runOutputWindowStateSelfTest},  // out-window-persistence: Output view-state sidecar (round-trip)
    {"hand-connect", runHandConnectSelfTest},      // connect/disconnect hand verbs → wire edit (headless)
    {"graphdump", ui::runGraphDumpSelfTest},       // req_graph → graph.json of current compound (免座標 id 來源)
    {"eye-occlusion", eye::runOcclusionSelfTest},  // map.json occluded flag: covered widget reports occluded
    {"theme", ui::theme::runThemeSelfTest},        // default theme table == TiXL UiColors constants (value-golden)
    {"theme-roundtrip", runThemeRegistrySelfTest}, // color-theme registry: construct→save→reload bit-for-bit survival (+RED leg)
);
}  // namespace sw
