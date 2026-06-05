/**
 * @file
 * my.numbers.int.basic.subInts node implementation.
 *
 * Exact Vuo body-layer adapter for TiXL Operators/Lib/numbers/int/basic/SubInts.
 */

VuoModuleMetadata({
					 "title" : "my_SubInts",
					 "description" : "TiXL SubInts integer adapter. Source: external/tixl/Operators/Lib/numbers/int/basic/SubInts.cs. Category: Operators/Lib/numbers/int/basic. Primary output: int (ColorForValues #868C8D).",
					 "keywords" : [ "tixl", "numbers", "int", "basic", "subtract", "ColorForValues", "#868C8D" ],
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
	*result = input1 - input2;
}
