/**
 * @file
 * my.numbers.floats.process.sumRange node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_SumRange
 * - Category: Operators/Lib/numbers/floats/process
 * - Source: external/tixl/Operators/Lib/numbers/floats/process/SumRange.cs
 * - Default: Input=[5.0,17.0], LowerLimit=0, UpperLimit=999999 from SumRange.t3
 * - Primary output: float Selected (ColorForValues #868C8D)
 *
 * TiXL returns early for null/empty input and keeps the previous slot value.
 * Vuo stateless node ports that cache behavior explicitly as previousSelected.
 */

#include "VuoList_VuoReal.h"

VuoModuleMetadata({
					 "title" : "my_SumRange",
					 "description" : "Sums a half-open index range of a float list, exposing TiXL's empty-input previous-value behavior.",
					 "keywords" : [ "tixl", "numbers", "floats", "list", "process", "sum", "range", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
				 });

void nodeEvent
(
		VuoInputData(VuoList_VuoReal) input,
		VuoInputData(VuoInteger, {"default":0}) lowerLimit,
		VuoInputData(VuoInteger, {"default":999999}) upperLimit,
		VuoInputData(VuoReal, {"default":0.0}) previousSelected,
		VuoOutputData(VuoReal, {"name":"Selected"}) selected
)
{
	unsigned long count = input ? VuoListGetCount_VuoReal(input) : 0;
	if (count == 0)
	{
		*selected = previousSelected;
		return;
	}

	VuoInteger lower = lowerLimit < 0 ? 0 : lowerLimit;
	VuoInteger upper = upperLimit > count ? (VuoInteger)count : upperLimit;

	VuoReal sum = 0.0;
	for (VuoInteger index = lower; index < upper; ++index)
		sum += VuoListGetValue_VuoReal(input, (unsigned long)index + 1);

	*selected = sum;
}
