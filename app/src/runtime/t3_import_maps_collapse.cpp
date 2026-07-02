// runtime/t3_import_maps_collapse — the IMAGE-FX-WRAPPER collapse tables (④a-d), split from
// t3_import_maps.cpp so both stay under the ARCHITECTURE.md rule-4 line ratchet (≤400). Pure CPU data
// (static std::maps). These feed collapseImageFxWrapper (t3_import_collapse.cpp): add an image-fx op =
// add its ④b root row + ④c fixed-slot rows + ④d FloatParams order here (data-driven, ARCHITECTURE rule 7).
// Declarations + the shared t3Lc helper live in t3_import_maps.h.
#include "runtime/t3_import_maps.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <vector>

namespace sw {

// ── IMAGE-FX COLLAPSE SEAM tables (image-fx-wrapper-collapses-to-tex-atom) ────────────────────────────
// The fx-setup framework wrapper SymbolIds (the child collapsed AWAY). Guids from the .cs [Guid]:
//   _multiImageFxSetupStatic  cc34a183  (image/fx/_/_multiImageFxSetupStatic.cs:3) — HSE's single child
//   _multiImageFxSetup        a2567844  (image/fx/_/_multiImageFxSetup.cs:3)
//   _trippleImageFxSetup      5b999887  (image/fx/_/_trippleImageFxSetup.cs:3)
bool isImageFxSetupGuid(const std::string& guid) {
  static const std::map<std::string, bool> kSet = {
      {"cc34a183-3978-4b6b-8ef1-dd8102410816", true},  // _multiImageFxSetupStatic
      {"a2567844-3314-48de-bda7-7904b5546535", true},  // _multiImageFxSetup
      {"5b999887-19df-4e91-9f58-1df2d8f1440b", true},  // _trippleImageFxSetup
  };
  return kSet.count(t3Lc(guid)) != 0;
}

// TABLE ④b: ROOT symbol guid → sw tex op type. Only roots whose .t3 is a KNOWN, SINGLE-fx-setup-child
// wrapper we can collapse 1:1 to a flat sw tex atom (ports match the op's .cs). Add an image-fx op =
// add a row here + its ④c fixed-slot rows + its ④d FloatParams order (data-driven, ARCHITECTURE rule 7).
std::string swTexOpForCollapseRootGuid(const std::string& rootGuid) {
  static const std::map<std::string, std::string> kTable = {
      // HSE (image/color/HSE.t3): the ONLY pure single-child cc34a183 wrapper. Root guid = HSE.cs [Guid].
      {"3c8003e8-70ca-4d71-9294-3df845bbb4a5", "HSE"},
      // Blend (image/use/Blend.t3): the MULTI-child generalization proof. Root = Blend.cs [Guid]. Its
      // fx-setup child's FloatParams rail is fed by helper value ops (Vector4Components×2 → ColorA/ColorB,
      // IntToFloat×3 → BlendMode/AlphaMode/ScaleMode, BoolToFloat → NormalForUpperHalf), all KEPT as real
      // sw children by the collapse (they have atoms + map rows). Proves the collapse table is not HSE-only.
      {"9f43f769-d32a-4f49-92ac-e0be3ba250cf", "Blend"},
      // BubbleZoom (image/fx/distort/BubbleZoom.t3): the GRADIENT-FED proof — root = BubbleZoom.cs [Guid].
      // Its fx-setup child's ImageB (t1) is fed by a GradientsToTexture child that renders the root's
      // FeatherGradient boundary Gradient to a texture. sw's BubbleZoom atom takes the Gradient on a PORT and
      // rasterizes it to a 1×512 row ITSELF (both raster-then-sample; NOT continuous in-shader), so the
      // SEPARATE GradientsToTexture is redundant → the collapse ELIDES the GradientsToTexture pass-through
      // (named fork gradientstotexture-elided-to-gradient-port, in t3_import_collapse.cpp): the atom's
      // ImageB fixed slot maps to the "Gradient" port (④c) and receives GradientsToTexture's Gradients
      // SOURCE. Two Vector2Components helpers (Center/GainAndBias vec2→X/Y) feed the FloatParams rail.
      {"ca3f3c1b-6f22-4bf3-b06b-d2b0d85a8881", "BubbleZoom"},
      // GRADIENT-FED image GENERATORS (image/generate/basic/*). Same wrapper shape as BubbleZoom: a
      // _multiImageFxSetupStatic (cc34a183) fx child whose ImageB (t1) is a GradientsToTexture (2c53eee7)
      // render of the root's Gradient boundary → ELIDED onto the atom's "Gradient" port. Helper decompose
      // children (Vector2/4Components, IntToFloat, BoolToFloat) all already mapped. Roots = the op .cs [Guid].
      // NGonGradient (image/generate/basic/NGonGradient.cs:[Guid]).
      {"05463270-37d4-400f-8d0d-c50f81663304", "NGonGradient"},
      // RadialGradient (RadialGradient.cs:5 [Guid]).
      {"82ad8911-c930-4851-803d-3f24422445bc", "RadialGradient"},
      // BoxGradient (BoxGradient.cs:[Guid]) — the FOUR-component-corner proof: a Vector4Components helper
      // decomposes CornersRadius vec4 into the FloatParams rail (that helper class already mapped for Blend).
      {"dc2273a7-8a54-4e6f-8d8e-9a675c1ef599", "BoxGradient"},
      // RemapColor (image/color/RemapColor.cs [Guid]) — the GRADIENT-FED COLOR op + the TRANSFORMIMAGE-
      // PASSTHROUGH proof. Same GradientsToTexture-elided-to-Gradient-port seam as the gradient trio, but the
      // GTT feeds an interposed TransformImage (identity copy, GenerateMips=true) before the fx child's ImageB
      // → that TransformImage is ALSO elided (transformimage-identity-passthrough-elided, t3_import_collapse.cpp),
      // chaining the atom's Gradient port back through it to the GTT's Gradients source. A dead bypassed
      // GenerateMips child hangs off the GTT too (elided, unconsumed). Helpers kept: IntToFloat (Mode),
      // BoolToFloat (DontColorAlpha), Vector2Components (GainAndBias) — all already mapped.
      {"da93f7d1-ef91-4b4a-9708-2d9b1baa4c14", "RemapColor"},
  };
  auto it = kTable.find(t3Lc(rootGuid));
  return it != kTable.end() ? it->second : std::string();
}

// TABLE ④c: (collapsed swType, fx-setup-child FIXED slot guid) → sw tex atom port NAME. The fx-setup
// framework's ImageA/ImageB/Output slot guids are SHARED across all wrappers (same _*FxSetup*.cs), so
// these rows are effectively per-framework, keyed by swType for clarity. FloatParams (2929c4c9) is NOT
// here — it is the positional scalar rail resolved by ④d (swFloatParamOrderForCollapse).
std::string swCollapseSlotNameForGuid(const std::string& swType, const std::string& slotGuid) {
  static const std::map<std::string, std::map<std::string, std::string>> kTable = {
      {"HSE",
       {
           {"55126bff-8c94-415d-96dd-3c16e216e663", "Image"},      // _*FxSetupStatic.ImageA  → HSE.Image
           {"0bb90f8d-88c9-4a99-b44f-f284b505c65b", "FxTexture"},  // _*FxSetupStatic.ImageB  → HSE.FxTexture
           {"76b6c677-12db-4404-aff7-ee3391d2d831", "out"},        // _*FxSetupStatic.Output  → HSE.out
       }},
      {"Blend",
       {
           {"55126bff-8c94-415d-96dd-3c16e216e663", "ImageA"},  // _*FxSetupStatic.ImageA → Blend.ImageA
           {"0bb90f8d-88c9-4a99-b44f-f284b505c65b", "ImageB"},  // _*FxSetupStatic.ImageB → Blend.ImageB
           {"76b6c677-12db-4404-aff7-ee3391d2d831", "out"},     // _*FxSetupStatic.Output → Blend.out
       }},
      {"BubbleZoom",
       {
           {"55126bff-8c94-415d-96dd-3c16e216e663", "Image"},     // _*FxSetupStatic.ImageA → BubbleZoom.Image (t0)
           // ImageB (t1) is the GRADIENT texture in TiXL; sw's BubbleZoom samples the Gradient DIRECTLY,
           // so ImageB collapses onto the "Gradient" PORT. The collapse elides the GradientsToTexture child
           // feeding this slot and re-anchors its Gradients source here (gradientstotexture-elided fork).
           {"0bb90f8d-88c9-4a99-b44f-f284b505c65b", "Gradient"},  // _*FxSetupStatic.ImageB → BubbleZoom.Gradient
           {"76b6c677-12db-4404-aff7-ee3391d2d831", "out"},       // _*FxSetupStatic.Output → BubbleZoom.out
       }},
      // GRADIENT-FED generators — identical fixed-slot mapping to BubbleZoom (ImageA→Image, ImageB→Gradient
      // via the GradientsToTexture elision, Output→out). The atoms' Image port is an OPTIONAL background;
      // unwired → generator mode (IsTextureValid=0). Same _*FxSetupStatic framework slot guids for all.
      {"NGonGradient",
       {
           {"55126bff-8c94-415d-96dd-3c16e216e663", "Image"},     // _*FxSetupStatic.ImageA → NGonGradient.Image
           {"0bb90f8d-88c9-4a99-b44f-f284b505c65b", "Gradient"},  // _*FxSetupStatic.ImageB → NGonGradient.Gradient (elided)
           {"76b6c677-12db-4404-aff7-ee3391d2d831", "out"},       // _*FxSetupStatic.Output → NGonGradient.out
       }},
      {"RadialGradient",
       {
           {"55126bff-8c94-415d-96dd-3c16e216e663", "Image"},     // _*FxSetupStatic.ImageA → RadialGradient.Image
           {"0bb90f8d-88c9-4a99-b44f-f284b505c65b", "Gradient"},  // _*FxSetupStatic.ImageB → RadialGradient.Gradient (elided)
           {"76b6c677-12db-4404-aff7-ee3391d2d831", "out"},       // _*FxSetupStatic.Output → RadialGradient.out
       }},
      {"BoxGradient",
       {
           {"55126bff-8c94-415d-96dd-3c16e216e663", "Image"},     // _*FxSetupStatic.ImageA → BoxGradient.Image
           {"0bb90f8d-88c9-4a99-b44f-f284b505c65b", "Gradient"},  // _*FxSetupStatic.ImageB → BoxGradient.Gradient (elided)
           {"76b6c677-12db-4404-aff7-ee3391d2d831", "out"},       // _*FxSetupStatic.Output → BoxGradient.out
       }},
      // RemapColor — same _*FxSetupStatic framework slots. ImageA→Image (background, generator dummy when
      // unwired); ImageB→Gradient via the GTT→TransformImage double-elision; Output→out.
      {"RemapColor",
       {
           {"55126bff-8c94-415d-96dd-3c16e216e663", "Image"},     // _*FxSetupStatic.ImageA → RemapColor.Image
           {"0bb90f8d-88c9-4a99-b44f-f284b505c65b", "Gradient"},  // _*FxSetupStatic.ImageB → RemapColor.Gradient (GTT+TransformImage elided)
           {"76b6c677-12db-4404-aff7-ee3391d2d831", "out"},       // _*FxSetupStatic.Output → RemapColor.out
       }},
  };
  auto t = kTable.find(swType);
  if (t == kTable.end()) return std::string();
  auto s = t->second.find(t3Lc(slotGuid));
  return s != t->second.end() ? s->second : std::string();
}

// TABLE ④d: for a collapsed swType, the ORDERED sw scalar port names that the fx-setup FloatParams
// MultiInput (2929c4c9) wires land on positionally. HSE.t3 wires Hue,Saturation,Exposure IN THAT ORDER
// into FloatParams (verified against HSE.t3 Connections + HueShift.hlsl cbuffer {Hue,Saturation,Exposure}).
const std::vector<std::string>& swFloatParamOrderForCollapse(const std::string& swType) {
  static const std::map<std::string, std::vector<std::string>> kTable = {
      {"HSE", {"Hue", "Saturation", "Exposure"}},
      // Blend FloatParams rail (verified against Blend.t3 wire order + Blend.cs input order + guids):
      // ColorA(Vec4C).xyzw, ColorB(Vec4C).xyzw, then BlendMode, AlphaMode, NormalForUpperHalf, ScaleMode
      // (the int/bool scalars, fed through IntToFloat/BoolToFloat helpers). Matches Blend atom port ids.
      {"Blend",
       {"ColorA.x", "ColorA.y", "ColorA.z", "ColorA.w",
        "ColorB.x", "ColorB.y", "ColorB.z", "ColorB.w",
        "BlendMode", "AlphaMode", "NormalForUpperHalf", "ScaleMode"}},
      // BubbleZoom FloatParams rail — verified against BubbleZoom.t3 wire order into 2929c4c9 AND the
      // BubbleZoom.hlsl b0 cbuffer ParamConstants {Center.xy, ScaleFactor, Feather, Radius, GainAndBias.xy,
      // FlipEffect}. Center/GainAndBias arrive as Vector2Components X/Y pairs; Magnify→ScaleFactor (sw port
      // name Magnify, [fork-magnify-rename] in point_ops_bubblezoom.cpp). Matches the sw BubbleZoom port ids.
      {"BubbleZoom",
       {"Center.x", "Center.y", "Magnify", "Feather", "Radius",
        "GainAndBias.x", "GainAndBias.y", "FlipEffect"}},
      // NGonGradient FloatParams rail — verified against NGonGradient.t3 wire order into 2929c4c9 AND the
      // NGonGradient.hlsl b0 cbuffer {Position.xy, Sides, Radius, Curvature, Blades, Roundness, Rotate,
      // Width, Offset, PingPong, Repeat, BlendMode, GainAndBias.xy}. Position/BiasAndGain arrive as
      // Vector2Components X/Y pairs; PingPong/Repeat via BoolToFloat; BlendMode via IntToFloat. sw port name
      // for the GainAndBias cbuffer field is "BiasAndGain" (the .cs input name; the rename trap — matches
      // point_ops_ngongradient.cpp port ids). 15 scalars.
      {"NGonGradient",
       {"Position.x", "Position.y", "Sides", "Radius", "Curvature", "Blades", "Roundness", "Rotate",
        "Width", "Offset", "PingPong", "Repeat", "BlendMode", "BiasAndGain.x", "BiasAndGain.y"}},
      // RadialGradient FloatParams rail — verified against RadialGradient.t3 wire order into 2929c4c9 AND the
      // RadialGradient.hlsl b0 cbuffer {Center.xy, Width, Offset, PingPong, Repeat, PolarOrientation,
      // BlendMode, GainAndBias.xy, Stretch.xy, Noise}. Center/BiasAndGain/Stretch arrive as Vector2Components
      // X/Y; PingPong/Repeat/PolarOrientation via BoolToFloat; BlendMode via IntToFloat. sw port name for the
      // GainAndBias cbuffer field is "BiasAndGain" (rename trap — matches point_ops_radialgradient.cpp). 13 scalars.
      {"RadialGradient",
       {"Center.x", "Center.y", "Width", "Offset", "PingPong", "Repeat", "PolarOrientation", "BlendMode",
        "BiasAndGain.x", "BiasAndGain.y", "Stretch.x", "Stretch.y", "Noise"}},
      // BoxGradient FloatParams rail — verified against BoxGradient.t3 wire order into 2929c4c9 AND the
      // BoxGradient.hlsl b0 cbuffer {Center.xy, Size.xy, CornersRadius.xyzw, Rotation, UniformScale, Width,
      // Offset, PingPong, Repeat, GainAndBias.xy, BlendMode}. Center/Size/GainAndBias via Vector2Components;
      // CornersRadius via a Vector4Components (its X/Y/Z/W outputs feed the 4 CornersRadius scalar slots, the
      // .t3 wiring order scrambled on the SOURCE side but the atom-port order here is cbuffer order); PingPong/
      // Repeat via BoolToFloat; BlendMode via IntToFloat. sw port for the cbuffer Width field is "GradientWidth"
      // (the .cs input name; matches point_ops_boxgradient.cpp port ids). 17 scalars.
      {"BoxGradient",
       {"Center.x", "Center.y", "Size.x", "Size.y",
        "CornersRadius.x", "CornersRadius.y", "CornersRadius.z", "CornersRadius.w",
        "Rotation", "UniformScale", "GradientWidth", "Offset", "PingPong", "Repeat",
        "GainAndBias.x", "GainAndBias.y", "BlendMode"}},
      // RemapColor FloatParams rail — verified against RemapColor.t3 wire order into 2929c4c9 AND the
      // ColorRemap.hlsl b0 cbuffer {DontColorAlpha, Mode, Offset, Exposure, GainAndBias.xy, Repeat}.
      // DontColorAlpha via BoolToFloat; Mode via IntToFloat; GainAndBias via Vector2Components; Cycle/
      // Exposure/Repeat straight from the boundary. sw port for the cbuffer Offset field is "Cycle" (the
      // .cs input name — [fork-offset-from-cycle], matches point_ops_remapcolor.cpp port ids). 7 scalars.
      {"RemapColor",
       {"DontColorAlpha", "Mode", "Cycle", "Exposure", "GainAndBias.x", "GainAndBias.y", "Repeat"}},
  };
  static const std::vector<std::string> kEmpty;
  auto it = kTable.find(swType);
  return it != kTable.end() ? it->second : kEmpty;
}

}  // namespace sw
