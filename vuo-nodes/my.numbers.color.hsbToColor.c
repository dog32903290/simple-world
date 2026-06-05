/**
 * @file
 * my.numbers.color.hsbToColor node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_HSBToColor
 * - Category: Operators/Lib/numbers/color
 * - Source: external/tixl/Operators/Lib/numbers/color/HSBToColor.cs
 * - Default: Hue=0.0, Saturation=0.0, Brightness=0.50000006, Alpha=1.0 from HSBToColor.t3
 * - Primary output: Vector4 Color (ColorForValues #868C8D)
 */

#include "VuoColor.h"
#include <math.h>

VuoModuleMetadata({
					 "title" : "my_HSBToColor",
					 "description" : "Converts TiXL hue degrees, saturation, brightness, and alpha into a color.",
					 "keywords" : [ "tixl", "numbers", "color", "hsb", "hsv", "brightness", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
				 });

void nodeEvent
(
		VuoInputData(VuoReal, {"default":0.0}) hue,
		VuoInputData(VuoReal, {"default":0.0}) saturation,
		VuoInputData(VuoReal, {"default":0.50000006}) brightness,
		VuoInputData(VuoReal, {"default":1.0}) alpha,
		VuoOutputData(VuoColor, {"name":"Color"}) color
)
{
	VuoReal h = fmod(hue, 360.0);
	VuoReal r = 0.0;
	VuoReal g = 0.0;
	VuoReal b = 0.0;

	if (saturation == 0.0)
	{
		r = brightness;
		g = brightness;
		b = brightness;
	}
	else
	{
		h = fmod(h, 360.0);
		if (hue < 0.0)
			h += 360.0;

		int sector = (int)(h / 60.0);
		VuoReal fractional = (h / 60.0) - sector;
		VuoReal p = brightness * (1.0 - saturation);
		VuoReal q = brightness * (1.0 - saturation * fractional);
		VuoReal t = brightness * (1.0 - saturation * (1.0 - fractional));

		if (sector == 0)
		{
			r = brightness; g = t; b = p;
		}
		else if (sector == 1)
		{
			r = q; g = brightness; b = p;
		}
		else if (sector == 2)
		{
			r = p; g = brightness; b = t;
		}
		else if (sector == 3)
		{
			r = p; g = q; b = brightness;
		}
		else if (sector == 4)
		{
			r = t; g = p; b = brightness;
		}
		else if (sector == 5)
		{
			r = brightness; g = p; b = q;
		}
	}

	*color = (VuoColor){r, g, b, alpha};
}
