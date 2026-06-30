// app/src/selftests_mesh.cpp — area manifest leaf for the --selftest router: mesh-* golden leaves
//
// Shell-tier (app/src/ root, like selftests.cpp): may name selftest fns from any zone via
// selftests_decls.h. Self-registers its rows into selftestRegistry() during pre-main dynamic init;
// selftests.cpp reads that sink. Adding a selftest to this area = add ONE row below — selftests.cpp
// is never touched. ORDER_BASE is the global index of the first row (keeps --selftest-list identical
// to the pre-split kTable order; see selftest_registry.h). Rows kept verbatim from the old kTable.
#include "runtime/selftest_registry.h"
#include "selftests_decls.h"

namespace sw {
REGISTER_SELFTESTS(/*orderBase=*/64,
    {"mesh-ngon", runMeshNGonGoldenSelfTest},
    {"mesh-quad", runMeshQuadGoldenSelfTest},
    {"mesh-transform", runMeshTransformGoldenSelfTest},
    {"mesh-combine", runMeshCombineGoldenSelfTest},
    {"mesh-production", runMeshInputProductionGoldenSelfTest},
    {"mesh-flipnormals", runMeshFlipNormalsGoldenSelfTest},
    {"mesh-recomputenormals", runMeshRecomputeNormalsGoldenSelfTest},
    {"mesh-transformuvs", runMeshTransformUvsGoldenSelfTest},
);
// Generator family (Sphere/Torus/Cylinder/Cube/Icosahedron). Separate block with a free high
// orderBase so it appends to --selftest-list without reshuffling the pre-existing mesh rows above.
REGISTER_SELFTESTS(/*orderBase=*/330,
    {"mesh-sphere", runMeshSphereGoldenSelfTest},
    {"mesh-torus", runMeshTorusGoldenSelfTest},
    {"mesh-cylinder", runMeshCylinderGoldenSelfTest},
    {"mesh-cube", runMeshCubeGoldenSelfTest},
    {"mesh-cube-uv", runMeshCubeUvGoldenSelfTest},
    {"mesh-icosahedron", runMeshIcosahedronGoldenSelfTest},
    {"mesh-icosahedron-uv", runMeshIcosahedronUvGoldenSelfTest},
);
// Modify family batch 2 (Split/Select/Deform/Collapse/ProjectUV). Fresh high orderBase appends to
// --selftest-list without reshuffling the pre-existing mesh rows.
REGISTER_SELFTESTS(/*orderBase=*/340,
    {"mesh-splitvertices", runMeshSplitVerticesGoldenSelfTest},
    {"mesh-selectvertices", runMeshSelectVerticesGoldenSelfTest},
    {"mesh-deform", runMeshDeformGoldenSelfTest},
    {"mesh-collapse", runMeshCollapseGoldenSelfTest},
    {"mesh-projectuv", runMeshProjectUvGoldenSelfTest},
);
// Modify family batch 3 (BlendMeshVertices / PickMeshBuffer). Fresh high orderBase.
REGISTER_SELFTESTS(/*orderBase=*/350,
    {"mesh-blendmeshvertices", runMeshBlendMeshVerticesGoldenSelfTest},
    {"mesh-pickmeshbuffer", runMeshPickMeshBufferGoldenSelfTest},
);
}  // namespace sw
