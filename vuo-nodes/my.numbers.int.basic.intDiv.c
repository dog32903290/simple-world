/**
 * @file
 * my.numbers.int.basic.intDiv node implementation.
 *
 * Exact Vuo body-layer adapter for TiXL Operators/Lib/numbers/int/basic/IntDiv.
 */

VuoModuleMetadata({
					 "title" : "my_IntDiv",
					 "description" : "TiXL IntDiv integer adapter. Source: external/tixl/Operators/Lib/numbers/int/basic/IntDiv.cs. Category: Operators/Lib/numbers/int/basic. Primary output: int (ColorForValues #868C8D).",
					 "keywords" : [ "tixl", "numbers", "int", "basic", "divide", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

void nodeEvent
(
		VuoInputData(VuoInteger, {"default":0}) numerator,
		VuoInputData(VuoInteger, {"default":1}) denominator,
		VuoOutputData(VuoInteger, {"name":"Result"}) result
)
{
	*result = denominator == 0 ? 1 : numerator / denominator;
}
