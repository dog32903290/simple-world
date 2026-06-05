/**
 * @file
 * my.numbers.color.gradientsToTexture node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_GradientsToTexture
 * - Category: Operators/Lib/numbers/color
 * - Source: external/tixl/Operators/Lib/numbers/color/GradientsToTexture.cs
 * - Default: Resolution=256, Direction=0 from GradientsToTexture.t3
 * - Primary output: Texture2D GradientsTexture (ColorForTextures #9F008A)
 *
 * Vuo bounded adapter: TiXL Gradient maps to color list + position list + interpolation enum.
 * TiXL writes R32G32B32A32_Float. This Vuo body emits an 8-bit RGBA VuoImage
 * so the texture can be visibly verified in Vuo.
 */

#include "VuoColor.h"
#include "VuoImage.h"
#include "VuoList_VuoColor.h"
#include "VuoList_VuoReal.h"
#include <OpenGL/gl.h>
#include <math.h>
#include <stdlib.h>

VuoModuleMetadata({
					 "title" : "my_GradientsToTexture",
					 "description" : "Converts bounded TiXL-style gradients into a VuoImage texture.",
					 "keywords" : [ "tixl", "numbers", "color", "gradient", "texture", "image", "ColorForTextures", "#9F008A" ],
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

static VuoInteger clampResolution(VuoInteger resolution)
{
	if (resolution < 1)
		return 1;
	if (resolution > 16384)
		return 16384;
	return resolution;
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

static VuoColor sampleGradientColor(VuoList_VuoColor colors, VuoList_VuoReal positions, VuoInteger interpolation, VuoReal samplePos)
{
	unsigned long colorCount = colors ? VuoListGetCount_VuoColor(colors) : 0;
	unsigned long positionCount = positions ? VuoListGetCount_VuoReal(positions) : 0;
	unsigned long stepCount = colorCount < positionCount ? colorCount : positionCount;
	VuoReal t = clamp01(samplePos);

	if (stepCount == 0)
		return (VuoColor){0.0, 0.0, 0.0, 0.0};

	bool hasPrevious = false;
	VuoReal previousPosition = 0.0;
	VuoColor previousColor = (VuoColor){0.0, 0.0, 0.0, 0.0};

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

		return lerpColor(previousColor, currentColor, fraction);
	}

	return hasPrevious ? previousColor : (VuoColor){0.0, 0.0, 0.0, 0.0};
}

static void writePixel(unsigned char *pixels, unsigned long index, VuoColor color)
{
	pixels[index + 0] = (unsigned char)round(clamp01(color.r) * 255.0);
	pixels[index + 1] = (unsigned char)round(clamp01(color.g) * 255.0);
	pixels[index + 2] = (unsigned char)round(clamp01(color.b) * 255.0);
	pixels[index + 3] = (unsigned char)round(clamp01(color.a) * 255.0);
}

static void sampleOneGradient(
		unsigned char *pixels,
		unsigned long width,
		unsigned long x,
		unsigned long y,
		VuoList_VuoColor colors,
		VuoList_VuoReal positions,
		VuoInteger interpolation,
		unsigned long sampleIndex,
		unsigned long sampleCount)
{
	VuoReal t = sampleCount <= 1 ? 0.0 : (VuoReal)sampleIndex / (VuoReal)(sampleCount - 1);
	writePixel(pixels, (y * width + x) * 4, sampleGradientColor(colors, positions, interpolation, t));
}

void nodeEvent
(
		VuoInputData(VuoList_VuoColor) gradient1Colors,
		VuoInputData(VuoList_VuoReal) gradient1Positions,
		VuoInputData(VuoInteger, {"default":0,"suggestedMin":0,"suggestedMax":4,"suggestedStep":1}) gradient1Interpolation,
		VuoInputData(VuoList_VuoColor) gradient2Colors,
		VuoInputData(VuoList_VuoReal) gradient2Positions,
		VuoInputData(VuoInteger, {"default":0,"suggestedMin":0,"suggestedMax":4,"suggestedStep":1}) gradient2Interpolation,
		VuoInputData(VuoList_VuoColor) gradient3Colors,
		VuoInputData(VuoList_VuoReal) gradient3Positions,
		VuoInputData(VuoInteger, {"default":0,"suggestedMin":0,"suggestedMax":4,"suggestedStep":1}) gradient3Interpolation,
		VuoInputData(VuoInteger, {"default":1,"suggestedMin":1,"suggestedMax":3,"suggestedStep":1}) inputCount,
		VuoInputData(VuoInteger, {"default":256,"suggestedMin":1,"suggestedMax":16384,"suggestedStep":1}) resolution,
		VuoInputData(VuoInteger, {"default":0,"suggestedMin":0,"suggestedMax":1,"suggestedStep":1}) direction,
		VuoOutputData(VuoImage, {"name":"GradientsTexture"}) gradientsTexture
)
{
	VuoInteger gradientCount = inputCount;
	if (gradientCount < 1)
		gradientCount = 1;
	if (gradientCount > 3)
		gradientCount = 3;

	VuoInteger sampleCount = clampResolution(resolution);
	bool useHorizontal = direction == 0;
	unsigned long width = useHorizontal ? (unsigned long)sampleCount : (unsigned long)gradientCount;
	unsigned long height = useHorizontal ? (unsigned long)gradientCount : (unsigned long)sampleCount;
	unsigned long byteCount = width * height * 4;
	unsigned char *pixels = (unsigned char *)calloc(byteCount, sizeof(unsigned char));

	for (unsigned long sample = 0; sample < (unsigned long)sampleCount; ++sample)
	{
		for (unsigned long gradient = 0; gradient < (unsigned long)gradientCount; ++gradient)
		{
			unsigned long x = useHorizontal ? sample : gradient;
			unsigned long y = useHorizontal ? gradient : sample;
			if (gradient == 0)
				sampleOneGradient(pixels, width, x, y, gradient1Colors, gradient1Positions, gradient1Interpolation, sample, (unsigned long)sampleCount);
			else if (gradient == 1)
				sampleOneGradient(pixels, width, x, y, gradient2Colors, gradient2Positions, gradient2Interpolation, sample, (unsigned long)sampleCount);
			else
				sampleOneGradient(pixels, width, x, y, gradient3Colors, gradient3Positions, gradient3Interpolation, sample, (unsigned long)sampleCount);
		}
	}

	*gradientsTexture = VuoImage_makeFromBuffer(pixels, GL_RGBA, (unsigned int)width, (unsigned int)height, VuoImageColorDepth_8, ^(void *buffer){ free(buffer); });
}
