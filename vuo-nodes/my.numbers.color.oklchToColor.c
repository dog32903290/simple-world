/**
 * @file
 * my.numbers.color.oklchToColor node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_OKLChToColor
 * - Category: Operators/Lib/numbers/color
 * - Source: external/tixl/Operators/Lib/numbers/color/OKLChToColor.cs
 * - Default: Hue=0.0, Saturation=0.0, Brightness=0.50000006, Alpha=1.0, UseGamma=false, IntensityBoost=1.0
 * - Primary output: Vector4 Color (ColorForValues #868C8D)
 *
 * UseGamma is passed through by TiXL but currently not branched in
 * external/tixl/Core/Utils/OkLab.cs FromOkLab; this Vuo body preserves that.
 */

#include "VuoColor.h"
#include <math.h>

VuoModuleMetadata({
					 "title" : "my_OKLChToColor",
					 "description" : "Converts OKLCh to a color using TiXL OkLab matrices and intensity boost.",
					 "keywords" : [ "tixl", "numbers", "color", "oklch", "oklab", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
				 });

static VuoReal clampReal(VuoReal value, VuoReal min, VuoReal max)
{
	if (value < min)
		return min;
	if (value > max)
		return max;
	return value;
}

static VuoColor toGamma(VuoColor c)
{
	return (VuoColor){
		pow(c.r, 1.0 / 2.2),
		pow(c.g, 1.0 / 2.2),
		pow(c.b, 1.0 / 2.2),
		c.a
	};
}

static VuoColor okLabToRgba(VuoReal lValue, VuoReal aValue, VuoReal bValue, VuoReal alpha)
{
	VuoReal l1 = lValue + 0.3963377774 * aValue + 0.2158037573 * bValue;
	VuoReal m1 = lValue - 0.1055613458 * aValue - 0.0638541728 * bValue;
	VuoReal s1 = lValue - 0.0894841775 * aValue - 1.2914855480 * bValue;

	VuoReal l = l1 * l1 * l1;
	VuoReal m = m1 * m1 * m1;
	VuoReal s = s1 * s1 * s1;

	return (VuoColor){
		+4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
		-1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
		-0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s,
		alpha
	};
}

static VuoColor fromOkLab(VuoReal lValue, VuoReal aValue, VuoReal bValue, VuoReal alpha)
{
	VuoReal hdrExcess = lValue > 1.0 ? lValue - 1.0 : 0.0;
	VuoColor linear = okLabToRgba(clampReal(lValue, 0.0, 1.0), aValue, bValue, alpha);
	VuoColor clampedLinear = {
		clampReal(linear.r, 0.0, 1.0),
		clampReal(linear.g, 0.0, 1.0),
		clampReal(linear.b, 0.0, 1.0),
		clampReal(linear.a, 0.0, 1.0)
	};
	VuoColor srgb = toGamma(clampedLinear);
	if (hdrExcess <= 0.0)
		return srgb;

	return (VuoColor){
		srgb.r * (1.0 + hdrExcess),
		srgb.g * (1.0 + hdrExcess),
		srgb.b * (1.0 + hdrExcess),
		srgb.a
	};
}

void nodeEvent
(
		VuoInputData(VuoReal, {"default":0.0}) hue,
		VuoInputData(VuoReal, {"default":0.0}) saturation,
		VuoInputData(VuoReal, {"default":0.50000006}) brightness,
		VuoInputData(VuoReal, {"default":1.0}) alpha,
		VuoInputData(VuoBoolean, {"default":false}) useGamma,
		VuoInputData(VuoReal, {"default":1.0}) intensityBoost,
		VuoOutputData(VuoColor, {"name":"Color"}) color
)
{
	VuoReal hDegrees = fmod(hue, 1.0) * 360.0;
	VuoReal h = hDegrees * (M_PI / 180.0);
	VuoReal a = saturation * cos(h);
	VuoReal b = saturation * sin(h);

	VuoColor result = fromOkLab(brightness, a, b, alpha);
	result.r *= intensityBoost;
	result.g *= intensityBoost;
	result.b *= intensityBoost;

	(void)useGamma;
	*color = result;
}
