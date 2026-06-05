/**
 * @file
 * my.numbers.color.defineGradient node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_DefineGradient
 * - Category: Operators/Lib/numbers/color
 * - Source: external/tixl/Operators/Lib/numbers/color/DefineGradient.cs
 * - Default: Color1Pos=0.0, Color2Pos=1.0, Color3Pos=-1.0, Color4Pos=-1.0, Interpolation=0 from DefineGradient.t3
 * - Primary output: Gradient OutGradient (ColorForValues #868C8D)
 *
 * Vuo bounded adapter: TiXL Gradient maps to color list + position list + interpolation enum.
 * Negative positions are skipped. If all positions are negative, TiXL falls
 * back to Color1 at position 0.
 */

#include "VuoColor.h"
#include "VuoList_VuoColor.h"
#include "VuoList_VuoReal.h"
#include <stdlib.h>

VuoModuleMetadata({
					 "title" : "my_DefineGradient",
					 "description" : "Defines a TiXL-style gradient payload from up to four color handles.",
					 "keywords" : [ "tixl", "numbers", "color", "gradient", "define", "ColorForValues", "#868C8D" ],
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

static void appendStepIfEnabled(GradientStep *steps, unsigned long *count, VuoColor color, VuoReal position)
{
	if (position < 0.0)
		return;

	steps[*count].color = color;
	steps[*count].position = position;
	++(*count);
}

void nodeEvent
(
		VuoInputData(VuoColor, {"default":{"r":0.000001,"g":0.000001,"b":0.000001,"a":1.0}}) color1,
		VuoInputData(VuoReal, {"default":0.0}) color1Pos,
		VuoInputData(VuoColor, {"default":{"r":1.0,"g":1.0,"b":1.0,"a":1.0}}) color2,
		VuoInputData(VuoReal, {"default":1.0}) color2Pos,
		VuoInputData(VuoColor, {"default":{"r":1.0,"g":0.0,"b":1.0,"a":0.0}}) color3,
		VuoInputData(VuoReal, {"default":-1.0}) color3Pos,
		VuoInputData(VuoColor, {"default":{"r":1.0,"g":0.0,"b":1.0,"a":0.0}}) color4,
		VuoInputData(VuoReal, {"default":-1.0}) color4Pos,
		VuoInputData(VuoInteger, {"default":0,"suggestedMin":0,"suggestedMax":4,"suggestedStep":1}) interpolation,
		VuoOutputData(VuoList_VuoColor, {"name":"Gradient Colors"}) gradientColors,
		VuoOutputData(VuoList_VuoReal, {"name":"Gradient Positions"}) gradientPositions,
		VuoOutputData(VuoInteger, {"name":"Gradient Interpolation"}) gradientInterpolation
)
{
	GradientStep steps[4];
	unsigned long stepCount = 0;

	appendStepIfEnabled(steps, &stepCount, color1, color1Pos);
	appendStepIfEnabled(steps, &stepCount, color2, color2Pos);
	appendStepIfEnabled(steps, &stepCount, color3, color3Pos);
	appendStepIfEnabled(steps, &stepCount, color4, color4Pos);

	if (stepCount == 0)
	{
		// TiXL fallback to Color1 when every handle is disabled.
		steps[0].color = color1;
		steps[0].position = 0.0;
		stepCount = 1;
	}

	qsort(steps, stepCount, sizeof(GradientStep), compareGradientSteps);

	VuoList_VuoColor sortedColors = VuoListCreate_VuoColor();
	VuoList_VuoReal sortedPositions = VuoListCreate_VuoReal();
	for (unsigned long i = 0; i < stepCount; ++i)
	{
		VuoListAppendValue_VuoColor(sortedColors, steps[i].color);
		VuoListAppendValue_VuoReal(sortedPositions, steps[i].position);
	}

	*gradientColors = sortedColors;
	*gradientPositions = sortedPositions;
	*gradientInterpolation = interpolation;
}
