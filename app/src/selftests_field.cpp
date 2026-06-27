// app/src/selftests_field.cpp — area manifest leaf for the --selftest router: field-* SDF/codegen golden leaves
//
// Shell-tier (app/src/ root, like selftests.cpp): may name selftest fns from any zone via
// selftests_decls.h. Self-registers its rows into selftestRegistry() during pre-main dynamic init;
// selftests.cpp reads that sink. Adding a selftest to this area = add ONE row below — selftests.cpp
// is never touched. ORDER_BASE is the global index of the first row (keeps --selftest-list identical
// to the pre-split kTable order; see selftest_registry.h). Rows kept verbatim from the old kTable.
#include "runtime/selftest_registry.h"
#include "selftests_decls.h"

namespace sw {
REGISTER_SELFTESTS(/*orderBase=*/26,
    {"field-codegen", runFieldCodegenSelfTest},
    {"field-render", runFieldRenderSelfTest},
    {"field-boxsdf", runFieldBoxSdfGoldenSelfTest},
    {"field-boxframesdf", runFieldBoxFrameSdfGoldenSelfTest},
    {"field-octahedronsdf", runFieldOctahedronSdfGoldenSelfTest},
    {"field-capsulelinesdf", runFieldCapsuleLineSdfGoldenSelfTest},
    {"field-chainlinksdf", runFieldChainLinkSdfGoldenSelfTest},
    {"field-torussdf", runFieldTorusSdfGoldenSelfTest},
    {"field-cylindersdf", runFieldCylinderSdfGoldenSelfTest},
    {"field-planesdf", runFieldPlaneSdfGoldenSelfTest},
    {"field-cappedtorussdf", runFieldCappedTorusSdfGoldenSelfTest},
    {"field-prismsdf", runFieldPrismSdfGoldenSelfTest},
    {"field-pyramidsdf", runFieldPyramidSdfGoldenSelfTest},
    {"field-rotatedplanesdf", runFieldRotatedPlaneSdfGoldenSelfTest},
    {"field-combinesdf", runFieldCombineSdfGoldenSelfTest},
    {"field-invertsdf", runFieldInvertSdfGoldenSelfTest},
    {"field-absolutesdf", runFieldAbsoluteSdfGoldenSelfTest},
    {"field-translate", runFieldTranslateGoldenSelfTest},
    {"field-repeatfield3", runFieldRepeatField3GoldenSelfTest},
    {"field-repeataxis", runFieldRepeatAxisGoldenSelfTest},
    {"field-reflectfield", runFieldReflectFieldGoldenSelfTest},
    {"field-bendfield", runFieldBendFieldGoldenSelfTest},
    {"field-combinefieldcolor", runFieldCombineFieldColorGoldenSelfTest},
    {"field-raster3dfield", runFieldRaster3dFieldGoldenSelfTest},
    {"field-rotateaxis", runFieldRotateAxisGoldenSelfTest},
    {"field-rotatefield", runFieldRotateFieldGoldenSelfTest},
    {"field-twistfield", runFieldTwistFieldGoldenSelfTest},
    {"field-repeatfieldlimit", runFieldRepeatFieldLimitGoldenSelfTest},
    {"field-fractalsdf", runFieldFractalSdfGoldenSelfTest},
    {"field-customsdf", runFieldCustomSdfGoldenSelfTest},
    {"field-image2dsdf", runFieldImage2dSdfGoldenSelfTest},
    {"field-repeatpolar", runFieldRepeatPolarGoldenSelfTest},
    {"field-translateuv", runFieldTranslateUvGoldenSelfTest},
    {"field-staircombinesdf", runFieldStairCombineSdfGoldenSelfTest},
    {"field-noisedisplacesdf", runFieldNoiseDisplaceSdfGoldenSelfTest},
    {"field-spatialdisplacesdf", runFieldSpatialDisplaceSdfGoldenSelfTest},
    {"field-transformfield", runFieldTransformFieldGoldenSelfTest},
    {"field-pushpullsdf", runFieldPushPullSdfGoldenSelfTest},
    {"field-blendsdfwithsdf", runFieldBlendSdfWithSdfGoldenSelfTest},
    {"field-toroidalvortexfield", runFieldToroidalVortexFieldGoldenSelfTest},
    {"field-setsdfmaterial", runFieldSetSDFMaterialGoldenSelfTest},
    {"field-raymarch", runFieldRaymarchSelfTest},
    {"raymarchfield-output", runRaymarchFieldOutputSelfTest},
    {"connect-cooks", runConnectCooksSelfTest},  // connect VERB → production cook → sphere silhouette
);
}  // namespace sw
