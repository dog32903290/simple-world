/**
 * @file
 * my.numbers.int2.process.scaleResolution node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_ScaleResolution
 * - Category: Operators/Lib/numbers/int2/process
 * - Source: external/tixl/Operators/Lib/numbers/int2/process/ScaleResolution.cs
 * - Defaults: Resolution=(0,0), Factor=(0,0), ClampToValidTextureSize=false
 * - Primary output: Int2 (ColorForValues #868C8D)
 *
 * Vuo body-layer mapping:
 * TiXL Int2 is carried as VuoPoint2d for Vuo wiring, with x=Width and y=Height.
 */

#include "VuoPoint2d.h"

VuoModuleMetadata({
					 "title" : "my_ScaleResolution",
					 "description" : "Non-uniformly scales a TiXL Int2 resolution and optionally clamps each dimension to 1..16384.",
					 "keywords" : [ "tixl", "int2", "resolution", "scale", "texture" ],
					 "version" : "1.0.0",
				 });

static const VuoInteger maxSize = 16384;

static VuoInteger toInt(VuoReal value)
{
	return (VuoInteger)value;
}

static VuoInteger clampTextureSize(VuoInteger value)
{
	if (value <= 0)
		return 1;
	if (value > maxSize)
		return maxSize;
	return value;
}

void nodeEvent
(
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) resolution,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) factor,
		VuoInputData(VuoBoolean, {"default":false}) clampToValidTextureSize,
		VuoOutputData(VuoPoint2d, {"name":"Size"}) size
)
{
	VuoInteger width = (VuoInteger)(toInt(resolution.x) * factor.x);
	VuoInteger height = (VuoInteger)(toInt(resolution.y) * factor.y);

	if (clampToValidTextureSize)
	{
		width = clampTextureSize(width);
		height = clampTextureSize(height);
	}

	*size = (VuoPoint2d){width, height};
}
