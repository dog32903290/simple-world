/**
 * @file
 * my.numbers.float.basic.div node implementation.
 *
 * Exact Vuo body-layer adapter for TiXL Operators/Lib/numbers/float/basic/Div.
 */

#include <math.h>

VuoModuleMetadata({
					 "title" : "my_Div",
					 "description" : "TiXL Div scalar adapter. Source: external/tixl/Operators/Lib/numbers/float/basic/Div.cs. Category: Operators/Lib/numbers/float/basic. Primary output: float (ColorForValues #868C8D).",
					 "keywords" : [ "tixl", "numbers", "float", "basic", "divide" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

void nodeEvent
(
		VuoInputData(VuoReal, {"default":1.0}) A,
		VuoInputData(VuoReal, {"default":1.0}) B,
		VuoOutputData(VuoReal, {"name":"Result"}) result
)
{
	*result = B == 0.0 ? NAN : A / B;
}
