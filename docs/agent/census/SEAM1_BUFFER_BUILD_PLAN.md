# SEAM1_BUFFER_BUILD_PLAN — buffer-marshalling keystone (照 TiXL, non-fork)

> 2026-06-29 Opus architect blueprint (read-only, re-confirmed against current main + TiXL SHA 395c4c55).
> Strategy DECIDED (柏為 2026-06-29 cook-core unblock): distinct "Buffer" port currency (raw bytes + stride),
> separate from "Points", faithful to TiXL BufferWithViews. DX11 SRV/UAV/Buffer triple → ONE MTL::Buffer.
> Workflow: this blueprint → Opus build (SPIKE first) → independent Opus refuter → fixer → orchestrator merge.

## Verified prior claims (RE-CONFIRMED in current main)
| Claim | Evidence |
|---|---|
| SwPoint = 64B MTL::Buffer currency "Points" | tixl_point.h:36-65 (static_asserts); point_graph.h:1-15 |
| host→GPU upload bridge (ListToBuffer), 64B-parity | pointlist_ops_listtobuffer.cpp:54 + driver point_graph.cpp:201-220 (memcpy contents) |
| buffer concat | point_ops_combinebuffers.cpp:42-46 (blit copyFromBuffer at offset) |
| **No "Buffer" dataType yet (greenfield)** | grep '"Buffer"' app/src/runtime/*.{cpp,h} → 0 hits |

Cook driver dispatches purely by `port.dataType` (point_graph.cpp:205,241,258,278,298,329,679,718). New "Buffer"
currency = one more flow alongside Points/PointList/Command/Texture2D/FloatList/String/Gradient/Curve.

## 1. The "Buffer" currency type
New leaf `app/src/runtime/sw_buffer.h` (~40 lines):
```
namespace sw {
struct SwBuffer {                 // == TiXL BufferWithViews collapsed for Metal
  MTL::Buffer* bytes = nullptr;   // the ONE buffer (was Buffer+Srv+Uav); driver-owned, borrowed
  uint32_t elementStride = 0;     // bytes/element (TiXL Buffer.Description.StructureByteStride)
  uint32_t elementCount  = 0;     // TiXL Srv.Description.Buffer.ElementCount = Length output
  uint32_t elementFormat = 0;     // optional tag: 0=raw/float, reserved for typed buffers
};
}  // 照 TiXL BufferWithViews.cs:5-9 — Srv/Uav folded into `bytes`
```
**Named fork `bufferwithviews-collapse-to-mtlbuffer`** — cite in every Buffer op header.

Routing: mirror the **PointList** pattern (pointlist_op_registry.h) — host-currency flow with spec sink + cook-fn
sink + `cookXNode` recursion. New leaf `app/src/runtime/buffer_op_registry.h`:
- `struct BufferCookCtx { dev/lib/queue; ctx; nodeId; const std::vector<const SwBuffer*>* inputBuffers;
  const MTL::Texture* inputTexture; const RenderCommand* inputCommand; SwBuffer* output;
  const std::map<std::string,float>* params; bool hasCamera; float matrices...; }`
- `using BufferCookFn = void(*)(BufferCookCtx&);`
- `std::vector<NodeSpec>& bufferSpecSink(); std::map<std::string,BufferCookFn>& bufferCookFns();`
- `const BufferCookFn* findBufferOp(const std::string&); bool& bufferInjectBug();`
- `struct BufferOp { BufferOp(NodeSpec, BufferCookFn); };` RAII registrar

### Cook-core edits (THE serialization risk — name every one)
1. **point_graph.cpp** — add `cookBufferNode(int)->const SwBuffer*` (clone cookPointListNode :162),
   `Impl::bufferBuf` map (clone pointListBuf), `port.dataType=="Buffer"` gather (clone :203-213),
   debug readback for goldens. (the big flow-dispatch edit)
2. **point_graph_resident.cpp** — resident mirror of cookBufferNode (S2c flat-resident parity gate;
   point_graph_cook_ctx.h:163-164 both legs must match). **Spike skips resident; production needs it.**
3. **point_graph.h** — ONE new public `debugCookedBuffer`-style accessor decl (golden readback). Only ratchet-file touch.
4. **node_registry** — add `bufferSpecSink()` to live spec aggregation (surfaces Add-menu + findSpec).
5. **selftests.h / selftests_decls.h / kTable** — register selftest rows.

## 2. Per-op port plan (all 7 = pure data marshalling, NO shader/codegen)
| Op | New file | TiXL clone | Fill | Output |
|---|---|---|---|---|
| **FloatsToBuffer** ★ | buffer_ops_floatstobuffer.cpp | FloatsToBuffer.cs:27-70 | Vec4Params(16 floats each, order .X.Y.Z.W :43-47)+float Params(:51-53) → float[floatCount+vec4Count*16]; memcpy (clone ListToBuffer point_graph.cpp:217) into StorageModeShared | SwBuffer{stride=4,count=total}; fork `floatstobuffer-const-to-shared` |
| **IntsToBuffer** | buffer_ops_intstobuffer.cpp | **port WithViews variant** IntsToBufferWithViews.cs:25-78 (not const-buffer IntsToBuffer.cs which 16B-pads). Backward-trace which GUID 208 compounds wire | int Params→int[count]; bytes=count*4 | SwBuffer{stride=4,count} |
| **GetBufferComponents** | buffer_ops_getbuffercomponents.cpp | GetBufferComponents.cs:37-86 | metadata passthrough; Length=count(:60) Stride(:61) IsValid(:45); SRV/UAV→same MTL::Buffer | Buffer + scalar Length/Stride/IsValid (value-output rail) |
| **GetSRVProperties** | buffer_ops_getsrvproperties.cpp | GetSRVProperties.cs:18-34 | ElementCount=count(:28), Buffer=same | scalar ElementCount + Buffer passthrough |
| **ExecuteBufferUpdate** | buffer_ops_executebufferupdate.cpp | ExecuteBufferUpdate.cs:15-28 | if IsEnabled(:17) cook Command input(:25) then forward Buffer unchanged(:27) — needs inputCommand recursion (clone point_graph_cook_ctx.h:146) | input SwBuffer forwarded |
| **SrvFromTexture2d** | buffer_ops_srvfromtexture2d.cpp | SrvFromTexture2d.cs:18-70 | Metal texture IS sampleable → near-noop passthrough; fork `srv-is-texture-on-metal`. **Backward-trace consumer before finalizing output dataType** | texture-handle passthrough |
| **TransformsConstBuffer** | buffer_ops_transformsconstbuffer.cpp | TransformsConstBuffer.cs:48-70 + TransformBufferLayout.cs:5-62 | **640B** (4*4*4*10) layout, 10 matrices from Camera/World/Object, each **transposed** (HLSL row-major :19-30) at offsets 0/64/../576; ping-pong _cbA/_cbB(:54-55,toggle:69); matrices via hasCamera+matrices[16] (clone fillPointCamera point_graph.cpp:359) | Buffer + PrevBuffer (ping-pong); stride=640,count=1 |

**Cut-58 backward-trace flags (implementer MUST verify vs 208 compounds):**
- IntsToBuffer has TWO GUIDs (2eb20a76… const / c036b4f2… WithViews) — trace which the compounds use.
- SrvFromTexture2d output dataType + GetSRVProperties input — decide Metal-collapsed type by downstream consumer.
- FloatsToBuffer/IntsToBuffer output-GUID f5531ffb… is SHARED (FloatsToBuffer.cs:8 / IntsToBuffer.cs:11) — don't let spec dedup confuse.

## 3. Harness-first — FIRST deliverable (the gate, byte-parity, NO TiXL render)
New `app/src/selftests_buffer.cpp` + kTable row (selftests_point.cpp:49 pattern). `--selftest-floatstobuffer` (+`-bug` RED), swept by run_all_selftests.sh --bite.
`runFloatsToBufferSelfTest`: feed Params=[1.5,2.5,3.5] + one identity Vec4Param; cook through real cookBufferNode
(build graph like runCombineBuffersSelfTest combinebuffers:96-117); read output->bytes->contents() as float*;
**assert exact bytes**: count==3+16==19; bytes[0..15]==identity in TiXL order (:43-47); bytes[16..18]==1.5,2.5,3.5;
elementStride==4. (TiXL-formula expected computed in-test, not a golden image.)
`runIntsToBufferSelfTest`: [7,8,9] → count==3, bytes==int32 7,8,9, stride==4.
**Red proof:** `bufferInjectBug()` (clone pointListInjectBug pointlist_op_registry.h:135) drops last float → real
corruption → assert fails. run_all --bite must show the tooth (no NO-BITE).

## 4. File/arch discipline
New leaves: sw_buffer.h, buffer_op_registry.h/.cpp, buffer_ops_{floatstobuffer,intstobuffer,getbuffercomponents,
getsrvproperties,executebufferupdate,srvfromtexture2d,transformsconstbuffer}.cpp, selftests_buffer.cpp.
Shared/cook-core edits (named in §1): point_graph.cpp, point_graph_resident.cpp (spike-deferrable), point_graph.h
(one accessor), node_registry, selftest registration. Each file <400 lines. check_arch must stay clean (runtime leaves, no upward deps).

## 5. Sequencing — SPIKE first (this work order)
1. sw_buffer.h + buffer_op_registry.h/.cpp (flow scaffold).
2. cookBufferNode + Impl::bufferBuf + "Buffer" gather in point_graph.cpp (**flat leg only**).
3. buffer_ops_floatstobuffer.cpp (keystone).
4. selftests_buffer.cpp::runFloatsToBufferSelfTest + -bug → **GATE: --selftest-floatstobuffer GREEN, -bug RED, run_all --bite tooth.**
5. + buffer_ops_getbuffercomponents.cpp + buffer_ops_executebufferupdate.cpp; selftest FloatsToBuffer→GetBufferComponents asserts Length==19,Stride==4.
If 1-5 pass → currency shape validated → THEN fan out IntsToBuffer/GetSRVProperties/SrvFromTexture2d/TransformsConstBuffer + resident mirror (separate work orders).

## 6. Risks/unknowns (refuter attack surface)
- IntsToBuffer GUID ambiguity (two variants) — backward-trace before porting.
- **SrvFromTexture2d / GetSRVProperties output-type collapse = softest spot** (ShaderResourceView has no clean Metal analogue); trace downstream consumer. Highest-uncertainty.
- **TransformBufferLayout transpose discipline** (TransformBufferLayout.cs:19-30 HLSL row-major) — verify transpose matches how sw shaders read the CB (metal-cpp silent-corruption risk). 640B/10-matrix offsets exact.
- Resident-leg parity deferred in spike (production path; known gap not silent).
- No shader/codegen for any of the 7 (confirmed). If a compound wires a Buffer output into a compute dispatch expecting UAV write-back → future seam (ExecuteBufferUpdate's UpdateCommand), out of marshalling scope; flag.
