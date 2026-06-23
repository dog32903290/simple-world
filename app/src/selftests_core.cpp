// app/src/selftests_core.cpp — area manifest leaf for the --selftest router: core graph/save/command + audio + early particle (graph..metal-compile)
//
// Shell-tier (app/src/ root, like selftests.cpp): may name selftest fns from any zone via
// selftests_decls.h. Self-registers its rows into selftestRegistry() during pre-main dynamic init;
// selftests.cpp reads that sink. Adding a selftest to this area = add ONE row below — selftests.cpp
// is never touched. ORDER_BASE is the global index of the first row (keeps --selftest-list identical
// to the pre-split kTable order; see selftest_registry.h). Rows kept verbatim from the old kTable.
#include "runtime/selftest_registry.h"
#include "selftests_decls.h"

namespace sw {
REGISTER_SELFTESTS(/*orderBase=*/1,
    {"graph", runGraphRoundtripSelfTest},
    {"save", runSaveLoadSelfTest},
    {"command", runCommandSelfTest},
    {"defremoval", runDefRemovalSelfTest},
    {"copypaste", runCopyPasteSelfTest},
    {"rename", runRenameSelfTest},
    {"childstate", runChildStateSelfTest},
    {"annotation", runAnnotationSelfTest},
    {"navigation", doc::runNavigationSelfTest},
    {"valuecook", runValueCookSelfTest},
    {"resolve", runResolveSelfTest},
    {"audionode", runAudioNodeSelfTest},
    {"attack", runAttackSelfTest},
    {"analyzer", runAudioAnalyzerSelfTest},
    {"spectrum", runSpectrumSelfTest},
    {"audioreaction", runAudioReactionSelfTest},
    {"audiomonitor", audio_monitor::runAudioMonitorSelfTest},
    {"bpm-detect", runBpmDetectionSelfTest},
    {"detectbpm", runDetectBpmSelfTest},
    {"flow", runParticleFlowSelfTest},
    {"draw", runDrawPointsSelfTest},
    {"decay", runParticleDecaySelfTest},
    {"pointgraph", runPointGraphSelfTest},
    {"residentcook", runResidentCookSelfTest},
    {"residentparity", runResidentCookParitySelfTest},
    {"imagedecode", platform::runImageDecodeSelfTest},
    {"metal-compile", platform::runMetalCompileSelfTest},
);
}  // namespace sw
