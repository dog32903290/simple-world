/**
 * @file
 * my.numbers.int2.process.int2Components node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_Int2Components
 * - Category: Operators/Lib/numbers/int2/process
 * - Source: external/tixl/Operators/Lib/numbers/int2/process/Int2Components.cs
 * - Defaults: Resolution=(0,0) from Int2Components.t3
 * - Primary output: int Width (ColorForValues #868C8D)
 *
 * Vuo body-layer mapping:
 * TiXL Int2 is carried as VuoPoint2d for Vuo wiring, with x=Width and y=Height.
 */

#include "VuoPoint2d.h"
#include <math.h>

VuoModuleMetadata({
					 "title" : "my_Int2Components",
					 "description" : "Splits a TiXL Int2 resolution into width, height, length, and aspect ratio.",
					 "keywords" : [ "tixl", "int2", "resolution", "components", "aspect" ],
					 "version" : "1.0.0",
				 });

static VuoInteger toInt(VuoReal value)
{
	return (VuoInteger)value;
}

void nodeEvent
(
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) resolution,
		VuoOutputData(VuoInteger, {"name":"Width"}) width,
		VuoOutputData(VuoInteger, {"name":"Height"}) height,
		VuoOutputData(VuoInteger, {"name":"Length"}) length,
		VuoOutputData(VuoReal, {"name":"AspectRatio"}) aspectRatio
)
{
	VuoInteger w = toInt(resolution.x);
	VuoInteger h = toInt(resolution.y);
	*width = w;
	*height = h;
	*length = w * h;
	*aspectRatio = (VuoReal)w / (VuoReal)h;
}
