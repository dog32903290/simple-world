# SEAM1_VEC4_CURRENCY_BUILD_PLAN — FloatsToBuffer.Vec4Params from a WIRED matrix, both legs

> 2026-06-30 Opus blueprint (vs main `79f819a` + TiXL `395c4c55`). OWNER-LOCKED cook-core, serial.
> Closes the named fork `floatstobuffer-vec4-from-nodeparams`: production (resident leg) gathered ZERO
> Vec4Params, so any compound wiring a matrix into `FloatsToBuffer.Vec4Params(914EA6E8)` produced a buffer
> MISSING the matrix block.
>
> ★SCOPE (ground-truth corrected, refuter-verified 2026-06-30 — do NOT re-overclaim "all 17"): the CURRENCY
> bridge is complete + byte-parity (a matrix on the ColorList channel flows on both legs). But it only SERVES
> a compound whose Vec4Params SOURCE is a producer sw HAS — i.e. `TransformMatrix.Result(751E97DE)`. Of the
> ~20 .t3 connections into Vec4Params, a real chunk wire `TransformMatrix.ResultInverted(ECA8121B)` (deferred
> fork `fork-transformmatrix-resultinverted`, NOT ported — e.g. AttributesFromImageChannels, VolumeForce's 2nd
> wire) or `GetMatrixVar.Result(1EEAB949)` (op ABSENT in sw). Those still get a ZERO matrix block — a SEPARATE
> producer-seam, not a bridge defect. See [[sw-stateful-node-parity-gap]] / chip.

## World-view (read before the build)
- **Two graph reps.** Flat `Graph` (`cookFlatBuffer`, the test/`pg.cook` leg; nodes carry `params`).
  Resident `ResidentEvalGraph` (`cookResidentBuffer`, PRODUCTION; nodes carry `extColorOut`).
- **The matrix currency already exists: `ColorList` = 4 `float4` rows.** TiXL `TransformMatrix.Result` is
  `Slot<Vector4[]>` (the 4 transposed SRT rows, MatrixExtensions.Row1..Row4 = M11..M44). sw carries that on
  the **ColorList channel** (`fork-matrix-as-4-vec4-on-extColorOut`): `cookMatrixOutputNodes`
  (resident_matrix_output_cook.cpp) writes the 4 rows to `ResidentNode::extColorOut[outIdx]`; the flat
  twin is `cookColorListNode(id) -> vector<float4>` (cooked into `colorListBuf`).
- **The gap is a CONNECTION-CURRENCY mismatch.** `TransformMatrix.Result` dataType = `"ColorList"`;
  `FloatsToBuffer.Vec4Params` dataType = `"Vec4Params"`. Neither cook leg bridges a ColorList wire into the
  Vec4Params gather. Flat reads Vec4Params ONLY from `Node::params["Vec4Params.<m>.<k>"]` (test stand-in);
  resident gathers none.
- **TiXL fill order (verified, FloatsToBuffer.cs:17-54):** `totalFloatCount = floatParamCount +
  vec4ArrayLength*4*4`. Matrices FIRST (each entry = 4 `float4` = 16 floats, `.X.Y.Z.W` per row), then the
  scalar floats. sw's `cookFloatsToBuffer` already lays exactly this — only the *source* of the matrices is
  missing. **Each Vec4Params entry is exactly 16 floats** (TiXL's `*4*4` assumes a 4-row matrix); keep
  `vec4Inputs : vector<array<float,16>>` (no ctx-type widening).

## The bridge (minimal, both legs source from the ColorList channel)
Keep dataType `"Vec4Params"` as the routing tag (it means "lay 16 floats per wire, matrices-first" — distinct
from a future generic ColorList-input op). Only change the SOURCE of the gather: a wired ColorList producer.

### Flat leg — `point_graph_buffer_cook.cpp` (+ decl + call site)
1. `cookFlatBuffer` gains a `cookColorListNode` param (already live in point_graph.cpp scope, line 522).
2. Vec4Params branch: for each `Connection` into the Vec4Params pin, `cookColorListNode(srcId)` → take rows
   `[0..3]` → 16 floats (`.x.y.z.w` per row) → one `array<float,16>`. If NO wires, FALL BACK to the existing
   `Node::params["Vec4Params.<m>.<k>"]` stand-in (keeps the current `--selftest-floatstobuffer` green).
- Edits: `point_graph_buffer_cook.cpp` (gather), `point_graph_internal.h` (decl), `point_graph.cpp:557`
  (pass `cookColorListNode`). **Cook-core (point_graph.cpp) = owner-locked.**

### Resident leg — `point_graph_resident_buffer.cpp`
Replace the "Vec4Params → empty" comment-fork with the real gather: `ri = n->input("Vec4Params")`; if
`driver==Connection`, read the upstream `extColorOut` of `(srcNodePath, srcSlotId)` and each `extraConns`
entry → 16 floats each. Map `srcSlotId → output port index` via `findSpec(src->opType)` (the output port
whose `id==srcSlotId`), then `src->extColorOut[idx]`. `cookMatrixOutputNodes`/`cookColorListNodes` already ran
in `cookHostValueNodes` BEFORE `pg.cookResident` (cook_host_values.cpp:75,93 → frame_cook), so extColorOut is
settled. Fork renamed `cookresidentbuffer-vec4-from-extcolorout` (was `-from-resident-params`).

## Goldens (TDD — write RED first)
- **G1 `vec4-currency` (both legs, gather symmetry).** `ColorsToList`(4 known distinct rows, e.g.
  (1,2,3,4)(5,6,7,8)(9,10,11,12)(13,14,15,16)) → `FloatsToBuffer.Vec4Params`, + `Const` scalars → `Params`.
  Cook flat (direct) AND resident (run `cookColorListNodes(rg)` then `cookResident`). Assert flat==resident
  byte-identical AND `bytes[0..15]==the 16 rows`, `bytes[16..]==scalars`, count=16+N, stride=4. ColorsToList
  is the only pure-host ColorList producer real on BOTH legs (the both-legs vehicle). RED today: flat falls to
  Node::params (no matrix), resident gathers none → no matrix block.
  `-bug` drops last scalar → count mismatch → RED both legs.
- **G2 `transformmatrix-to-buffer` (resident, the production keystone).** `TransformMatrix`(known SRT) →
  `FloatsToBuffer.Vec4Params`. Run `cookMatrixOutputNodes(rg)` then `cookResident`; assert `bytes[0..15] ==
  transformMatrixRows(known SRT)` (the SAME closed-form the existing `--selftest-transformmatrix` golden
  TiXL-verifies → resident==TiXL, non-circular). Proves the actual 17-compound wire. Flat omitted by
  construction (no flat TransformMatrix producer — resident IS production).

## Risk / de-risk
- **Highest:** resident `srcSlotId → output index` mapping. If wrong, reads `extColorOut[wrong]` → empty/garbage.
  De-risk: G1 asserts byte-identical to the TiXL-verified flat leg; G2 asserts against closed-form rows.
- Row truncation: a source with !=4 rows. Faithful contract = exactly 4 (a matrix); take `min(4,size)`,
  zero-pad. TiXL would buffer-overrun on !=4, so 4-exact is the contract; goldens feed exactly 4.
- Both-legs rule (cook_ctx.h:163-164): G1 is the flat==resident gate for the gather.

## Sequencing
Blueprint → goldens RED → flat gather → resident gather → GREEN + `run_all --bite` tooth → independent Opus
refuter → fixer → merge. SERIAL (owner-locked cook-core; not concurrent with Seam-2 resident work).
