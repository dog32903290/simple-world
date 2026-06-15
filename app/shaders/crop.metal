// Crop: TiXL-ported compute image filter — the FIRST -cs.hlsl COMPUTE leaf in simple_world.
// Faithful port of external/tixl Operators/Lib/Assets/shaders/img/transform/CropImage-cs.hlsl.
//
// HLSL source (verbatim, the load-bearing body):
//     int x = i.x - int(CropLeft+0.4);
//     int y = i.y - int(CropTop+0.4);
//     bool outsize = x<0 || x>=width || y<0 || y>=height;
//     Result[i.xy] = outsize ? BackgroundColor : SourceImage[int2(x,y)];
//
// MSL caveats observed (silent-corruption traps):
//   - HLSL [numthreads(8,8,1)] has NO MSL equivalent — the 8x8 tile lives ONLY in the host
//     dispatchThreadgroups call (CROP_TGX/CROP_TGY in crop_params.h). This kernel must be
//     dispatched 8x8 to match the HLSL grid.
//   - HLSL t0 (SRV) and u0 (UAV) are SEPARATE register namespaces; MSL [[texture(n)]] is ONE
//     namespace -> Source @ texture(0), Result @ texture(1) (CROP_Source/CROP_Result).
//   - int(CropLeft+0.4) is HLSL float->int TRUNCATION toward zero. Ported VERBATIM as int(x) in
//     MSL (also truncation toward zero) — do NOT "clean up" to round() (silent off-by-one fork).
//   - SourceImage[int2(x,y)] is read ONLY after the x<0/x>=width bounds guard (OOB texture .read is
//     undefined). The HLSL relies on the ternary short-circuit; we guard explicitly the same way.
//   - Result[i.xy] / SourceImage[int2] integer indexing -> .write(val,i) / .read(uint2(x,y)).
//   - GetDimensions(0,width,height,numLevels) -> Source.get_width()/get_height().
//   - Extra guard (i.x>=Result.width || i.y>=Result.height) for non-8-divisible output sizes:
//     dispatchThreadgroups launches ceil(size/8)*8 threads, so the last tile can overrun.
#include <metal_stdlib>
#include "crop_params.h"
using namespace metal;

kernel void crop_cs(texture2d<float, access::read>  Source [[texture(CROP_Source)]],
                    texture2d<float, access::write> Result [[texture(CROP_Result)]],
                    constant CropParams&            P      [[buffer(CROP_Params)]],
                    uint2                           i      [[thread_position_in_grid]]) {
  // Non-8-divisible output guard: skip threads beyond the actual output extent.
  if (i.x >= Result.get_width() || i.y >= Result.get_height()) return;

  int width  = (int)Source.get_width();
  int height = (int)Source.get_height();

  // Verbatim: int(CropLeft+0.4) / int(CropTop+0.4) — truncation toward zero (NOT round).
  int x = (int)i.x - (int)(P.CropLeft + 0.4f);
  int y = (int)i.y - (int)(P.CropTop  + 0.4f);

  bool outsize = x < 0 || x >= width
              || y < 0 || y >= height;

  float4 bg = float4(P.BackgroundColor[0], P.BackgroundColor[1],
                     P.BackgroundColor[2], P.BackgroundColor[3]);

  // Guarded read: only sample Source when inside its bounds (OOB .read is undefined).
  float4 val = outsize ? bg : Source.read(uint2((uint)x, (uint)y));
  Result.write(val, i);
}
