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
    {"listroutingwave1", runListRoutingWave1SelfTest},
    {"pointlist", runPointListSelfTest},
    {"pointstocpu", runPointsToCpuSelfTest},
    {"gradient", runGradientSelfTest},
    {"pickgradient", runPickGradientSelfTest},
    {"blendgradients", runBlendGradientsSelfTest},
);
// New stateful-string-rail rows go in their OWN block with a high orderBase so they APPEND at the end of
// --selftest-list deterministically (the 72-block is contiguous 72..83 with no room to insert mid-block;
// the registry sorts by `order`, so 300 lands after every existing row without renumbering anything).
REGISTER_SELFTESTS(/*orderBase=*/300,
    {"hasstringchanged", runHasStringChangedSelfTest},  // per-node cross-frame STRING state (HasStringChanged)
);
// Wave-2 FloatList→FloatList producers (list fan-out). Own high-orderBase block so it appends at the end
// of --selftest-list deterministically (the registry sorts by `order`).
REGISTER_SELFTESTS(/*orderBase=*/310,
    {"floatlistproducers", runFloatListProducersSelfTest},  // Combine/IntsToList/SetFloat/SetInt/Remap (chain-through-evalFloat)
    {"smoothvalues", runSmoothValuesSelfTest},              // SmoothValues forward-window box average (STATELESS FloatList→FloatList, chain-through-evalFloat)
    {"animfloatlist", runAnimFloatListSelfTest},            // AnimFloatList animator PRODUCER (AnimMath shapes → List<float> on LocalFxTime; flat + production-resident chain-through)
    {"floatlistconversion", runFloatListConversionSelfTest},  // FloatListToIntList (trunc-toward-zero) + IntListToFloatList (widening), chain-through-evalFloat
    {"amplifyvalues", runAmplifyValuesSelfTest},            // AmplifyValues cross-frame STATE (damp toward input over frames; flat + R-2 production-resident)
    {"dampfloatlist", runDampFloatListSelfTest},            // DampFloatList cross-frame STATE (per-index damp + dt-gate; flat + production-resident)
    {"keepfloatvalues", runKeepFloatValuesSelfTest},        // KeepFloatValues cross-frame STATE (front-insert ring accumulator; flat + production-resident)
);
// PointList host-rail LEAF ops (SampleCpuPoints / JoinLists). Own high-orderBase block so they append at
// the end of --selftest-list deterministically (the registry sorts by `order`; the 72-block is full).
REGISTER_SELFTESTS(/*orderBase=*/320,
    {"samplecpupoints", runSampleCpuPointsSelfTest},        // SampleCpuPoints: 2-key host list -> 1 Bezier+quaternion resampled point (pure CPU)
    {"joinlists", runJoinListsSelfTest},                    // JoinLists (Result-only): N host lists -> ONE concat in wire order (Length deferred)
);
}  // namespace sw
