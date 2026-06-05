/**
 * @file
 * my.numbers.floats.basic.setFloatListValue node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_SetFloatListValue
 * - Category: Operators/Lib/numbers/floats/basic
 * - Source: external/tixl/Operators/Lib/numbers/floats/basic/SetFloatListValue.cs
 * - Default: Mode=0, TriggerSet=false, FloatList=[], Index=0, Value=0.0 from SetFloatListValue.t3
 * - Primary output: List<float> Result (ColorForValues #868C8D)
 *
 * Vuo boundary: TiXL leaves Result.Value unchanged when TriggerSet is false or the list is empty.
 * This node exposes that previous value as previousResult.
 */

#include "VuoList_VuoReal.h"

VuoModuleMetadata({
					 "title" : "my_SetFloatListValue",
					 "description" : "Sets, adds, or multiplies one or all values in a float list when triggered.",
					 "keywords" : [ "tixl", "floats", "list", "set", "add", "multiply", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
				 });

static VuoInteger positiveMod(VuoInteger value, VuoInteger repeat)
{
	if (repeat == 0)
		return 0;
	VuoInteger result = value % repeat;
	return result < 0 ? result + repeat : result;
}

static VuoReal applyMode(VuoReal current, VuoReal value, VuoInteger mode)
{
	if (mode == 1)
		return current + value;
	if (mode == 2)
		return current * value;
	return value;
}

void nodeEvent
(
		VuoInputData(VuoList_VuoReal) floatList,
		VuoInputData(VuoInteger, {"default":0, "suggestedMin":0, "suggestedMax":2}) mode,
		VuoInputData(VuoBoolean, {"default":false}) triggerSet,
		VuoInputData(VuoInteger, {"default":0}) index,
		VuoInputData(VuoReal, {"default":0.0}) value,
		VuoInputData(VuoList_VuoReal) previousResult,
		VuoOutputData(VuoList_VuoReal, {"name":"Result"}) result
)
{
	unsigned long inputCount = floatList ? VuoListGetCount_VuoReal(floatList) : 0;
	if (!triggerSet || inputCount == 0)
	{
		*result = previousResult ? VuoListCopy_VuoReal(previousResult) : VuoListCreate_VuoReal();
		return;
	}

	VuoList_VuoReal output = VuoListCopy_VuoReal(floatList);
	unsigned long count = VuoListGetCount_VuoReal(output);

	if (index >= 0)
	{
		unsigned long position = (unsigned long)positiveMod(index, (VuoInteger)count) + 1;
		VuoReal current = VuoListGetValue_VuoReal(output, position);
		VuoListSetValue_VuoReal(output, applyMode(current, value, mode), position, false);
	}
	else if (index == -2)
	{
		for (unsigned long i = 1; i <= count; ++i)
		{
			VuoReal current = VuoListGetValue_VuoReal(output, i);
			VuoListSetValue_VuoReal(output, applyMode(current, value, mode), i, false);
		}
	}

	*result = output;
}
