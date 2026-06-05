/**
 * @file
 * my.numbers.int2.basic.addInt2 node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_AddInt2
 * - Category: Operators/Lib/numbers/int2/basic
 * - Source: external/tixl/Operators/Lib/numbers/int2/basic/AddInt2.cs
 * - Defaults: Input1=(0,0), Input2=(0,0) from AddInt2.t3
 * - Primary output: Int2 (ColorForValues #868C8D)
 *
 * Vuo body-layer mapping:
 * TiXL Int2 is carried as VuoPoint2d for Vuo wiring, with x=Width and y=Height.
 */

#include "VuoPoint2d.h"

VuoModuleMetadata({
					 "title" : "my_AddInt2",
					 "description" : "Adds two TiXL Int2 values, carried in Vuo as point2d x/y width/height.",
					 "keywords" : [ "tixl", "int2", "resolution", "add", "size" ],
					 "version" : "1.0.0",
				 });

static VuoInteger toInt(VuoReal value)
{
	return (VuoInteger)value;
}

void nodeEvent
(
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) input1,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) input2,
		VuoOutputData(VuoPoint2d, {"name":"Result"}) result
)
{
	*result = (VuoPoint2d){toInt(input1.x) + toInt(input2.x), toInt(input1.y) + toInt(input2.y)};
}
