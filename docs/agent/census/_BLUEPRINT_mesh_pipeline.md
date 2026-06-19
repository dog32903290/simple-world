# Build Blueprint: mesh-pipeline block #4 = MeshBuffers 4th cook flow + NGonMesh + QuadMesh

Scout output (Cut 90, 2026-06-20). mesh-pipeline = a **4th cook flow** (parallel to Points/Command/Texture2D in point_graph.cpp) carrying currency **MeshBuffers** = PAIR of buffers: vertices (PbrVertex 80B) + indices (Int3 12B). GREENFIELD (no existing Mesh infra — "Mesh" appears only as a throwaway string in child_state_selftest.cpp:129). CPU-self-sufficient: cook writes contents(), golden reads back memcpy, NO GPU draw/camera. The Mesh currency IS the seam; NGonMesh+QuadMesh prove it.

## TiXL data structures (forced parity — match byte-exact)
- PbrVertex (Core/DataTypes/PbrVertex.cs:5-32): **80 bytes, 20 floats** — Position(3)/Normal(3)/Tangent(3)/Bitangent(3)/Texcoord(2)/Texcoord2(2)/Selection(1)/ColorRgb(... to 80B). ★ALIGNMENT TRAP: Selection is a lone float @64 immediately before ColorRgb @68 (vec3). Use sw_packed3 trick (tixl_point.h precedent) for ALL vec3 members + static_assert EVERY offset (metal-cpp-discipline — the 本質-complex spot; wrap in header so 柏為 never touches).
- Index = Int3 (12 bytes, 3×int32), one triangle = 3 vertex indices. TiXL stride = 3*4 (QuadMesh.cs:117).
- ChunkDefsBuffer: only instancing uses it; CPU generators leave default → DEFER (don't add to struct day 1).

## Existing infra (reuse as BLUEPRINT, not direct reuse, file:line)
- dataType-string currency dispatch: graph.h:28 PortSpec.dataType; branches point_graph.cpp:238/314/318/379/384.
- typed GPU buffer flow + per-node owned pre-sized output, cook writes contents(): PointCookCtx (point_graph.h:47-76), element SwPoint 64B (tixl_point.h:36).
- CPU-readback golden (no draw): point_graph_selftest.cpp:27/37/91 (cook generator → output->contents() → assert).
- self-registration sink: field_node_registry.h (FieldOp) — clone for MeshOp.
★Mesh is NOT a point bag: TWO buffers travel together. Do NOT overload PointCookCtx (point-shaped, single SwPoint output). Add PARALLEL MeshCookFn + cookMeshNode branch keyed dataType=="Mesh", mirroring how Texture2D was added as the 3rd flow. REUSE: (a) dataType dispatch mechanism, (b) owned-output + count-change-reuse lifetime rule, (c) self-reg sink pattern, (d) StorageModeShared+contents() golden harness.

## Proving ops (backward-traced .cs, port VERBATIM)
**A. NGonMesh** (mesh/generate/NGonMesh.cs) — #1 cheapest, triangle fan:
- vertex[0]=center(0,0,0); vertex[1..N] on circle: phi=2π·i/N, p=(R·sinφ·stretchX, R·cosφ·stretchY, 0) (NGonMesh.cs:84-89).
- index[i] = Int3(0, (i+2)>N ? 1 : i+2, i+1) (NGonMesh.cs:120-122).
- counts: verticesCount=segments+1, faceCount=segments.
- GOLDEN (Segments=4,Radius=1,Stretch=(1,1),no rot): verts = (0,0,0),(0,1,0),(1,0,0),(0,-1,0),(-1,0,0); faces = (0,2,1),(0,3,2),(0,4,3),(0,1,4). Assert vtxcount=5,facecount=4,exact pos,exact winding. ★Segments=4 → sin/cos at 0/90/180/270 = exact 0/±1 (NO epsilon; Cut71-72 degenerate-free-coord rule). injectBug: corrupt a vertex pos or index triple in the real cook.
**B. QuadMesh** (mesh/generate/QuadMesh.cs) — #2 grid:
- columns=SegW+1, rows=SegH+1; verticesCount=columns·rows, faceCount=(columns-1)(rows-1)·2.
- vertex[row + col·rows].Position = (col·colStep, row·rowStep, 0)+offset (QuadMesh.cs:82-94).
- 2 tris/cell: Int3(v,v+rows,v+1) + Int3(v+rows,v+rows+1,v+1) (QuadMesh.cs:106-107).
- GOLDEN (Segments=(1,1),Scale=1,Stretch=(1,1),pivot=(0.5,0.5)): 2×2=4 verts, 2 faces, offset=stretch·scale·(pivot-0.5)=0, colStep=rowStep=1. Assert 4 corner pos + 2 face triples exactly. injectBug similar.
**C. FlipNormals** (mesh/modify/FlipNormals.cs) — HOLD for batch 2 (proves mesh→mesh consume; keep batch 1 minimal to generators-only).

## Scope ONE batch — SHIP:
1. sw_mesh.h: SwVertex (80B/20-float, sw_packed3 for vec3, static_assert all offsets) + SwTriIndex (3×int32 12B).
2. MeshCookCtx (output_vertices+output_indices+counts) + MeshCookFn + registerMeshOp/meshSpecSink/MeshOp registrar (clone field_node_registry.h).
3. cookMeshNode branch in cook driver dispatched dataType=="Mesh", alloc owned vtx+idx output buffers (count-change reuse). ★This touches shared承重 point_graph.cpp — orchestrator central-wires (寫-leaf→中央接線), leaves conflict-free.
4. mesh_ops_ngonmesh.cpp + mesh_ops_quadmesh.cpp (self-register NodeSpec+cook fn).
5. 2 CPU-readback goldens (--selftest-mesh-ngon/--selftest-mesh-quad) + injectBug.
DEFERRED: all Draw*Mesh/VisualizeMesh/VisualizeUvMap (camera3d+Layer2d+Execute); ChunkDefs+instancing (DrawMeshChunksAtPoints/_AssembleMeshBuffers); ColorVerticesWithField/SelectVerticesWithSDF/Custom*Shader (shader-graph cross); Displace*/Texture* (asset-tex); LoadGltf/LoadObj (loaders); DelaunayMesh (algorithm); TransformMesh + ITransformable (gizmo seam); FlipNormals (batch 2).

## Forks: vertex/index byte layout = FORCED parity (80B PbrVertex/Int3, match exactly now to avoid re-layout — NOT taste, no 柏為 escalation). 2-buffers-not-interleaved = match TiXL. ChunkDefs out of batch 1. No render/觀感 change in this batch (no pixels) → no 拍板 needed.

## Risks: (1) SwVertex alignment packed_float3 trap (Selection float@64 before ColorRgb vec3@68) — sw_packed3 + static_assert every offset, the one 本質-complex spot. (2) driver branch must slot into Points/Texture2D dispatch site (~point_graph.cpp 238/314/379) without disturbing existing 3 flows. (3) NGonMesh Segments=4 (exact trig, no epsilon). No GPU/camera/pixel/resolution/Layer2d dep → cleanest possible seam.
Size: ONE batch (1 currency header + 1 registry + 1 driver branch + 2 leaves + 2 goldens), ~ Texture2D-flow addition / field seam-establishing cut.
## Critical files: app/src/runtime/point_graph.h+.cpp (driver, the dispatch site — central wire), graph.h (dataType), tixl_point.h (sw_packed3 + static_assert precedent), field_node_registry.h (sink clone), point_graph_selftest.cpp (golden harness), external/tixl Core/DataTypes/PbrVertex.cs + MeshBuffers.cs + Operators/Lib/mesh/generate/{NGonMesh,QuadMesh}.cs (authority).
