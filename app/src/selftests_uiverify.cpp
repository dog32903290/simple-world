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
}  // namespace sw
