/**
 * @file
 * my.numbers.int.basic.addInts node implementation.
 *
 * Exact Vuo body-layer adapter for TiXL Operators/Lib/numbers/int/basic/AddInts.
 */

VuoModuleMetadata({
					 "title" : "my_AddInts",
					 "description" : "TiXL AddInts integer adapter. Source: external/tixl/Operators/Lib/numbers/int/basic/AddInts.cs. Category: Operators/Lib/numbers/int/basic. Primary output: int (ColorForValues #868C8D). Separate creator-facing port from my_IntAdd because TiXL exposes both names.",
					 "keywords" : [ "tixl", "numbers", "int", "basic", "add", "alias", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

void nodeEvent
(
		VuoInputData(VuoInteger, {"default":0}) input1,
		VuoInputData(VuoInteger, {"default":0}) input2,
		VuoOutputData(VuoInteger, {"name":"Result"}) result
)
{
	*result = input1 + input2;
}
