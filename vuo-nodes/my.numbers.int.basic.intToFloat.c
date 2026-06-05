/**
 * @file
 * my.numbers.int.basic.intToFloat node implementation.
 *
 * Exact Vuo body-layer adapter for TiXL Operators/Lib/numbers/int/basic/IntToFloat.
 */

VuoModuleMetadata({
					 "title" : "my_IntToFloat",
					 "description" : "TiXL IntToFloat adapter. Source: external/tixl/Operators/Lib/numbers/int/basic/IntToFloat.cs. Category: Operators/Lib/numbers/int/basic. Primary output: float (ColorForValues #868C8D).",
					 "keywords" : [ "tixl", "numbers", "int", "float", "convert", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

void nodeEvent
(
		VuoInputData(VuoInteger, {"default":0}) intValue,
		VuoOutputData(VuoReal, {"name":"Result"}) result
)
{
	*result = (VuoReal)intValue;
}
