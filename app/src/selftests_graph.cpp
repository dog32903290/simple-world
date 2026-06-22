// app/src/selftests_graph.cpp — area manifest leaf for the --selftest router: graph bypass/compound/transport/anim/curve/resident eval core
//
// Shell-tier (app/src/ root, like selftests.cpp): may name selftest fns from any zone via
// selftests_decls.h. Self-registers its rows into selftestRegistry() during pre-main dynamic init;
// selftests.cpp reads that sink. Adding a selftest to this area = add ONE row below — selftests.cpp
// is never touched. ORDER_BASE is the global index of the first row (keeps --selftest-list identical
// to the pre-split kTable order; see selftest_registry.h). Rows kept verbatim from the old kTable.
#include "runtime/selftest_registry.h"
#include "selftests_decls.h"

namespace sw {
REGISTER_SELFTESTS(/*orderBase=*/90,
    {"bypasscook", runBypassCookSelfTest},
    {"bypasscompound", runBypassCompoundSelfTest},
    {"graphbridge", runGraphBridgeSelfTest},
    {"statecount", runStateCountSelfTest},
    {"savev2", runSaveV2SelfTest},
    {"testproj", runTestProjSelfTest},
    {"forcekindcorrupt", runForceKindCorruptProbe},
    {"combine", runCombineSelfTest},
    {"compoundspec", runCompoundSpecSelfTest},
    {"compoundmodel", runCompoundModelSelfTest},
    {"cycleguard", runCycleGuardSelfTest},
    {"transport", runTransportSelfTest},
    {"arclock", framecook::runArClockSelfTest},
    {"contextvar", framecook::runContextVarSelfTest},  // context-var YELLOW seam (block #1)
    {"animvalue", framecook::runAnimValueSelfTest},    // Anim* foundation: AnimValue on the prod cook
    {"animint", framecook::runAnimIntSelfTest},        // Anim* integer sibling: AnimInt on the prod cook
    {"animboolean", framecook::runAnimBooleanSelfTest},// Anim* edge-only: AnimBoolean on the prod cook
    {"curve", runCurveSelfTest},
    {"animator", runCurveAnimatorSelfTest},
    {"animgui", runAnimGuiSelfTest},
    {"residenteval", runResidentEvalSelfTest},
    {"multiinput", runMultiInputSelfTest},
    {"residentcache", runResidentCacheSelfTest},
    {"idlefade", runIdleFadeSelfTest},
    {"residentpatch", runResidentPatchSelfTest},
    {"residentlibpatch", runResidentLibPatchSelfTest},
);
}  // namespace sw
