# Vuo Mesh Adapter

Version: `0.1`

Purpose: define the first bridge between Vuo's `VuoMesh` and the My World portable `Mesh` contract.

This adapter should not make Vuo or TiXL the core mesh format. Vuo is a host/prototype surface. My World owns the portable mesh contract.

## Source Evidence

Vuo source:

```text
external/vuo/type/VuoMesh.h
external/vuo/type/VuoMesh.c
```

Important Vuo facts:

- `VuoMesh` can hold CPU and/or GPU buffers.
- CPU attributes are positions, normals, texture coordinates, colors, and elements.
- `VuoMesh_ElementAssemblyMethod` decides how elements become triangles, lines, or points.
- GPU buffers must not be shared blindly into My World runtime state.

## Node Contract

```text
Node: VuoToMyWorldMesh
Question: how does a Vuo mesh become a portable My World Mesh value?
Conversion: VuoMesh CPU snapshot -> Mesh
Family: mesh
Domain: frame
Input: VuoMesh snapshot
Output: Mesh
State: stateless for CPU snapshots; externalResource for live Vuo objects
Failure: write errors.json; do not invent missing attributes
Diagnostics: vertex count, index count, topology, attributes, bounds, ownership
Evidence: mesh_contract.json, mesh_stats.json, errors.json
```

## Current Shell

Fixture:

```text
docs/runtime/fixtures/vuo_triangle_mesh.snapshot.json
```

Converter:

```text
docs/runtime/scripts/convert_vuo_mesh_shell.py
```

Artifacts:

```text
docs/runtime/artifacts/vuo_mesh_adapter/mesh_contract.json
docs/runtime/artifacts/vuo_mesh_adapter/mesh_stats.json
docs/runtime/artifacts/vuo_mesh_adapter/errors.json
```

Test:

```text
tests/runtime_vuo_mesh_adapter.test.js
```

## Supported Now

- `VuoMesh_IndividualTriangles` -> `topology: triangles`
- `VuoMesh_IndividualLines` -> `topology: lines`
- `VuoMesh_Points` -> `topology: points`
- attributes:
  - `positions` -> `position: Vec3`
  - `normals` -> `normal: Vec3`
  - `textureCoordinates` -> `uv: Vec2`
  - `colors` -> `color: Color`

## Not Solved Yet

- `VuoMesh_TriangleStrip`
- `VuoMesh_TriangleFan`
- `VuoMesh_LineStrip`
- coordinate handedness
- up axis
- live GPU buffer sharing
- dynamic / streaming mesh update policy
- Vuo custom node packaging

Rule:

```text
If topology cannot be preserved, fail loudly instead of silently triangulating.
```
