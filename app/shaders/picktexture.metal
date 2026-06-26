// PickTexture: TiXL-ported MultiInput texture SELECTOR (lane multi-tex-input, image/use). Faithful
// port of external/tixl Operators/Lib/image/use/PickTexture.cs — a pure C# op with NO shader:
//   Update(): connections = Input.GetCollectedTypedInputs(); idx = Index.Mod(connections.Count);
//             Selected.Value = connections[idx].GetValue();
// i.e. it forwards the (Index mod N)-th of its N MultiInput textures, unchanged.
//
// In simple_world the cook driver always hands an op an ensureTex OUTPUT to fill, so the "forward the
// selected texture" semantic is realized as a 1:1 fullscreen COPY of the CPU-selected input (bound at
// texture(0)) into the output. The selection (Index mod N) happens host-side in point_ops_picktexture.cpp;
// this shader is the trivial passthrough sampler. The op is the FIRST genuinely-MultiInput Texture2D
// consumer — proving the variable-N inputTextures[] gather (cookTexNode MultiInput Texture2D branch).
//
// Fork (named, DX11->Metal): TiXL's PickTexture does a pure reference passthrough (no resample). We
// realize it as a fullscreen sample-copy because the engine's tex-cook always allocates a fresh output
// texture. With output size == input size (the golden's case) the copy is bit-exact at texel centers;
// a sampler is bound for the general (resized) case. Nearest filter + ClampToEdge keeps the copy
// faithful (no smoothing, no wrap) when sizes match.
#include <metal_stdlib>
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer); texCoord 0..1 with Y flipped (NDC up vs
// texture down), same convention as combine3images_vs / blur_vs.
vertex VSOut picktexture_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);   // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);
  return o;
}

// Passthrough: sample the host-SELECTED input (bound at texture(0)) at uv, return it unchanged.
fragment float4 picktexture_fs(VSOut in [[stage_in]],
                               texture2d<float> selected [[texture(0)]],
                               sampler texSampler        [[sampler(0)]]) {
  return selected.sample(texSampler, in.texCoord);
}
