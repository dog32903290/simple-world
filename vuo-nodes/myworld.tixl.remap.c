/**
 * @file
 * myworld.tixl.remap node implementation.
 *
 * This node mirrors TiXL's Lib.numbers.float.adjust.Remap operator.
 */

#include <math.h>

VuoModuleMetadata({
					 "title" : "TiXL Remap",
					 "description" : "Remaps one value range to another, with TiXL-compatible bias/gain, clamp, and modulo modes.",
					 "keywords" : [ "tixl", "range", "map", "scale", "normalize", "clamp", "modulo", "bias", "gain" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

static VuoReal myworldClamp(VuoReal value, VuoReal min, VuoReal max)
{
	if (value < min)
		return min;
	if (value > max)
		return max;
	return value;
}

static VuoReal myworldBias(VuoReal b, VuoReal x)
{
	return x / (((1.0 / b - 2.0) * (1.0 - x)) + 1.0);
}

static VuoReal myworldSchlickBias(VuoReal g, VuoReal x)
{
	if (x < 0.5)
	{
		x *= 2.0;
		x = 0.5 * myworldBias(g, x);
	}
	else
	{
		x = 2.0 * x - 1.0;
		x = 0.5 * myworldBias(1.0 - g, x) + 0.5;
	}

	return x;
}

static VuoReal myworldApplyGainAndBias(VuoReal value, VuoReal gain, VuoReal bias)
{
	VuoReal b = myworldClamp(bias, 0.0, 1.0);
	VuoReal g = myworldClamp(gain, 0.0, 1.0);

	if (value > 0.999)
		return 1.0;

	if (value < 0.00001)
		return 0.0;

	if (g < 0.5)
	{
		value = myworldBias(b, value);
		value = myworldSchlickBias(g, value);
	}
	else
	{
		value = myworldSchlickBias(g, value);
		value = myworldBias(b, value);
	}

	return value;
}

static VuoReal myworldFmod(VuoReal value, VuoReal mod)
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
		VuoOutputData(VuoReal) result
)
{
	VuoReal normalized = (value - rangeInMin) / (rangeInMax - rangeInMin);

	if (normalized > 0.0 && normalized < 1.0)
		normalized = myworldApplyGainAndBias(normalized, biasAndGain.x, biasAndGain.y);

	VuoReal v = normalized * (rangeOutMax - rangeOutMin) + rangeOutMin;

	if (mode == 1)
	{
		VuoReal min = fmin(rangeOutMin, rangeOutMax);
		VuoReal max = fmax(rangeOutMin, rangeOutMax);
		v = myworldClamp(v, min, max);
	}
	else if (mode == 2)
	{
		VuoReal min = fmin(rangeOutMin, rangeOutMax);
		VuoReal max = fmax(rangeOutMin, rangeOutMax);
		v = myworldFmod(v, max - min);
	}

	*result = v;
}
