/**
 * @file
 * my.numbers.float.basic.add node implementation.
 *
 * Exact Vuo body-layer adapter for TiXL Operators/Lib/numbers/float/basic/Add.
 */

VuoModuleMetadata({
					 "title" : "my_Add",
					 "description" : "TiXL Add scalar adapter. Source: external/tixl/Operators/Lib/numbers/float/basic/Add.cs. Category: Operators/Lib/numbers/float/basic. Primary output: float (ColorForValues #868C8D).",
					 "keywords" : [ "tixl", "numbers", "float", "basic", "add" ],
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
	*result = Input1 + Input2;
}
