/**
 * @file
 * my.numbers.float.adjust.remap node implementation.
 *
 * Exact Vuo body-layer adapter for TiXL Operators/Lib/numbers/float/adjust/Remap.
 */

#include "VuoPoint2d.h"
#include <math.h>

VuoModuleMetadata({
					 "title" : "my_Remap",
					 "description" : "TiXL Remap scalar adapter. Source: external/tixl/Operators/Lib/numbers/float/adjust/Remap.cs. Category: Operators/Lib/numbers/float/adjust. Primary output: float (ColorForValues #868C8D). Implements Normal, Clamped, and Modulo modes plus TiXL ApplyGainAndBias.",
					 "keywords" : [ "tixl", "numbers", "float", "adjust", "remap", "range", "bias", "gain", "clamp", "modulo", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
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
		VuoInputData(VuoReal, {"default":0.0}) value,
		VuoInputData(VuoReal, {"default":0.0}) rangeInMin,
		VuoInputData(VuoReal, {"default":1.0}) rangeInMax,
		VuoInputData(VuoReal, {"default":0.0}) rangeOutMin,
		VuoInputData(VuoReal, {"default":1.0}) rangeOutMax,
		VuoInputData(VuoPoint2d, {"default":{"x":0.5,"y":0.5}}) biasAndGain,
		VuoInputData(VuoInteger, {"default":0, "suggestedMin":0, "suggestedMax":2}) mode,
		VuoOutputData(VuoReal, {"name":"Result"}) result
)
{
	VuoReal normalized = (value - rangeInMin) / (rangeInMax - rangeInMin);
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
		v = myFmod(v, max - min);
	}

	*result = v;
}
