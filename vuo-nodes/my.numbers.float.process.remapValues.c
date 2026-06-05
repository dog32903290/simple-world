/**
 * @file
 * my.numbers.float.process.remapValues node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_RemapValues
 * - Category: Operators/Lib/numbers/float/process
 * - Source: external/tixl/Operators/Lib/numbers/float/process/RemapValues.cs
 * - Default: InputAndOutputPairs=(0,0), InputValue=0.0 from RemapValues.t3
 * - Primary output: float Result (ColorForValues #868C8D)
 *
 * Vuo body-layer mapping:
 * TiXL MultiInputSlot<Vector2> is carried as a VuoList_VuoPoint2d input where x=lookup and y=output.
 */

#include "VuoList_VuoPoint2d.h"
#include "VuoPoint2d.h"
#include <math.h>

VuoModuleMetadata({
					 "title" : "my_RemapValues",
					 "description" : "Selects the output value from the pair whose x lookup value is closest to InputValue.",
					 "keywords" : [ "tixl", "numbers", "float", "remap", "lookup", "nearest", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
				 });

void nodeEvent
(
		VuoInputData(VuoList_VuoPoint2d) inputAndOutputPairs,
		VuoInputData(VuoReal, {"default":0.0}) inputValue,
		VuoOutputData(VuoReal, {"name":"Result"}) result
)
{
	VuoReal minDistance = INFINITY;
	VuoReal bestValue = 0.0;
	VuoInteger bestIndex = -1;
	unsigned long count = inputAndOutputPairs ? VuoListGetCount_VuoPoint2d(inputAndOutputPairs) : 0;

	for (unsigned long i = 1; i <= count; ++i)
	{
		VuoPoint2d pair = VuoListGetValue_VuoPoint2d(inputAndOutputPairs, i);
		VuoReal distance = fabs(pair.x - inputValue);
		if (distance < minDistance)
		{
			minDistance = distance;
			bestValue = pair.y;
			bestIndex = (VuoInteger)i - 1;
		}
	}

	if (bestIndex == -1)
		*result = 0.0;
	else
		*result = bestValue;
}
