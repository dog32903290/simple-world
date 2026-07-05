// runtime/node_registry_draw_render — NodeSpec rows for the RENDER family: the point/mesh/screen draw
// ops that emit render Commands (DrawPoints/DrawLines/…/DrawMeshUnlit/DrawScreenQuad/ClearRenderTarget),
// the RenderTarget resolution pin, and the SetRequestedResolution override. Peeled out of
// node_registry_draw.cpp so the render lane can extend this family without touching drawSpecs()'s shared
// table (parallel-lane peel — no merge conflict with camera/flow/data lanes). These rows moved VERBATIM
// from node_registry_draw.cpp; table order unchanged (drawSpecs() appends this sub-table in source order).
#include "runtime/node_registry_draw.h"
#include "runtime/graph.h"

namespace sw {

const std::vector<NodeSpec>& drawRenderSpecs() {
  static const std::vector<NodeSpec> specs = {
      // DrawPoints (TiXL Lib.point.draw.DrawPoints → DrawPoints.hlsl): points bag in -> a render
      // Command out (wires into a RenderTarget). Draws each Point as a screen-facing QUAD SPRITE sized
      // by PointSize (the SAME DrawPoints.hlsl shader as DrawPoints2; sw rides DrawKind::Points2 — see
      // cookDrawPoints). Params mirror DrawPoints.t3 DefaultValue: PointSize=0.1, Color(Vec4)=white,
      // ScaleFactor=None(0) (F1 → scale sprite by W=FX1), BlendMode=Normal(0). FORK (named): camera/
      // Transforms/Fog/FadeNearest/AlphaCutOff/EnableZWrite/EnableZTest + the Texture_ sprite atlas
      // (asset-bind seam not built → flat square, no round-dot mask) + ColorField + UsePointsScale's
      // per-point Scale.xy stretch (draw_points2.metal doesn't read Scale.xy, same fork as DrawPoints2)
      // are dropped (sw's camera-less baked-ortho fork class).
      {"DrawPoints", "DrawPoints",
       {{"points", "points", "Points", true},
        {"out", "out", "Command", false},
        {"PointSize", "PointSize", "Float", true, 0.1f, 0.0f, 10.0f},
        {"Color.x", "Color", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"Color.y", "Color.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.z", "Color.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.w", "Color.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"ScaleFactor", "ScaleFactor", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum,
         {"None", "F1", "F2"}, true},
        {"BlendMode", "BlendMode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
         {"Normal", "Additive"}, true}},
       nullptr,
       "point.draw"},
      // DrawLines (TiXL Lib.point.draw.DrawLines): connects the point bag into a polyline —
      // Points[i]→Points[i+1], each segment a screen-space-thickened quad (draw_lines.metal).
      // Points in → Command out (same cmd flow as DrawPoints). Params mirror DrawLines.t3:
      // Color (Vec4, white) + LineWidth (0.02). W(FX1)=NaN breaks the polyline — forward
      // parity: no production op writes the NaN separator yet (fork named in draw_lines.metal).
      // FORK (named): TiXL's camera/texture/UV/ShrinkWithDistance/Fog/Blend/ZTest are dropped
      // (no camera system — DrawPoints' baked-ortho fork class); flat untextured band.
      {"DrawLines", "DrawLines",
       {{"points", "points", "Points", true},
        {"out", "out", "Command", false},
        {"Color.x", "Color", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"Color.y", "Color.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.z", "Color.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.w", "Color.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"LineWidth", "LineWidth", "Float", true, 0.02f, 0.0f, 1.0f}},
       nullptr,
       "point.draw"},
      // DrawClosedLines (TiXL Lib.point.draw.DrawClosedLines → DrawLinesAlt.hlsl): the closed-loop
      // sibling of DrawLines — connects the bag into a polyline AND wraps the last point back to the
      // first (Points[last]→Points[0]), closing each shape. PointsPerShape>0 splits the bag into
      // closed shapes of that many points (.t3 default 0 = one shape over all points). Points in →
      // Command out (same cmd flow + DrawKind::Lines shader as DrawLines; only lineClosed differs).
      // Params mirror DrawClosedLines.t3: Color (Vec4 white) + LineWidth (0.02) + PointsPerShape (0).
      // FORK (named): inherits DrawLines' band fork class (no camera/Transforms/Fog/UV/texture/miter);
      // LineOffset/WidthFactor/ShrinkWithDistance/TransitionProgress/Blend/ZTest/ZWrite dropped.
      {"DrawClosedLines", "DrawClosedLines",
       {{"points", "points", "Points", true},
        {"out", "out", "Command", false},
        {"Color.x", "Color", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"Color.y", "Color.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.z", "Color.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.w", "Color.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"LineWidth", "LineWidth", "Float", true, 0.02f, 0.0f, 1.0f},
        {"PointsPerShape", "PointsPerShape", "Float", true, 0.0f, 0.0f, 100000.0f}},
       nullptr,
       "point.draw"},
      // DrawPoints2 (TiXL Lib.point.draw.DrawPoints2 → DrawPoints.hlsl Radius variant): the
      // Radius-driven DrawPoints — draws the bag as screen-facing quad sprites sized by Radius (the
      // .t3 routes Radius → ×10.8 → the shader PointSize), optionally scaled per-point by W (FX1,
      // UseWForSize). Points in → Command out (DrawKind::Points2 — its own shader, v1 DrawPoints
      // untouched). Params mirror DrawPoints2.t3: Color (Vec4 white) + Radius (0.01) + UseWForSize (1).
      // FORK (named): camera/Transforms/Fog/FadeNearest/BlendMode/ZTest/ZWrite + the Texture_ sprite
      // atlas (asset-bind seam not built → omitted, flat square sprite) dropped (no camera system).
      {"DrawPoints2", "DrawPoints2",
       {{"points", "points", "Points", true},
        {"out", "out", "Command", false},
        {"Color.x", "Color", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"Color.y", "Color.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.z", "Color.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.w", "Color.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Radius", "Radius", "Float", true, 0.01f, 0.0f, 10.0f},
        {"UseWForSize", "UseWForSize", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool}},
       nullptr,
       "point.draw"},
      // DrawLinesBuildup (TiXL Lib.point.draw.DrawLinesBuildup → DrawLinesBuildup.hlsl): DrawLines'
      // polyline with a progressive W-reveal — TransitionProgress sweeps a VisibleRange-wide visible
      // window along the line (each point's W=FX1 is the reveal coord). Points in → Command out
      // (DrawKind::LinesBuildup — its own shader, DrawLines/DrawClosedLines untouched). Params mirror
      // DrawLinesBuildup.t3: Color (Vec4 white) + LineWidth (0.02) + TransitionProgress (0.5) +
      // VisibleRange (0.5). FORK (named): camera/Transforms/ShrinkWithDistance/Fog/miter/BlendMode/
      // ZTest/ZWrite + the Texture_ sample (white-pixel no-op) + Color×ForegroundColor theme coupling
      // dropped (DrawLines fork class); Texture_ port deferred (asset-bind seam not built).
      {"DrawLinesBuildup", "DrawLinesBuildup",
       {{"points", "points", "Points", true},
        {"out", "out", "Command", false},
        {"Color.x", "Color", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"Color.y", "Color.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.z", "Color.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.w", "Color.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"LineWidth", "LineWidth", "Float", true, 0.02f, 0.0f, 1.0f},
        {"TransitionProgress", "TransitionProgress", "Float", true, 0.5f, 0.0f, 1.0f},
        {"VisibleRange", "VisibleRange", "Float", true, 0.5f, 0.0f, 1.0f}},
       nullptr,
       "point.draw"},
      // DrawBillboards (TiXL Lib.point.draw.DrawBillboards): expands each Point into a
      // screen-facing quad sprite (draw_billboards.metal). Points in → Command out. Params mirror
      // DrawBillboards.t3: Scale (1.0) + Color (Vec4, white); per-point Scale.xy stretch kept
      // (UsePointScale default true). FORK (named): camera/atlas/sprite-texture/scatter/rotation/
      // curves dropped (no camera system); flat untextured screen-facing quad.
      {"DrawBillboards", "DrawBillboards",
       {{"points", "points", "Points", true},
        {"out", "out", "Command", false},
        {"Scale", "Scale", "Float", true, 1.0f, 0.0f, 1000.0f},
        {"Color.x", "Color", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"Color.y", "Color.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.z", "Color.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.w", "Color.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1}},
       nullptr,
       "point.draw"},
      // DrawScreenQuad (TiXL Lib.render.basic.DrawScreenQuad): a textured fullscreen quad — samples
      // a Texture2D input, tints by Color, sizes/places by Width/Height/Position, composites by
      // BlendMode. Texture2D in → Command out. The 19 dx11 sub-ops in DrawScreenQuad.t3 collapse into
      // ONE Metal render pass (the executor's DrawKind::ScreenQuad case). Params mirror
      // DrawScreenQuad.cs defaults. FORK (named): NO camera (ObjectToClipSpace is commented out in
      // vs-draw-viewport-quad.hlsl — clean leaf); EnableDepthTest/Write + Filter dropped (no
      // depth-stencil seam yet); BlendMode shipped = Normal+Additive only (Multiply/Invert/... → Normal).
      {"DrawScreenQuad", "DrawScreenQuad",
       {{"Texture", "Texture", "Texture2D", true},
        {"out", "out", "Command", false},
        {"Color.x", "Color", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"Color.y", "Color.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.z", "Color.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.w", "Color.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Width", "Width", "Float", true, 1.0f, 0.0f, 100.0f},
        {"Height", "Height", "Float", true, 1.0f, 0.0f, 100.0f},
        {"BlendMode", "BlendMode", "Float", true, 0.0f, 0.0f, 8.0f, Widget::Enum,
         {"Normal", "Additive", "Multiply", "Invert", "None", "PreMultiplied", "BlendOnWhite",
          "BlendOnWhite01", "UseImageAlpha"}, true},
        {"Position.x", "Position", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 2},
        {"Position.y", "Position.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1}},
       nullptr,
       "render.basic"},
      // ClearRenderTarget (TiXL Lib.render._dx11.api.ClearRenderTarget): a chain-clear directive —
      // Command out (no input). Maps to the executor's pass clear color when it is the FIRST chain
      // item (faithful + free; the retained-mode pass clears once via LoadActionClear). Proves
      // Command-chain ordering with a 2nd op type. FORK (named): the DSV depth clear is dropped (no
      // depth-stencil seam); a non-first mid-chain re-clear (clear-quad) is deferred.
      {"ClearRenderTarget", "ClearRenderTarget",
       {{"out", "out", "Command", false},
        {"ClearColor.x", "ClearColor", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"ClearColor.y", "ClearColor.y", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"ClearColor.z", "ClearColor.z", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"ClearColor.w", "ClearColor.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1}},
       nullptr,
       "render._dx11.api"},
      // DrawMeshUnlit (TiXL Lib.mesh.draw.DrawMeshUnlit): the FIRST 3D mesh — a depth-tested,
      // genuinely-unlit triangle mesh (mesh-DrawUnlit.hlsl; psMain default = albedo(white)·Color = Color).
      // Mesh in → Command out (DrawKind::Mesh). The executor attaches a Depth32Float buffer and draws it
      // LessEqual/ZWrite/CCW-front/Cull-Back (TiXL DepthStencilState 61714c96 + Rasterizer 6e672779).
      // Color (Vec4, .t3 default white) is the only param shipped v1. FORKS (named): in-code 1×1 white t2
      // (no Texture input → byte-identical white.png albedo); BlendMode/FillMode/Cull/AlphaCutOff/BlurLevel/
      // UseVertexColor/UseCubeMap/Texture deferred (defaults only); DrawMesh (the PBR variant) deferred.
      {"DrawMeshUnlit", "DrawMeshUnlit",
       {{"Mesh", "Mesh", "Mesh", true},
        {"out", "out", "Command", false},
        {"Color.x", "Color", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"Color.y", "Color.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.z", "Color.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.w", "Color.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1}},
       nullptr,
       "mesh.draw"},
      // RenderTarget (TiXL Lib.image.generate.basic.RenderTarget): executes a Command chain into a
      // sized Texture2D — the RESOLUTION PIN. Command in, Texture2D out; Resolution enum picks the
      // output size (WindowFollow tracks the viewport, fixed modes pin a standard size, Custom reads
      // CustomW/H); ClearColor is the background. See docs/runtime/RENDER_TARGET_CONTRACT.md.
      {"RenderTarget", "RenderTarget",
       {{"command", "command", "Command", true},
        {"out", "out", "Texture2D", false},
        {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
         {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
        {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
        {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f},
        {"ClearColor.x", "ClearColor", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"ClearColor.y", "ClearColor.y", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"ClearColor.z", "ClearColor.z", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"ClearColor.w", "ClearColor.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        // TextureReference (RenderTarget.cs:684 InputSlot<RenderTargetReference>, the TiXL "wireless"
        // texture link): wire a UseTextureReference.Reference here → after this RenderTarget cooks,
        // BOTH tex walkers publish its output texture under that node's key (RenderTarget.cs:143-148
        // reference.ColorTexture = ColorTexture) and UseTextureReference.Texture serves it anywhere in
        // the graph. dataType "TexRef" is a MARKER wire (no cooked value travels; no gather reads it).
        {"TextureReference", "TextureReference", "TexRef", true}},
       nullptr,
       "image.generate.basic"},
      // SetRequestedResolution (TiXL Lib.render.shading.SetRequestedResolution): the EXPLICIT override
      // op of the S1 output-resolution seam. Wraps a Command subtree; while cooking it, pushes
      // context.RequestedResolution = new Int2(Width or current, Height or current) * Multiply, clamped
      // [1,16384], then restores (SetRequestedResolution.cs:18-28 save/set/cook-child/restore). The
      // PUSH happens in the cook driver (cookCommand) BEFORE the subtree cooks — the op cook itself just
      // forwards the subtree's items (the driver owns the push because the subtree is cooked there).
      // Width/Height default 0 = "use the current RequestedResolution" (so a bare Multiply scales the
      // ambient size); Multiply default 1. Command in → Command out.
      {"SetRequestedResolution", "SetRequestedResolution",
       {{"command", "command", "Command", true},
        {"out", "out", "Command", false},
        {"Width", "Width", "Float", true, 0.0f, 0.0f, 16384.0f},
        {"Height", "Height", "Float", true, 0.0f, 0.0f, 16384.0f},
        {"Multiply", "Multiply", "Float", true, 1.0f, 0.0f, 16.0f}},
       nullptr,
       "render.shading"},
      // SetRequestedResolutionCmd (TiXL Lib.flow.context.SetRequestedResolutionCmd): the flow-rail sibling
      // of SetRequestedResolution — same Command-subtree RequestedResolution push, but the factor is
      // ScaleResolution × StretchResolution (Vector2) instead of a single Multiply. The push happens in the
      // cook driver (resolveSetRequestedResolutionCmd) BEFORE the subtree cooks; this op forwards the items.
      // Width/Height (Int2 Resolution) default 0 + StretchResolution default (1,1): the cs:24 gate needs ALL
      // FOUR >0 to adopt Resolution, else it scales the ambient size (a bare ScaleResolution). Cmd in → out.
      {"SetRequestedResolutionCmd", "SetRequestedResolutionCmd",
       {{"Texture", "Texture", "Command", true},
        {"out", "out", "Command", false},
        {"Width", "Width", "Float", true, 0.0f, 0.0f, 16384.0f},
        {"Height", "Height", "Float", true, 0.0f, 0.0f, 16384.0f},
        {"StretchResolution.x", "StretchResolution", "Float", true, 1.0f, 0.0f, 16.0f, Widget::Vec, {}, true, 2},
        {"StretchResolution.y", "StretchResolution.y", "Float", true, 1.0f, 0.0f, 16.0f, Widget::Vec, {}, true, 1},
        {"ScaleResolution", "ScaleResolution", "Float", true, 1.0f, 0.0f, 16.0f}},
       nullptr,
       "flow.context"},
      // SpreadIntoGrid (TiXL Lib.render.transform.SpreadIntoGrid): MultiInput Command layout — child i
      // is TRANSLATED to grid cell (i%gx, i/gx, i/(gx·gy)) scaled by Spread·SpreadScale (per-child
      // ObjectToWorld push, SpreadIntoGrid.cs:33-66; cooked via the per-wire group stamp,
      // point_ops_spreadintogrid.cpp). Params mirror SpreadIntoGrid.t3: Spread=(1,1,1), SpreadScale=1,
      // GridSize=(3,3,1) (Int3 → 3 Floats, sw vec-as-N-floats). Single child ⇒ spread zeroed (cs:20-21).
      {"SpreadIntoGrid", "SpreadIntoGrid",
       {{"Commands", "Commands", "Command", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, true},
        {"out", "out", "Command", false},
        {"Spread.x", "Spread", "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
        {"Spread.y", "Spread.y", "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Spread.z", "Spread.z", "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"SpreadScale", "SpreadScale", "Float", true, 1.0f, 0.0f, 10.0f},
        {"GridSize.x", "GridSize", "Float", true, 3.0f, 1.0f, 100.0f, Widget::Vec, {}, true, 3},
        {"GridSize.y", "GridSize.y", "Float", true, 3.0f, 1.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"GridSize.z", "GridSize.z", "Float", true, 1.0f, 1.0f, 100.0f, Widget::Vec, {}, true, 1}},
       nullptr,
       "render.transform"},
      // SpreadLayout (TiXL Lib.render.transform.SpreadLayout): Group's SRT push with the TRANSLATION
      // spread per child along Spread — child i at Translation − Spread·f_i, f_i = (0.5−(i/(count−1)
      // −0.5))−Pivot (SpreadLayout.cs:55-93; per-wire group stamp, point_ops_spreadlayout.cpp). Params
      // mirror SpreadLayout.t3: Spread/Translation/Rotation=(0,0,0), Scale=(1,1,1), UniformScale=1,
      // Pivot=0.5, IsEnabled=true. FORK (named, Group fork class): Color foreground-tint +
      // ForceColorUpdate dropped (S3 shading-context concern; .t3 Color default white = parity no-op).
      {"SpreadLayout", "SpreadLayout",
       {{"Commands", "Commands", "Command", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, true},
        {"out", "out", "Command", false},
        {"Spread.x", "Spread", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
        {"Spread.y", "Spread.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Spread.z", "Spread.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Translation.x", "Translation", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
        {"Translation.y", "Translation.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Translation.z", "Translation.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Rotation.x", "Rotation", "Float", true, 0.0f, -180.0f, 180.0f, Widget::Vec, {}, true, 3},
        {"Rotation.y", "Rotation.y", "Float", true, 0.0f, -180.0f, 180.0f, Widget::Vec, {}, true, 1},
        {"Rotation.z", "Rotation.z", "Float", true, 0.0f, -180.0f, 180.0f, Widget::Vec, {}, true, 1},
        {"Scale.x", "Scale", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Scale.y", "Scale.y", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Scale.z", "Scale.z", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"UniformScale", "UniformScale", "Float", true, 1.0f, 0.0f, 10.0f},
        {"Pivot", "Pivot", "Float", true, 0.5f, -1.0f, 2.0f},
        {"IsEnabled", "IsEnabled", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool, {}, true}},
       nullptr,
       "render.transform"},
      // GetScreenPos (TiXL Lib.render.analyze.GetScreenPos): projects a WORLD position through the
      // ambient camera into screen space (GetScreenPos.cs:17-53) — Command SOURCE (UpdateCommand runs
      // the projection inside the render eval, no draw items) + Position.x/y/z on the VALUE rail via
      // the cross-rail latch (point_ops_getscreenpos.cpp; evaluate=nullptr → resident extOut, the
      // stateful-value sink fills extOut[1..3] = these port indices — Position ports MUST stay at
      // spec indices 1..3). Params mirror GetScreenPos.t3: LocalPosition=(0,0,0), SetDepthToZero=true.
      // FORKS named in the op leaf (frame-latch / single-latch / identity-O2W / perspective-only).
      {"GetScreenPos", "GetScreenPos",
       {{"UpdateCommand", "UpdateCommand", "Command", false},
        {"Position.x", "Position.x", "Float", false},
        {"Position.y", "Position.y", "Float", false},
        {"Position.z", "Position.z", "Float", false},
        {"LocalPosition.x", "LocalPosition", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"LocalPosition.y", "LocalPosition.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"LocalPosition.z", "LocalPosition.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"SetDepthToZero", "SetDepthToZero", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool, {}, true}},
       nullptr,
       "render.analyze"},
      // GpuMeasure (TiXL Lib.render.analyze.GpuMeasure): times the GPU duration of a wrapped Command subtree
      // (GpuMeasure.cs:31-80 — D3D timestamp queries in TiXL; sw uses MTLCommandBuffer.GPUEndTime−GPUStartTime,
      // the whole-buffer analog). Command PASSTHROUGH (forwards the subtree items) + LastMeasureInMs (smoothed,
      // cs:74) / LastMeasureInMicroSeconds (raw, cs:70) on the VALUE rail via the cross-rail latch
      // (point_ops_gpumeasure.cpp; evaluate=nullptr → the stateful-value sink fills extOut[1..2] = these port
      // indices — value ports MUST stay at spec indices 1..2, the GetScreenPos contract). SMOKE-level golden
      // (no closed-form GPU time). Params mirror GpuMeasure.t3: Enabled=true, LogToConsole=false.
      {"GpuMeasure", "GpuMeasure",
       {{"Output", "Output", "Command", false},
        {"LastMeasureInMs", "LastMeasureInMs", "Float", false},
        {"LastMeasureInMicroSeconds", "LastMeasureInMicroSeconds", "Float", false},
        {"Command", "Command", "Command", true},
        {"Enabled", "Enabled", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"LogToConsole", "LogToConsole", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true}},
       nullptr,
       "render.analyze"},
      // SliceViewPort (TiXL Lib.render.transform.SliceViewPort): renders a Command SubGraph into ONE grid cell
      // — a per-cell sub-viewport rect + a RepeatView CameraToClipSpace M11/M22 scale, with RequestedResolution
      // pushed to the cell size (SliceViewPort.cs:24-114). Command PASSTHROUGH that STAMPS the viewport rect +
      // clip scale onto every subtree item (point_ops_sliceviewport.cpp; the Camera/Group per-item stamp
      // precedent); the driver pushes the cell RequestedResolution around the subtree cook (both cook legs).
      // v1 FORK fork-sliceviewport-repeatview-only: Mode=RepeatView (M11/M22 scale); SliceView/FitProjection
      // crop offsets deferred. .t3 defaults: CellIndex=0, CellCounts=(2,2), Stretch=(1,1), Mode=RepeatView(0).
      {"SliceViewPort", "SliceViewPort",
       {{"Output", "Output", "Command", false},
        {"SubGraph", "SubGraph", "Command", true},
        {"CellIndex", "CellIndex", "Float", true, 0.0f, 0.0f, 10000.0f, Widget::Slider},
        {"CellCounts.x", "CellCounts", "Float", true, 2.0f, 1.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"CellCounts.y", "CellCounts.y", "Float", true, 2.0f, 1.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Stretch.x", "Stretch", "Float", true, 1.0f, 0.0f, 2.0f, Widget::Vec, {}, false, 2},
        {"Stretch.y", "Stretch.y", "Float", true, 1.0f, 0.0f, 2.0f, Widget::Vec, {}, false, 1},
        {"Mode", "Mode", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum,
         {"RepeatView", "SliceView", "FitProjection"}, true}},
       nullptr,
       "render.transform"},
  };
  return specs;
}

}  // namespace sw
