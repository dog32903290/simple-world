/**
 * @file
 * my.numbers.float.basic.sqrt node implementation.
 *
 * Exact Vuo body-layer adapter for TiXL Operators/Lib/numbers/float/basic/Sqrt.
 */

#include <math.h>

VuoModuleMetadata({
					 "title" : "my_Sqrt",
					 "description" : "TiXL Sqrt scalar adapter. Source: external/tixl/Operators/Lib/numbers/float/basic/Sqrt.cs. Category: Operators/Lib/numbers/float/basic. Primary output: float (ColorForValues #868C8D).",
					 "keywords" : [ "tixl", "numbers", "float", "basic", "sqrt", "square root" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

void nodeEvent
(
		VuoInputData(VuoReal, {"default":1.0}) Value,
		VuoOutputData(VuoReal, {"name":"Result"}) result
)
{
	*result = sqrt(Value);
}
