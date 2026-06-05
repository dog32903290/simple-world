/**
 * @file
 * my.numbers.float.basic.sum node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_Sum
 * - Category: Operators/Lib/numbers/float/basic
 * - Source: external/tixl/Operators/Lib/numbers/float/basic/Sum.cs
 * - Default: InputValues=0.0 from Sum.t3
 * - Primary output: float Result (ColorForValues #868C8D)
 *
 * Vuo body-layer mapping:
 * TiXL MultiInputSlot<float> is carried as a VuoList_VuoReal input.
 */

#include "VuoList_VuoReal.h"

VuoModuleMetadata({
					 "title" : "my_Sum",
					 "description" : "Sums connected float values; empty input returns the exposed default value like TiXL.",
					 "keywords" : [ "tixl", "numbers", "float", "sum", "aggregate", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
				 });

void nodeEvent
(
		VuoInputData(VuoList_VuoReal) inputValues,
		VuoInputData(VuoReal, {"default":0.0}) defaultValue,
		VuoOutputData(VuoReal, {"name":"Result"}) result
)
{
	unsigned long count = inputValues ? VuoListGetCount_VuoReal(inputValues) : 0;
	if (count == 0)
	{
		*result = defaultValue;
		return;
	}

	VuoReal total = 0.0;
	for (unsigned long i = 1; i <= count; ++i)
		total += VuoListGetValue_VuoReal(inputValues, i);
	*result = total;
}
