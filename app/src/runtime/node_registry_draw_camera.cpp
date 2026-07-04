// runtime/node_registry_draw_camera — NodeSpec rows for the CAMERA family: the render.camera ops that
// wrap a Command subtree and render it through an explicit camera projection (Camera / OrthographicCamera).
// Peeled out of node_registry_draw.cpp so the camera lane can extend this family without touching
// drawSpecs()'s shared table (parallel-lane peel — no merge conflict with render/flow/data lanes). These
// rows moved VERBATIM from node_registry_draw.cpp; table order unchanged (drawSpecs() appends in source order).
#include "runtime/node_registry_draw.h"
#include "runtime/graph.h"

namespace sw {

const std::vector<NodeSpec>& drawCameraSpecs() {
  static const std::vector<NodeSpec> specs = {
      // Camera (TiXL Lib.render.camera.Camera): wraps a Command subtree and renders it through an
      // explicit camera (Position/Target/Up/FieldOfView/ClipPlanes) instead of the driver-local default
      // (Camera.cs push/pop). Command in → Command out (the op stamps its camera onto every subtree item;
      // the RenderTarget executor builds WorldToCamera/CameraToClipSpace from those params). FORK (named):
      // offset/roll/lensShift dropped (Camera.cs:82-103 commented embellishments); AspectRatio default 0
      // → the output (RequestedResolution) aspect (Camera.cs:53-55). v1: no OrbitCamera/ActionCamera, no depth.
      {"Camera", "Camera",
       {{"command", "command", "Command", true},
        {"out", "out", "Command", false},
        {"Position.x", "Position", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
        {"Position.y", "Position.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Position.z", "Position.z", "Float", true, 2.4142135f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Target.x", "Target", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
        {"Target.y", "Target.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Target.z", "Target.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Up.x", "Up", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 3},
        {"Up.y", "Up.y", "Float", true, 1.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Up.z", "Up.z", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"FieldOfView", "FieldOfView", "Float", true, 45.0f, 1.0f, 179.0f},
        {"ClipPlanes.x", "ClipPlanes", "Float", true, 0.01f, 0.0001f, 1000.0f, Widget::Vec, {}, true, 2},
        {"ClipPlanes.y", "ClipPlanes.y", "Float", true, 1000.0f, 0.0001f, 100000.0f, Widget::Vec, {}, true, 1},
        {"AspectRatio", "AspectRatio", "Float", true, 0.0f, 0.0f, 10.0f}},
       nullptr,
       "render.camera"},
      // OrthographicCamera (TiXL Lib.render.camera.OrthographicCamera): the perspective Camera's twin — wraps
      // a Command subtree and renders it through an ORTHOGRAPHIC projection (no perspective foreshortening; a
      // farther eye does NOT shrink the view). Command in → Command out (the op stamps its ortho camera onto
      // every subtree item; the RenderTarget executor builds CameraToClipSpace = orthoRH(size) where
      // size = Stretch·Scale·(aspect,1), OrthographicCamera.cs:33). FORKS (named): Roll dropped
      // (OrthographicCamera.cs:30 rollRotation); Position default = TiXL's exact .t3 value (NOT rounded); no
      // Reference output; AspectRatio default 0 → output (RequestedResolution) aspect (cs:25-28). v1: Scale +
      // eye/target/up + near/far shipped (Stretch defaults (1,1)); no OrbitCamera/ActionCamera, no depth.
      {"OrthographicCamera", "OrthographicCamera",
       {{"command", "command", "Command", true},
        {"out", "out", "Command", false},
        {"Position.x", "Position", "Float", true, -0.0015059264f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
        {"Position.y", "Position.y", "Float", true, 0.0014562709f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Position.z", "Position.z", "Float", true, 10.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Target.x", "Target", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
        {"Target.y", "Target.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Target.z", "Target.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Up.x", "Up", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 3},
        {"Up.y", "Up.y", "Float", true, 1.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Up.z", "Up.z", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Scale", "Scale", "Float", true, 1.0f, 0.0001f, 100.0f},
        {"Stretch.x", "Stretch", "Float", true, 1.0f, 0.0001f, 100.0f, Widget::Vec, {}, true, 2},
        {"Stretch.y", "Stretch.y", "Float", true, 1.0f, 0.0001f, 100.0f, Widget::Vec, {}, true, 1},
        {"NearFarClip.x", "NearFarClip", "Float", true, 0.1f, 0.0001f, 1000.0f, Widget::Vec, {}, true, 2},
        {"NearFarClip.y", "NearFarClip.y", "Float", true, 1000.0f, 0.0001f, 100000.0f, Widget::Vec, {}, true, 1},
        {"AspectRatio", "AspectRatio", "Float", true, 0.0f, 0.0f, 10.0f}},
       nullptr,
       "render.camera"},
      // CamPosition (TiXL Lib.render.camera.CamPosition, camera-A lane): emits the AMBIENT camera's
      // world-space Position / (unnormalized) view Direction / projection AspectRatio — CamPosition.cs:
      // 29-38 (invert context.WorldToCamera; transform (0,0,0,1) and (0,0,1,1); M22/M11). OUTPUT-ONLY
      // (CamPosition.cs has no Input slots). CONTEXT-reading → evaluate==nullptr; the values are cooked
      // once per frame by cookCameraValueOutputNodes (resident_camera_value_cook.cpp) onto extOut[1..7],
      // resolving the enclosing camera STRUCTURALLY (fork-camera-value-structural-enclosing-walk, named
      // in that header). The Command output is the TiXL execution-path hook (draws nothing; registered
      // as a no-op cmd op so a Command chain stays walkable through it).
      // OUTPUT PORTS ONLY, extOut index = port index: [0]=Command (no float slot), [1..3]=Position,
      // [4..6]=Direction, [7]=AspectRatio (exactly fills extOut[8]).
      // FORK fork-vec-output-as-n-scalar-ports: TiXL's Position/Direction are ONE Slot<Vector3> each;
      // here 3 Float ports each (the established output-side scalar-pack fork, RequestedResolution).
      {"CamPosition", "CamPosition",
       {{"Command", "Command", "Command", false},
        {"Position.x", "Position", "Float", false},
        {"Position.y", "Position.y", "Float", false},
        {"Position.z", "Position.z", "Float", false},
        {"Direction.x", "Direction", "Float", false},
        {"Direction.y", "Direction.y", "Float", false},
        {"Direction.z", "Direction.z", "Float", false},
        {"AspectRatio", "AspectRatio", "Float", false}},
       nullptr,
       "render.camera"},
  };
  return specs;
}

}  // namespace sw
