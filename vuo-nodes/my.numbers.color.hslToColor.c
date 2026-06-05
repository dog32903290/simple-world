/**
 * @file
 * my.numbers.color.hslToColor node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_HSLToColor
 * - Category: Operators/Lib/numbers/color
 * - Source: external/tixl/Operators/Lib/numbers/color/HSLToColor.cs
 * - Default: Hue=0.0, Saturation=0.0, Lightness=0.50000006, Alpha=1.0 from HSLToColor.t3
 * - Primary output: Vector4 Color (ColorForValues #868C8D)
 *
 * This keeps TiXL custom HSL formula rather than replacing it with a library
 * conversion.
 */

#include "VuoColor.h"
#include <math.h>

VuoModuleMetadata({
					 "title" : "my_HSLToColor",
					 "description" : "Converts TiXL normalized hue, saturation, lightness, and alpha into a color using TiXL's custom HSL formula.",
					 "keywords" : [ "tixl", "numbers", "color", "hsl", "lightness", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
				 });

void nodeEvent
(
		VuoInputData(VuoReal, {"default":0.0}) hue,
		VuoInputData(VuoReal, {"default":0.0}) saturation,
		VuoInputData(VuoReal, {"default":0.50000006}) lightness,
		VuoInputData(VuoReal, {"default":1.0}) alpha,
		VuoOutputData(VuoColor, {"name":"Color"}) color
)
{
	VuoReal h = fmod(hue, 1.0) * 360.0;

	VuoReal satR = 1.0;
	VuoReal satG = 1.0;
	VuoReal satB = 1.0;
	if (h < 120.0)
	{
		satR = (120.0 - h) / 60.0;
		satG = h / 60.0;
		satB = 0.0;
	}
	else if (h < 240.0)
	{
		satR = 0.0;
		satG = (240.0 - h) / 60.0;
		satB = (h - 120.0) / 60.0;
	}
	else
	{
		satR = (h - 240.0) / 60.0;
		satG = 0.0;
		satB = (360.0 - h) / 60.0;
	}

	satR = satR < 1.0 ? satR : 1.0;
	satG = satG < 1.0 ? satG : 1.0;
	satB = satB < 1.0 ? satB : 1.0;

	VuoReal tmpR = 2.0 * saturation * satR + (1.0 - saturation);
	VuoReal tmpG = 2.0 * saturation * satG + (1.0 - saturation);
	VuoReal tmpB = 2.0 * saturation * satB + (1.0 - saturation);

	VuoReal r;
	VuoReal g;
	VuoReal b;
	if (lightness < 0.5)
	{
		r = lightness * tmpR;
		g = lightness * tmpG;
		b = lightness * tmpB;
	}
	else
	{
		r = (1.0 - lightness) * tmpR + 2.0 * lightness - 1.0;
		g = (1.0 - lightness) * tmpG + 2.0 * lightness - 1.0;
		b = (1.0 - lightness) * tmpB + 2.0 * lightness - 1.0;
	}

	*color = (VuoColor){r, g, b, alpha};
}
