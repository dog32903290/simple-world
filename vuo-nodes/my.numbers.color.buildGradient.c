/**
 * @file
 * my.numbers.color.buildGradient node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_BuildGradient
 * - Category: Operators/Lib/numbers/color
 * - Source: external/tixl/Operators/Lib/numbers/color/BuildGradient.cs
 * - Default: Colors=[black, white], Positions=[], Interpolation=3 from BuildGradient.t3
 * - Primary output: Gradient OutGradient (ColorForValues #868C8D)
 *
 * Vuo bounded adapter: TiXL Gradient maps to color list + position list + interpolation enum.
 * The full TiXL Gradient object has no direct Vuo type, so this node outputs the
 * sorted gradient payload as semantic lists for downstream proof nodes.
 */

#include "VuoColor.h"
#include "VuoList_VuoColor.h"
#include "VuoList_VuoReal.h"
#include <stdlib.h>

VuoModuleMetadata({
					 "title" : "my_BuildGradient",
					 "description" : "Builds a TiXL-style gradient payload from color and position lists.",
					 "keywords" : [ "tixl", "numbers", "color", "gradient", "build", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
				 });

typedef struct
{
	VuoReal position;
	VuoColor color;
} GradientStep;

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

static void sortGradientSteps(GradientStep *steps, unsigned long count)
{
	qsort(steps, count, sizeof(GradientStep), compareGradientSteps);
}

static void appendNormalizedPositions(VuoList_VuoReal output, unsigned long colorCount)
{
	if (colorCount == 0)
		return;

	if (colorCount == 1)
	{
		VuoListAppendValue_VuoReal(output, 0.0);
		return;
	}

	for (unsigned long i = 0; i < colorCount; ++i)
		VuoListAppendValue_VuoReal(output, (VuoReal)i / (VuoReal)(colorCount - 1));
}

void nodeEvent
(
		VuoInputData(VuoList_VuoColor) colors,
		VuoInputData(VuoList_VuoReal) positions,
		VuoInputData(VuoInteger, {"default":3,"suggestedMin":0,"suggestedMax":4,"suggestedStep":1}) interpolation,
		VuoOutputData(VuoList_VuoColor, {"name":"Gradient Colors"}) gradientColors,
		VuoOutputData(VuoList_VuoReal, {"name":"Gradient Positions"}) gradientPositions,
		VuoOutputData(VuoInteger, {"name":"Gradient Interpolation"}) gradientInterpolation
)
{
	unsigned long colorCount = colors ? VuoListGetCount_VuoColor(colors) : 0;
	unsigned long positionCount = positions ? VuoListGetCount_VuoReal(positions) : 0;

	VuoList_VuoReal effectivePositions = VuoListCreate_VuoReal();
	if (positionCount == 0)
		appendNormalizedPositions(effectivePositions, colorCount);
	else
	{
		for (unsigned long i = 1; i <= positionCount; ++i)
			VuoListAppendValue_VuoReal(effectivePositions, VuoListGetValue_VuoReal(positions, i));
	}

	positionCount = VuoListGetCount_VuoReal(effectivePositions);
	unsigned long stepCount = colorCount < positionCount ? colorCount : positionCount;

	VuoList_VuoColor sortedColors = VuoListCreate_VuoColor();
	VuoList_VuoReal sortedPositions = VuoListCreate_VuoReal();

	if (stepCount > 0)
	{
		GradientStep *steps = (GradientStep *)calloc(stepCount, sizeof(GradientStep));
		for (unsigned long i = 0; i < stepCount; ++i)
		{
			steps[i].color = VuoListGetValue_VuoColor(colors, i + 1);
			steps[i].position = VuoListGetValue_VuoReal(effectivePositions, i + 1);
		}

		sortGradientSteps(steps, stepCount);

		for (unsigned long i = 0; i < stepCount; ++i)
		{
			VuoListAppendValue_VuoColor(sortedColors, steps[i].color);
			VuoListAppendValue_VuoReal(sortedPositions, steps[i].position);
		}

		free(steps);
	}

	*gradientColors = sortedColors;
	*gradientPositions = sortedPositions;
	*gradientInterpolation = interpolation;
}
