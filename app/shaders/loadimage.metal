// LoadImage: TiXL-ported image-source op (the SOURCE-OP seam, proving op LoadImage).
// Faithful to external/tixl Operators/Lib/image/generate/load/LoadImage.cs: a Texture2D SOURCE
// that decodes a file at `Path` and exposes it as a Slot<Texture2D>. TiXL has NO pixel kernel
// (the texture IS the decoded asset); we copy the decoded RGBA8 texels 1:1 into the op's own
// resolution-sized output via this trivial fullscreen pass.
//
// Sampler is bound NEAREST (point/clamp) in the cook (cookLoadImage), so when the output
// resolution matches the decoded image's native dimensions the copy is byte-exact (each output
// texel reads the matching source texel center) — this is what the golden pins. At a DIFFERENT
// output resolution it nearest-resamples (a host-resolution fork, named in the leaf header).
//
// Fork (named, DX11->Metal):
//   - fork[no-kernel]: TiXL LoadImage has no shader (Texture2D = decoded asset). The copy pass is a
//     host-side artifact of riding the resolution-pinned ensureTex output flow (so a downstream
//     Texture2D consumer reads a normal RGBA8Unorm texture). The PIXELS are the decoded asset,
//     unmodified — no color math.
//   - fork[nearest-sampler]: nearest (not linear) so a same-size copy is identity (golden exactness).
#include <metal_stdlib>
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer): covers the viewport, texCoord 0..1.
vertex VSOut loadimage_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);  // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);       // flip Y: NDC up vs texture down
  return o;
}

// Copy the decoded source texel straight through (no color math — the texture IS the asset).
fragment float4 loadimage_fs(VSOut in              [[stage_in]],
                             texture2d<float> src   [[texture(0)]],
                             sampler samNearest      [[sampler(0)]]) {
  return src.sample(samNearest, in.texCoord);
}
