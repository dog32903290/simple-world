/**
 * @file
 * my.numbers.color.sampleGradient node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_SampleGradient
 * - Category: Operators/Lib/numbers/color
 * - Source: external/tixl/Operators/Lib/numbers/color/SampleGradient.cs
 * - Default: SamplePos=0.0, OverrideInterpolation=false, Interpolation=0 from SampleGradient.t3
 * - Primary output: Vector4 Color (ColorForValues #868C8D)
 *
 * Vuo bounded adapter: TiXL Gradient maps to color list + position list + interpolation enum.
 * Color sampling is exact for Linear, Hold, Smooth, and OkLab. Spline mode is adapter-bounded
 * and falls back to Linear until the TiXL CubicSpline contract gets a native runtime body.
 */

#include "VuoColor.h"
#include "VuoList_VuoColor.h"
#include "VuoList_VuoReal.h"
#include <math.h>

VuoModuleMetadata({
					 "title" : "my_SampleGradient",
					 "description" : "Samples a bounded TiXL-style gradient payload.",
					 "keywords" : [ "tixl", "numbers", "color", "gradient", "sample", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
				 });

static VuoReal clamp01(VuoReal value)
{
	if (value < 0.0)
		return 0.0;
	if (value > 1.0)
		return 1.0;
	return value;
}

static VuoReal smootherStep(VuoReal value)
{
	VuoReal t = clamp01(value);
	return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

static VuoReal lerpReal(VuoReal a, VuoReal b, VuoReal t)
{
	return a + (b - a) * t;
}

static VuoColor lerpColor(VuoColor a, VuoColor b, VuoReal t)
{
	return (VuoColor){
		lerpReal(a.r, b.r, t),
		lerpReal(a.g, b.g, t),
		lerpReal(a.b, b.b, t),
		lerpReal(a.a, b.a, t)
	};
}

static VuoColor colorMaxZero(VuoColor c)
{
	return (VuoColor){
		c.r < 0.0 ? 0.0 : c.r,
		c.g < 0.0 ? 0.0 : c.g,
		c.b < 0.0 ? 0.0 : c.b,
		c.a < 0.0 ? 0.0 : c.a
	};
}

static VuoColor degamma(VuoColor c)
{
	return (VuoColor){pow(c.r, 2.2), pow(c.g, 2.2), pow(c.b, 2.2), c.a};
}

static VuoColor toGamma(VuoColor c)
{
	return (VuoColor){pow(c.r, 1.0 / 2.2), pow(c.g, 1.0 / 2.2), pow(c.b, 1.0 / 2.2), c.a};
}

static VuoColor rgbAToOkLab(VuoColor c)
{
	double cr = c.r;
	double cg = c.g;
	double cb = c.b;
	double l = 0.4122214708 * cr + 0.5363325363 * cg + 0.0514459929 * cb;
	double m = 0.2119034982 * cr + 0.6806995451 * cg + 0.1073969566 * cb;
	double s = 0.0883024619 * cr + 0.2817188376 * cg + 0.6299787005 * cb;
	double lCbrt = pow(l, 1.0 / 3.0);
	double mCbrt = pow(m, 1.0 / 3.0);
	double sCbrt = pow(s, 1.0 / 3.0);
	return (VuoColor){
		0.2104542553 * lCbrt + 0.793617785 * mCbrt - 0.0040720468 * sCbrt,
		1.9779984951 * lCbrt - 2.428592205 * mCbrt + 0.4505937099 * sCbrt,
		0.0259040371 * lCbrt + 0.7827717662 * mCbrt - 0.808675766 * sCbrt,
		c.a
	};
}

static VuoColor okLabToRgba(VuoColor c)
{
	VuoReal l1 = c.r + 0.3963377774 * c.g + 0.2158037573 * c.b;
	VuoReal m1 = c.r - 0.1055613458 * c.g - 0.0638541728 * c.b;
	VuoReal s1 = c.r - 0.0894841775 * c.g - 1.2914855480 * c.b;
	VuoReal l = l1 * l1 * l1;
	VuoReal m = m1 * m1 * m1;
	VuoReal s = s1 * s1 * s1;
	return (VuoColor){
		+4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
		-1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
		-0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s,
		c.a
	};
}

static VuoColor mixOkLab(VuoColor a, VuoColor b, VuoReal t)
{
	VuoColor labA = rgbAToOkLab(degamma(colorMaxZero(a)));
	VuoColor labB = rgbAToOkLab(degamma(colorMaxZero(b)));
	return toGamma(okLabToRgba(lerpColor(labA, labB, t)));
}

static VuoColor sampleGradientColor(VuoList_VuoColor colors, VuoList_VuoReal positions, VuoInteger interpolation, VuoReal samplePos)
{
	unsigned long colorCount = colors ? VuoListGetCount_VuoColor(colors) : 0;
	unsigned long positionCount = positions ? VuoListGetCount_VuoReal(positions) : 0;
	unsigned long stepCount = colorCount < positionCount ? colorCount : positionCount;
	VuoReal t = clamp01(samplePos);

	if (stepCount == 0)
		return (VuoColor){1.0, 1.0, 1.0, 1.0};

	bool hasPrevious = false;
	VuoReal previousPosition = 0.0;
	VuoColor previousColor = (VuoColor){1.0, 1.0, 1.0, 1.0};

	for (unsigned long i = 1; i <= stepCount; ++i)
	{
		VuoReal position = VuoListGetValue_VuoReal(positions, i);
		VuoColor currentColor = VuoListGetValue_VuoColor(colors, i);

		if (!(position >= t))
		{
			hasPrevious = true;
			previousPosition = position;
			previousColor = currentColor;
			continue;
		}

		if (!hasPrevious || previousPosition >= position)
			return currentColor;

		if (interpolation == 1)
			return previousColor;

		VuoReal fraction = clamp01((t - previousPosition) / (position - previousPosition));
		if (interpolation == 2)
			fraction = smootherStep(fraction);

		if (interpolation == 3)
			return mixOkLab(previousColor, currentColor, fraction);

		return lerpColor(previousColor, currentColor, fraction);
	}

	return hasPrevious ? previousColor : (VuoColor){1.0, 1.0, 1.0, 1.0};
}

void nodeEvent
(
		VuoInputData(VuoReal, {"default":0.0}) samplePos,
		VuoInputData(VuoList_VuoColor) gradientColors,
		VuoInputData(VuoList_VuoReal) gradientPositions,
		VuoInputData(VuoInteger, {"default":0,"suggestedMin":0,"suggestedMax":4,"suggestedStep":1}) gradientInterpolation,
		VuoInputData(VuoBoolean, {"default":false}) overrideInterpolation,
		VuoInputData(VuoInteger, {"default":0,"suggestedMin":0,"suggestedMax":4,"suggestedStep":1}) interpolation,
		VuoOutputData(VuoColor, {"name":"Color"}) color,
		VuoOutputData(VuoList_VuoColor, {"name":"Out Gradient Colors"}) outGradientColors,
		VuoOutputData(VuoList_VuoReal, {"name":"Out Gradient Positions"}) outGradientPositions,
		VuoOutputData(VuoInteger, {"name":"Out Gradient Interpolation"}) outGradientInterpolation
)
{
	VuoInteger effectiveInterpolation = overrideInterpolation ? interpolation : gradientInterpolation;
	*color = sampleGradientColor(gradientColors, gradientPositions, effectiveInterpolation, samplePos);
	*outGradientColors = gradientColors;
	*outGradientPositions = gradientPositions;
	*outGradientInterpolation = effectiveInterpolation;
}
