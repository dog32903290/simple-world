/**
 * @file
 * my.numbers.int.basic.intAdd node implementation.
 *
 * Exact Vuo body-layer adapter for TiXL Operators/Lib/numbers/int/basic/IntAdd.
 */

VuoModuleMetadata({
					 "title" : "my_IntAdd",
					 "description" : "TiXL IntAdd integer adapter. Source: external/tixl/Operators/Lib/numbers/int/basic/IntAdd.cs. Category: Operators/Lib/numbers/int/basic. Primary output: int (ColorForValues #868C8D).",
					 "keywords" : [ "tixl", "numbers", "int", "basic", "add", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

void nodeEvent
(
		VuoInputData(VuoInteger, {"default":0}) value1,
		VuoInputData(VuoInteger, {"default":0}) value2,
		VuoOutputData(VuoInteger, {"name":"Result"}) result
)
{
	*result = value1 + value2;
}
