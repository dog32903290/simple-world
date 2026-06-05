/**
 * @file
 * my.numbers.float.trigonometry.atan2 node implementation.
 *
 * Exact Vuo body-layer adapter for TiXL Operators/Lib/numbers/float/trigonometry/Atan2.
 */

#include "VuoPoint2d.h"
#include <math.h>

VuoModuleMetadata({
					 "title" : "my_Atan2",
					 "description" : "TiXL Atan2 adapter. Source: external/tixl/Operators/Lib/numbers/float/trigonometry/Atan2.cs. Category: Operators/Lib/numbers/float/trigonometry. Primary output: float (ColorForValues #868C8D). Uses TiXL component order MathF.Atan2(Vector.X, Vector.Y).",
					 "keywords" : [ "tixl", "numbers", "float", "trigonometry", "atan2", "vector2", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

void nodeEvent
(
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) vector,
		VuoOutputData(VuoReal, {"name":"Result"}) result
)
{
	*result = atan2(vector.x, vector.y);
}
