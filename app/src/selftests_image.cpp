// app/src/selftests_image.cpp — area manifest leaf for the --selftest router: image filters (blur/tint/draw*/screenquad/render-target)
//
// Shell-tier (app/src/ root, like selftests.cpp): may name selftest fns from any zone via
// selftests_decls.h. Self-registers its rows into selftestRegistry() during pre-main dynamic init;
// selftests.cpp reads that sink. Adding a selftest to this area = add ONE row below — selftests.cpp
// is never touched. ORDER_BASE is the global index of the first row (keeps --selftest-list identical
// to the pre-split kTable order; see selftest_registry.h). Rows kept verbatim from the old kTable.
#include "runtime/selftest_registry.h"
#include "selftests_decls.h"

namespace sw {
REGISTER_SELFTESTS(/*orderBase=*/187,
    {"blur", runBlurSelfTest},
    {"blurchain", runBlurChainSelfTest},
    {"displace", runDisplaceSelfTest},
    {"displacechain", runDisplaceChainSelfTest},
    {"blendchain", runBlendChainSelfTest},                  // multi-image seam (3rd consumer): resident gather
    {"blendwithmaskchain", runBlendWithMaskChainSelfTest},  // multi-image seam (1st THREE-input consumer)
    {"tint", runTintSelfTest},
    {"tintchain", runTintChainSelfTest},
    {"chromab", runChromaBAShiftSelfTest},
    {"adjustcolors", runAdjustColorsSelfTest},
    {"pixelate", runPixelateSelfTest},
    {"sharpen", runSharpenSelfTest},
    {"detectedges", runDetectEdgesSelfTest},
    {"chromaticdistortion", runChromaticDistortionSelfTest},
    {"voronoicells", runVoronoiCellsSelfTest},
    {"dither", runDitherSelfTest},
    {"normalmap", runNormalMapSelfTest},
    {"chromakey", runChromaKeySelfTest},
    {"convertcolors", runConvertColorsSelfTest},
    {"drawlines", runDrawLinesSelfTest},
    {"drawclosedlines", runDrawClosedLinesSelfTest},
    {"drawpoints2", runDrawPoints2SelfTest},
    {"drawlinesbuildup", runDrawLinesBuildupSelfTest},
    {"drawbillboards", runDrawBillboardsSelfTest},
    {"drawscreenquad", runDrawScreenQuadSelfTest},
    {"drawscreenquadclamp", runDrawScreenQuadClampSelfTest},
    {"drawscreenquadfilter", runDrawScreenQuadFilterSelfTest},
    {"drawscreenquadblend", runDrawScreenQuadBlendSelfTest},
    {"drawscreenquadwired", runDrawScreenQuadWiredSelfTest},
    {"clearrendertarget", runClearRenderTargetSelfTest},
    {"fxaa", runFxaaSelfTest},  // NVIDIA FXAA 3.11 anti-aliasing (TiXL image/use/Fxaa)
);
}  // namespace sw
