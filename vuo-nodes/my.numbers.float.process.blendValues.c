/**
 * @file
 * my.numbers.float.process.blendValues node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_BlendValues
 * - Category: Operators/Lib/numbers/float/process
 * - Source: external/tixl/Operators/Lib/numbers/float/process/BlendValues.cs
 * - Default: Values=0.0, F=0.0 from BlendValues.t3
 * - Primary output: float Result (ColorForValues #868C8D)
 *
 * Vuo body-layer mapping:
 * TiXL MultiInputSlot<float> is carried as a VuoList_VuoReal input.
 */

#include "VuoList_VuoReal.h"
#include <math.h>

VuoModuleMetadata({
					 "title" : "my_BlendValues",
					 "description" : "Blends adjacent wrapped float inputs using TiXL MathUtils.Fmod index and mix behavior.",
					 "keywords" : [ "tixl", "numbers", "float", "blend", "lerp", "aggregate", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
				 });

static VuoReal myFmod(VuoReal value, VuoReal mod)
{
	return value - mod * floor(value / mod);
}

void nodeEvent
(
		VuoInputData(VuoList_VuoReal) values,
		VuoInputData(VuoReal, {"default":0.0}) f,
		VuoOutputData(VuoReal, {"name":"Result"}) result
)
{
	unsigned long count = values ? VuoListGetCount_VuoReal(values) : 0;
	if (count == 0)
	{
		*result = 0.0;
		return;
	}

	VuoInteger index1 = (VuoInteger)myFmod((VuoInteger)f, (VuoReal)count);
	VuoInteger index2 = (VuoInteger)myFmod((VuoInteger)(f + 1.0), (VuoReal)count);
	VuoReal mix = myFmod(f, 1.0);
	VuoReal a = VuoListGetValue_VuoReal(values, (unsigned long)index1 + 1);
	VuoReal b = VuoListGetValue_VuoReal(values, (unsigned long)index2 + 1);
	*result = a * (1.0 - mix) + b * mix;
}
