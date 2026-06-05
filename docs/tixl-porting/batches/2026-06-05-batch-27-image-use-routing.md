# Batch 27 Image Use Routing Acceptance Matrix

Scope: four Texture2D routing nodes from `Operators/Lib/image/use`: `FirstValidTexture`, `PickTexture`, `SwapTextures`, and `UseFallbackTexture`.

Gate: every row needs TiXL source/default evidence, semantic edge cases, Vuo C source, installation into the Vuo user module folder, and a Vuo-visible proof that consumes the exact `my_<TiXLName>` node output.

Vuo body-layer note: TiXL `MultiInputSlot<Texture2D>` is represented as three bounded `VuoImage` inputs plus `inputCount`. Previous-value behavior is exposed through explicit `previousOutput` / `previousSelected` image inputs because Vuo does not provide TiXL slot state in a stateless node body. `UseFallbackTexture` preserves the visible TextureA-or-Fallback law; TiXL dirty-flag fallback caching is documented as a bounded adapter limit.

| TiXL node | grade | Vuo title | Vuo source | source evidence | semantic/source tests | Vuo visual proof | status |
|---|---|---|---|---|---|---|---|
| `Lib.image.use.FirstValidTexture` | C body adapter | `my_FirstValidTexture` | `vuo-nodes/my.image.use.firstValidTexture.c` | C# `external/tixl/Operators/Lib/image/use/FirstValidTexture.cs`; `.t3` `external/tixl/Operators/Lib/image/use/FirstValidTexture.t3` | `tests/tixl_batch27_image_use_routing_semantics.test.js`; `tests/tixl_batch27_image_use_routing_vuo_nodes.test.js` | `vuo-compositions/generated/myworld-batch-27-image-use-routing-proof.vuo` -> first band red from `input2` after null `input1`; Vuo-saved image `artifacts/vuo_cli/batch-27-image-use-routing-vuo-save.png` | done |
| `Lib.image.use.PickTexture` | C body adapter | `my_PickTexture` | `vuo-nodes/my.image.use.pickTexture.c` | C# `external/tixl/Operators/Lib/image/use/PickTexture.cs`; `.t3` `external/tixl/Operators/Lib/image/use/PickTexture.t3` | `tests/tixl_batch27_image_use_routing_semantics.test.js`; `tests/tixl_batch27_image_use_routing_vuo_nodes.test.js` | same proof -> second band blue from `index=-1` positive modulo | done |
| `Lib.image.use.SwapTextures` | C body adapter | `my_SwapTextures` | `vuo-nodes/my.image.use.swapTextures.c` | C# `external/tixl/Operators/Lib/image/use/SwapTextures.cs`; `.t3` `external/tixl/Operators/Lib/image/use/SwapTextures.t3` | `tests/tixl_batch27_image_use_routing_semantics.test.js`; `tests/tixl_batch27_image_use_routing_vuo_nodes.test.js` | same proof -> third/fourth bands green/red from `EnableSwap=true` | done |
| `Lib.image.use.UseFallbackTexture` | C body adapter | `my_UseFallbackTexture` | `vuo-nodes/my.image.use.useFallbackTexture.c` | C# `external/tixl/Operators/Lib/image/use/UseFallbackTexture.cs`; `.t3` `external/tixl/Operators/Lib/image/use/UseFallbackTexture.t3` | `tests/tixl_batch27_image_use_routing_semantics.test.js`; `tests/tixl_batch27_image_use_routing_vuo_nodes.test.js` | same proof -> fifth band blue from fallback when `TextureA` is null | done |

## Batch Notes

- `my_FirstValidTexture` preserves TiXL order scan and previous-output fallback when no valid texture exists. Bounded Vuo inputs: `input1`, `input2`, `input3`, `inputCount`, `previousOutput`.
- `my_PickTexture` preserves TiXL positive modulo behavior, including negative indices. When `inputCount=0`, it outputs `previousSelected`.
- `my_SwapTextures` preserves TiXL default pass-through and `EnableSwap=true` swap behavior.
- `my_UseFallbackTexture` preserves visible `TextureA ?? Fallback` routing. TiXL's cached fallback dirty-flag behavior is a Vuo body-layer limit because this node body has no TiXL `DirtyFlag`.
- Vuo CLI proof `batch-27-image-use-routing-proof` compiled and linked with return code `0`, loaded all four manufactured nodes plus `my.image.batch.batch27ImageUseRoutingProof`, opened an onscreen runner window, and produced Vuo-rendered PNG evidence at `artifacts/vuo_cli/batch-27-image-use-routing-vuo-save.png`.
- Per current acceptance rule, macOS window screenshot capture is skipped as a known permission-layer issue. Vuo-native saved PNG is the visual proof: `640x160`, average luma `0.335115`, bright ratio `1.0`, `mostlyBlack=false`.
