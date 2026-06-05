/**
 * @file
 * my.numbers.floats.process.remapFloatList node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_RemapFloatList
 * - Category: Operators/Lib/numbers/floats/process
 * - Source: external/tixl/Operators/Lib/numbers/floats/process/RemapFloatList.cs
 * - Default: FloatList=[], RangeInMin=0.0, RangeInMax=1.0, RangeOutMin=0.0, RangeOutMax=1.0, BiasAndGain=(0.5,0.5), Mode=0
 * - Primary output: List<float> Result (ColorForValues #868C8D)
 */

#include "VuoList_VuoReal.h"
#include "VuoPoint2d.h"
#include <math.h>

VuoModuleMetadata({
					 "title" : "my_RemapFloatList",
					 "description" : "Remaps each float in a list with TiXL range, gain/bias, clamped, and modulo modes.",
					 "keywords" : [ "tixl", "numbers", "floats", "list", "process", "remap", "gain", "bias", "clamp", "modulo", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
				 });

static VuoReal myClamp(VuoReal value, VuoReal min, VuoReal max)
{
	return fmin(fmax(value, min), max);
}

static VuoReal myBias(VuoReal b, VuoReal x)
{
	return x / (((1.0 / b - 2.0) * (1.0 - x)) + 1.0);
}

static VuoReal mySchlickBias(VuoReal g, VuoReal x)
{
	if (x < 0.5)
	{
		x *= 2.0;
		return 0.5 * myBias(g, x);
	}
	x = 2.0 * x - 1.0;
	return 0.5 * myBias(1.0 - g, x) + 0.5;
}

static VuoReal myApplyGainAndBias(VuoReal value, VuoReal gain, VuoReal bias)
{
	VuoReal b = myClamp(bias, 0.0, 1.0);
	VuoReal g = myClamp(gain, 0.0, 1.0);

	if (value > 0.999)
		return 1.0;
	if (value < 0.00001)
		return 0.0;

	if (g < 0.5)
		return mySchlickBias(g, myBias(b, value));
	return myBias(b, mySchlickBias(g, value));
}

static VuoReal myFmod(VuoReal value, VuoReal mod)
{
	return value - mod * floor(value / mod);
}

void nodeEvent
(
		VuoInputData(VuoList_VuoReal) floatList,
		VuoInputData(VuoReal, {"default":0.0}) rangeInMin,
		VuoInputData(VuoReal, {"default":1.0}) rangeInMax,
		VuoInputData(VuoReal, {"default":0.0}) rangeOutMin,
		VuoInputData(VuoReal, {"default":1.0}) rangeOutMax,
		VuoInputData(VuoPoint2d, {"default":{"x":0.5,"y":0.5}}) biasAndGain,
		VuoInputData(VuoInteger, {"default":0,"suggestedMin":0,"suggestedMax":2,"suggestedStep":1}) mode,
		VuoOutputData(VuoList_VuoReal, {"name":"Result"}) result
)
{
	VuoList_VuoReal output = VuoListCreate_VuoReal();
	unsigned long count = floatList ? VuoListGetCount_VuoReal(floatList) : 0;
	if (count == 0)
	{
		*result = output;
		return;
	}

	VuoReal inRange = rangeInMax - rangeInMin;
	if (fabs(inRange) < 0.00001)
	{
		for (unsigned long i = 1; i <= count; ++i)
			VuoListAppendValue_VuoReal(output, rangeOutMin);
		*result = output;
		return;
	}

	for (unsigned long i = 1; i <= count; ++i)
	{
		VuoReal value = VuoListGetValue_VuoReal(floatList, i);
		VuoReal normalized = (value - rangeInMin) / inRange;
		if (normalized > 0.0 && normalized < 1.0)
			normalized = myApplyGainAndBias(normalized, biasAndGain.x, biasAndGain.y);

		VuoReal v = normalized * (rangeOutMax - rangeOutMin) + rangeOutMin;

		if (mode == 1)
		{
			VuoReal min = fmin(rangeOutMin, rangeOutMax);
			VuoReal max = fmax(rangeOutMin, rangeOutMax);
			v = myClamp(v, min, max);
		}
		else if (mode == 2)
		{
			VuoReal min = fmin(rangeOutMin, rangeOutMax);
			VuoReal max = fmax(rangeOutMin, rangeOutMax);
			VuoReal modRange = max - min;
			if (fabs(modRange) > 0.00001)
				v = min + myFmod(v - min, modRange);
			else
				v = min;
		}

		VuoListAppendValue_VuoReal(output, v);
	}

	*result = output;
}
