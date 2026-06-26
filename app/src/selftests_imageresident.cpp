// app/src/selftests_imageresident.cpp — area manifest leaf for the --selftest router: resident image-filter compound goldens (crop/fastblur/rgbtv/distort/combine)
//
// Shell-tier (app/src/ root, like selftests.cpp): may name selftest fns from any zone via
// selftests_decls.h. Self-registers its rows into selftestRegistry() during pre-main dynamic init;
// selftests.cpp reads that sink. Adding a selftest to this area = add ONE row below — selftests.cpp
// is never touched. ORDER_BASE is the global index of the first row (keeps --selftest-list identical
// to the pre-split kTable order; see selftest_registry.h). Rows kept verbatim from the old kTable.
#include "runtime/selftest_registry.h"
#include "selftests_decls.h"

namespace sw {
REGISTER_SELFTESTS(/*orderBase=*/84,
    {"cropresident", runResidentCropSelfTest},
    {"fastblurresident", runResidentFastBlurSelfTest},
    {"rgbtvresident", runResidentRgbTvSelfTest},
    {"distortandshaderesident", runResidentDistortAndShadeSelfTest},
    {"combine3imagesresident", runResidentCombine3ImagesSelfTest},
    {"picktextureresident", runResidentPickTextureSelfTest},
    {"combinematerialchannels2resident", runResidentCombineMaterialChannels2SelfTest},
    {"combinematerialchannelsresident", runResidentCombineMaterialChannelsSelfTest},
    {"hseresident", runResidentHseSelfTest},
    {"mosiactilingresident", runResidentMosiacTilingSelfTest},
    {"firstvalidtextureresident", runResidentFirstValidTextureSelfTest},
    {"usefallbacktextureresident", runResidentUseFallbackTextureSelfTest},
);
}  // namespace sw
