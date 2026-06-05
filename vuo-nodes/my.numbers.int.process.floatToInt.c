/**
 * @file
 * my.numbers.int.process.floatToInt node implementation.
 *
 * Exact Vuo body-layer adapter for TiXL Operators/Lib/numbers/int/process/FloatToInt.
 */

VuoModuleMetadata({
					 "title" : "my_FloatToInt",
					 "description" : "TiXL FloatToInt adapter. Source: external/tixl/Operators/Lib/numbers/int/process/FloatToInt.cs. Category: Operators/Lib/numbers/int/process. Primary output: int (ColorForValues #868C8D). C# cast truncates toward zero.",
					 "keywords" : [ "tixl", "numbers", "int", "process", "float", "cast", "truncate", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

void nodeEvent
(
		VuoInputData(VuoReal, {"default":0.0}) floatValue,
		VuoOutputData(VuoInteger, {"name":"Integer"}) integer
)
{
	*integer = (VuoInteger)floatValue;
}
