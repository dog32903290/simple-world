/**
 * @file
 * my.numbers.float.basic.pow node implementation.
 *
 * Exact Vuo body-layer adapter for TiXL Operators/Lib/numbers/float/basic/Pow.
 */

#include <math.h>

VuoModuleMetadata({
					 "title" : "my_Pow",
					 "description" : "TiXL Pow scalar adapter. Source: external/tixl/Operators/Lib/numbers/float/basic/Pow.cs. Category: Operators/Lib/numbers/float/basic. Primary output: float (ColorForValues #868C8D).",
					 "keywords" : [ "tixl", "numbers", "float", "basic", "pow", "power", "exponent" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

void nodeEvent
(
		VuoInputData(VuoReal, {"default":1.0}) Value,
		VuoInputData(VuoReal, {"default":1.0}) Exponent,
		VuoOutputData(VuoReal, {"name":"Result"}) result
)
{
	*result = pow(Value, Exponent);
}
