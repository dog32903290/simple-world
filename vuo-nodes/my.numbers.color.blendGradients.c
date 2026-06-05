/**
 * @file
 * my.numbers.color.blendGradients node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_BlendGradients
 * - Category: Operators/Lib/numbers/color
 * - Source: external/tixl/Operators/Lib/numbers/color/BlendGradients.cs
 * - Default: BlendMode=3, MixFactor=0.0 from BlendGradients.t3
 * - Primary output: Gradient Result (ColorForValues #868C8D)
 *
 * Vuo bounded adapter: TiXL Gradient maps to color list + position list + interpolation enum.
 * The result interpolation is always Linear, matching TiXL's new Gradient result.
 */

#include "VuoColor.h"
#include "VuoList_VuoColor.h"
#include "VuoList_VuoReal.h"
#include <stdlib.h>

VuoModuleMetadata({
					 "title" : "my_BlendGradients",
					 "description" : "Blends two bounded TiXL-style gradient payloads over their union of handle positions.",
					 "keywords" : [ "tixl", "numbers", "color", "gradient", "blend", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
				 });

typedef struct
{
	VuoReal position;
	VuoColor color;
} GradientStep;

static VuoReal clamp01(VuoReal value)
{
	if (value < 0.0)
		return 0.0;
	if (value > 1.0)
		return 1.0;
	return value;
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

static VuoColor sampleGradientColor(VuoList_VuoColor colors, VuoList_VuoReal positions, VuoReal samplePos)
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

		VuoReal fraction = clamp01((t - previousPosition) / (position - previousPosition));
		return lerpColor(previousColor, currentColor, fraction);
	}

	return hasPrevious ? previousColor : (VuoColor){1.0, 1.0, 1.0, 1.0};
}

static VuoColor blendGradientColor(VuoColor a, VuoColor b, VuoInteger blendMode, VuoReal mixFactor)
{
	switch (blendMode)
	{
		case 0:
			return (VuoColor){
				(1.0 - b.a) * a.r + b.a * b.r,
				(1.0 - b.a) * a.g + b.a * b.g,
				(1.0 - b.a) * a.b + b.a * b.b,
				a.a + b.a - a.a * b.a
			};
		case 1:
			return (VuoColor){a.r * b.r, a.g * b.g, a.b * b.b, a.a + b.a - a.a * b.a};
		case 2:
			return (VuoColor){
				1.0 - (1.0 - a.r) * (1.0 - b.r),
				1.0 - (1.0 - a.g) * (1.0 - b.g),
				1.0 - (1.0 - a.b) * (1.0 - b.b),
				a.a + b.a - a.a * b.a
			};
		case 3:
			return lerpColor(a, b, mixFactor);
	}

	return (VuoColor){1.0, 1.0, 1.0, 1.0};
}

static int compareGradientSteps(const void *a, const void *b)
{
	const GradientStep *left = (const GradientStep *)a;
	const GradientStep *right = (const GradientStep *)b;
	if (left->position < right->position)
		return -1;
	if (left->position > right->position)
		return 1;
	return 0;
}

static void mergeStep(GradientStep *steps, unsigned long *count, VuoReal position, VuoColor color)
{
	for (unsigned long i = 0; i < *count; ++i)
	{
		if (steps[i].position == position)
		{
			steps[i].color = color;
			return;
		}
	}

	steps[*count].position = position;
	steps[*count].color = color;
	++(*count);
}

void nodeEvent
(
		VuoInputData(VuoList_VuoColor) gradientAColors,
		VuoInputData(VuoList_VuoReal) gradientAPositions,
		VuoInputData(VuoInteger, {"default":0,"suggestedMin":0,"suggestedMax":4,"suggestedStep":1}) gradientAInterpolation,
		VuoInputData(VuoList_VuoColor) gradientBColors,
		VuoInputData(VuoList_VuoReal) gradientBPositions,
		VuoInputData(VuoInteger, {"default":0,"suggestedMin":0,"suggestedMax":4,"suggestedStep":1}) gradientBInterpolation,
		VuoInputData(VuoInteger, {"default":3,"suggestedMin":0,"suggestedMax":3,"suggestedStep":1}) blendMode,
		VuoInputData(VuoReal, {"default":0.0,"suggestedMin":0.0,"suggestedMax":1.0,"suggestedStep":0.01}) mixFactor,
		VuoOutputData(VuoList_VuoColor, {"name":"Result Colors"}) resultColors,
		VuoOutputData(VuoList_VuoReal, {"name":"Result Positions"}) resultPositions,
		VuoOutputData(VuoInteger, {"name":"Result Interpolation"}) resultInterpolation
)
{
	(void)gradientAInterpolation;
	(void)gradientBInterpolation;

	unsigned long countA = gradientAColors && gradientAPositions ? VuoListGetCount_VuoColor(gradientAColors) : 0;
	unsigned long posCountA = gradientAPositions ? VuoListGetCount_VuoReal(gradientAPositions) : 0;
	if (posCountA < countA)
		countA = posCountA;
	unsigned long countB = gradientBColors && gradientBPositions ? VuoListGetCount_VuoColor(gradientBColors) : 0;
	unsigned long posCountB = gradientBPositions ? VuoListGetCount_VuoReal(gradientBPositions) : 0;
	if (posCountB < countB)
		countB = posCountB;

	unsigned long capacity = countA + countB;
	VuoList_VuoColor colors = VuoListCreate_VuoColor();
	VuoList_VuoReal positions = VuoListCreate_VuoReal();
	if (capacity == 0)
	{
		*resultColors = colors;
		*resultPositions = positions;
		*resultInterpolation = 0;
		return;
	}

	GradientStep *steps = (GradientStep *)calloc(capacity, sizeof(GradientStep));
	unsigned long stepCount = 0;
	VuoReal clampedMix = clamp01(mixFactor);

	for (unsigned long i = 1; i <= countA; ++i)
	{
		VuoReal position = VuoListGetValue_VuoReal(gradientAPositions, i);
		VuoColor colorA = VuoListGetValue_VuoColor(gradientAColors, i);
		VuoColor colorB = sampleGradientColor(gradientBColors, gradientBPositions, position);
		mergeStep(steps, &stepCount, position, blendGradientColor(colorA, colorB, blendMode, clampedMix));
	}

	for (unsigned long i = 1; i <= countB; ++i)
	{
		VuoReal position = VuoListGetValue_VuoReal(gradientBPositions, i);
		VuoColor colorB = VuoListGetValue_VuoColor(gradientBColors, i);
		VuoColor colorA = sampleGradientColor(gradientAColors, gradientAPositions, position);
		mergeStep(steps, &stepCount, position, blendGradientColor(colorA, colorB, blendMode, clampedMix));
	}

	qsort(steps, stepCount, sizeof(GradientStep), compareGradientSteps);

	for (unsigned long i = 0; i < stepCount; ++i)
	{
		VuoListAppendValue_VuoColor(colors, steps[i].color);
		VuoListAppendValue_VuoReal(positions, steps[i].position);
	}

	free(steps);

	*resultColors = colors;
	*resultPositions = positions;
	// TiXL result interpolation is always Linear.
	*resultInterpolation = 0;
}
