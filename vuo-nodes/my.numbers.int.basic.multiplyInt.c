/**
 * @file
 * my.numbers.int.basic.multiplyInt node implementation.
 *
 * Exact Vuo body-layer adapter for TiXL Operators/Lib/numbers/int/basic/MultiplyInt.
 */

VuoModuleMetadata({
					 "title" : "my_MultiplyInt",
					 "description" : "TiXL MultiplyInt integer adapter. Source: external/tixl/Operators/Lib/numbers/int/basic/MultiplyInt.cs. Category: Operators/Lib/numbers/int/basic. Primary output: int (ColorForValues #868C8D).",
					 "keywords" : [ "tixl", "numbers", "int", "basic", "multiply", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

void nodeEvent
(
		VuoInputData(VuoInteger, {"default":1}) a,
		VuoInputData(VuoInteger, {"default":1}) b,
		VuoOutputData(VuoInteger, {"name":"Result"}) result
)
{
	*result = a * b;
}
