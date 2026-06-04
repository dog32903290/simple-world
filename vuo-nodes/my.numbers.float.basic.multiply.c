/**
 * @file
 * my.numbers.float.basic.multiply node implementation.
 *
 * Exact Vuo body-layer adapter for TiXL Operators/Lib/numbers/float/basic/Multiply.
 */

VuoModuleMetadata({
					 "title" : "my_Multiply",
					 "description" : "TiXL Multiply scalar adapter. Source: external/tixl/Operators/Lib/numbers/float/basic/Multiply.cs. Category: Operators/Lib/numbers/float/basic. Primary output: float (ColorForValues #868C8D).",
					 "keywords" : [ "tixl", "numbers", "float", "basic", "multiply" ],
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
	*result = A * B;
}
