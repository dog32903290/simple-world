/**
 * @file
 * my.numbers.int.process.clampInt node implementation.
 *
 * Exact Vuo body-layer adapter for TiXL Operators/Lib/numbers/int/process/ClampInt.
 */

VuoModuleMetadata({
					 "title" : "my_ClampInt",
					 "description" : "TiXL ClampInt adapter. Source: external/tixl/Operators/Lib/numbers/int/process/ClampInt.cs. Category: Operators/Lib/numbers/int/process. Primary output: int (ColorForValues #868C8D). Uses MathUtils.Clamp shape: min(max(value,min),max), without sorting reversed ranges.",
					 "keywords" : [ "tixl", "numbers", "int", "process", "clamp", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

static VuoInteger myClampInt(VuoInteger value, VuoInteger min, VuoInteger max)
{
	VuoInteger raised = value > min ? value : min;
	return raised < max ? raised : max;
}

void nodeEvent
(
		VuoInputData(VuoInteger, {"default":0}) value,
		VuoInputData(VuoInteger, {"default":0}) min,
		VuoInputData(VuoInteger, {"default":0}) max,
		VuoOutputData(VuoInteger, {"name":"Result"}) result
)
{
	*result = myClampInt(value, min, max);
}
