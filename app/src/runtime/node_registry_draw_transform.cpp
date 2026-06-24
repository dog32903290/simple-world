// runtime/node_registry_draw_transform — NodeSpec rows for the render-island TRANSFORM-CONTEXT ops
// (RotateAroundAxis / Shear / Transform / RotateTowards). Peeled out of node_registry_draw.cpp to keep that
// file under the 400-line ratchet (ARCHITECTURE rule 4) when RotateTowards landed. These four ops are a
// cohesive family: each wraps a single Command subtree and pushes a transform onto context.ObjectToWorld via
// the Group per-item group-stamp (render_command.h hasGroup/groupObjectToWorld) — no SRT/feedback/seam. The
// cook leaves are point_ops_{rotatearoundaxis,shear,transform,rotatetowards}.cpp; this is only their UI spec.
//
// Appended into drawSpecs() (node_registry_draw.cpp) in source order — the table order is unchanged from
// before the peel.
#include "runtime/node_registry_draw.h"
#include "runtime/graph.h"

namespace sw {

const std::vector<NodeSpec>& drawTransformSpecs() {
  static const std::vector<NodeSpec> specs = {
      // RotateAroundAxis (TiXL Lib.render.transform.RotateAroundAxis): wraps a Command subtree and pushes
      // ONE axis-angle rotation onto context.ObjectToWorld (Matrix4x4.CreateFromAxisAngle(Axis, Angle°)).
      // Command in → Command out (the op stamps the rotation onto every subtree item via the Group
      // per-item group-stamp mechanism; the executor right-multiplies it into ObjectToWorld). A thinner
      // sibling of Group — no SRT, no IsEnabled (TiXL's op has none). .t3 defaults: Axis (0,0,1)=Z, Angle 0.
      {"RotateAroundAxis", "RotateAroundAxis",
       {{"command", "command", "Command", true},
        {"out", "out", "Command", false},
        {"Angle", "Angle", "Float", true, 0.0f, -360.0f, 360.0f},
        {"Axis.x", "Axis", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 3},
        {"Axis.y", "Axis.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Axis.z", "Axis.z", "Float", true, 1.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1}},
       nullptr,
       "render.transform"},
      // Shear (TiXL Lib.render.transform.Shear): wraps a Command subtree and pushes a SHEAR matrix onto
      // context.ObjectToWorld (Identity with M12=Translation.Y, M21=Translation.X, M14=Translation.Z).
      // Command in → Command out (Group per-item group-stamp mechanism). No IsEnabled (TiXL has none).
      // The input is named "Translation" in TiXL (= the 3 shear amounts X/Y/Z); .t3 default (0,0,0).
      {"Shear", "Shear",
       {{"command", "command", "Command", true},
        {"out", "out", "Command", false},
        {"Translation.x", "Translation", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Translation.y", "Translation.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Translation.z", "Translation.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1}},
       nullptr,
       "render.transform"},
      // Transform (TiXL Lib.render.transform.Transform): the full-TRS render-island transform with a PIVOT
      // — the general sibling of Group (Group = Transform with pivot 0 + color/enable). Wraps a Command
      // subtree and pushes M = T(-Pivot)·S·R·T(+Pivot)·T(Translation) onto context.ObjectToWorld (Group
      // per-item group-stamp). Scale×UniformScale; Rotation = yaw(Y)/pitch(X)/roll(Z) degrees; no IsEnabled
      // (TiXL has none). FORK (named): TransformCallback editor gizmo hook dropped. .t3 defaults: Scale
      // (1,1,1), UniformScale 1, Rotation/Translation/Pivot (0,0,0).
      {"Transform", "Transform",
       {{"command", "command", "Command", true},
        {"out", "out", "Command", false},
        {"Translation.x", "Translation", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
        {"Translation.y", "Translation.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Translation.z", "Translation.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Rotation.x", "Rotation", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 3},
        {"Rotation.y", "Rotation.y", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        {"Rotation.z", "Rotation.z", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        {"Scale.x", "Scale", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Scale.y", "Scale.y", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Scale.z", "Scale.z", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"UniformScale", "UniformScale", "Float", true, 1.0f, 0.0f, 10.0f},
        {"Pivot.x", "Pivot", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
        {"Pivot.y", "Pivot.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Pivot.z", "Pivot.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1}},
       nullptr,
       "render.transform"},
      // RotateTowards (TiXL Lib.render.transform.RotateTowards): wraps a Command subtree and pushes a LookAt-
      // style ROTATION onto context.ObjectToWorld so the subtree FACES a target point. M = rotateOffset ·
      // inverse(LookAtRH(0, sourcePos-target, Up)) (Group per-item group-stamp; the executor right-multiplies
      // it into ObjectToWorld). No IsEnabled (TiXL has none). FORKS (named in point_ops_rotatetowards.cpp):
      // sourcePos=(0,0,0) at op scope; TowardsCamera mode resolves the camera to the default camera world pos
      // (0,0,defaultCameraDistance()) since no WorldToCamera is threaded. .t3 defaults: AlternativeTarget
      // (0,0,1), RotationOffset (0,0,0), LookTowards 0 (TowardsCamera).
      {"RotateTowards", "RotateTowards",
       {{"command", "command", "Command", true},
        {"out", "out", "Command", false},
        {"LookTowards", "LookTowards", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
         {"TowardsCamera", "TowardsPosition"}, true},
        {"AlternativeTarget.x", "AlternativeTarget", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
        {"AlternativeTarget.y", "AlternativeTarget.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"AlternativeTarget.z", "AlternativeTarget.z", "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"RotationOffset.x", "RotationOffset", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 3},
        {"RotationOffset.y", "RotationOffset.y", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        {"RotationOffset.z", "RotationOffset.z", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1}},
       nullptr,
       "render.transform"},
  };
  return specs;
}

}  // namespace sw
