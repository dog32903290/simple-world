/**
 * @file
 * my.numbers.ints.setIntListValue node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_SetIntListValue
 * - Category: Operators/Lib/numbers/ints
 * - Source: external/tixl/Operators/Lib/numbers/ints/SetIntListValue.cs
 * - Defaults: Mode=0, TriggerSet=false, IntList=[], Index=0, Value=0 from SetIntListValue.t3
 * - Primary output: List<int> Result (ColorForValues #868C8D)
 */

#include "VuoList_VuoInteger.h"

VuoModuleMetadata({
					 "title" : "my_SetIntListValue",
					 "description" : "Sets, adds, or multiplies one or all values in an integer list when triggered.",
					 "keywords" : [ "tixl", "ints", "list", "set", "add", "multiply" ],
					 "version" : "1.0.0",
				 });

static VuoInteger positiveMod(VuoInteger value, VuoInteger repeat)
{
	VuoInteger result = value % repeat;
	return result < 0 ? result + repeat : result;
}

static VuoInteger applyMode(VuoInteger current, VuoInteger value, VuoInteger mode)
{
	if (mode == 1)
		return current + value;
	if (mode == 2)
		return current * value;
	return value;
}

void nodeEvent
(
		VuoInputData(VuoList_VuoInteger) intList,
		VuoInputData(VuoInteger, {"default":0, "suggestedMin":0, "suggestedMax":2}) mode,
		VuoInputData(VuoBoolean, {"default":false}) triggerSet,
		VuoInputData(VuoInteger, {"default":0}) index,
		VuoInputData(VuoInteger, {"default":0}) value,
		VuoOutputData(VuoList_VuoInteger, {"name":"Result"}) result
)
{
	VuoList_VuoInteger output = intList ? VuoListCopy_VuoInteger(intList) : VuoListCreate_VuoInteger();
	unsigned long count = VuoListGetCount_VuoInteger(output);
	if (!triggerSet || count == 0)
	{
		*result = output;
		return;
	}

	if (index >= 0)
	{
		unsigned long position = (unsigned long)positiveMod(index, (VuoInteger)count) + 1;
		VuoInteger current = VuoListGetValue_VuoInteger(output, position);
		VuoListSetValue_VuoInteger(output, applyMode(current, value, mode), position, false);
	}
	else if (index == -2)
	{
		for (unsigned long i = 1; i <= count; ++i)
		{
			VuoInteger current = VuoListGetValue_VuoInteger(output, i);
			VuoListSetValue_VuoInteger(output, applyMode(current, value, mode), i, false);
		}
	}

	*result = output;
}
