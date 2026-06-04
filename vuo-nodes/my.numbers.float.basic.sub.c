/**
 * @file
 * my.numbers.float.basic.sub node implementation.
 *
 * Exact Vuo body-layer adapter for TiXL Operators/Lib/numbers/float/basic/Sub.
 */

VuoModuleMetadata({
					 "title" : "my_Sub",
					 "description" : "TiXL Sub scalar adapter. Source: external/tixl/Operators/Lib/numbers/float/basic/Sub.cs. Category: Operators/Lib/numbers/float/basic. Primary output: float (ColorForValues #868C8D).",
					 "keywords" : [ "tixl", "numbers", "float", "basic", "subtract" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

void nodeEvent
(
		VuoInputData(VuoReal, {"default":0.0}) Input1,
		VuoInputData(VuoReal, {"default":0.0}) Input2,
		VuoOutputData(VuoReal, {"name":"Result"}) result
)
{
	*result = Input1 - Input2;
}
