// app/src/selftests_list.cpp — area manifest leaf for the --selftest router: float/color list + gradient + string rail + point list
//
// Shell-tier (app/src/ root, like selftests.cpp): may name selftest fns from any zone via
// selftests_decls.h. Self-registers its rows into selftestRegistry() during pre-main dynamic init;
// selftests.cpp reads that sink. Adding a selftest to this area = add ONE row below — selftests.cpp
// is never touched. ORDER_BASE is the global index of the first row (keeps --selftest-list identical
// to the pre-split kTable order; see selftest_registry.h). Rows kept verbatim from the old kTable.
#include "runtime/selftest_registry.h"
#include "selftests_decls.h"

namespace sw {
REGISTER_SELFTESTS(/*orderBase=*/72,
    {"floatlist", runFloatListSelfTest},
    {"colorstolist", runColorsToListSelfTest},
    {"colorlist", runColorListSelfTest},
    {"combinecolorlists", runCombineColorListsSelfTest},
    {"readpointcolors", runReadPointColorsSelfTest},
    {"keepcolors", runKeepColorsSelfTest},
    {"stringrail", runStringRailSelfTest},
    {"listrouting", runListRoutingSelfTest},
    {"pointlist", runPointListSelfTest},
    {"gradient", runGradientSelfTest},
    {"pickgradient", runPickGradientSelfTest},
    {"blendgradients", runBlendGradientsSelfTest},
);
}  // namespace sw
