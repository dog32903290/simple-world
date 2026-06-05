/**
 * @file
 * my.numbers.color.blendColors node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_BlendColors
 * - Category: Operators/Lib/numbers/color
 * - Source: external/tixl/Operators/Lib/numbers/color/BlendColors.cs
 * - Default: ColorA=(1.0,1.0,1.0,1.0), ColorB=(1.0,1.0,1.0,1.0), Factor=1.0, Mode=0
 * - Primary output: Vector4 Color (ColorForValues #868C8D)
 */

#include "VuoColor.h"

VuoModuleMetadata({
					 "title" : "my_BlendColors",
					 "description" : "Blends two colors using TiXL Mix, Multiply, Add, and Blend modes.",
					 "keywords" : [ "tixl", "numbers", "color", "blend", "mix", "multiply", "add", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
				 });

void nodeEvent
(
		VuoInputData(VuoColor, {"default":{"r":1.0,"g":1.0,"b":1.0,"a":1.0}}) colorA,
		VuoInputData(VuoColor, {"default":{"r":1.0,"g":1.0,"b":1.0,"a":1.0}}) colorB,
		VuoInputData(VuoReal, {"default":1.0}) factor,
		VuoInputData(VuoInteger, {"default":0,"suggestedMin":0,"suggestedMax":3,"suggestedStep":1}) mode,
		VuoOutputData(VuoColor, {"name":"Color"}) color
)
{
	VuoColor result = colorA;

	if (mode == 0)
	{
		result.r = colorA.r * (1.0 - factor) + colorB.r * factor;
		result.g = colorA.g * (1.0 - factor) + colorB.g * factor;
		result.b = colorA.b * (1.0 - factor) + colorB.b * factor;
		result.a = colorA.a * (1.0 - factor) + colorB.a * factor;
	}
	else if (mode == 1)
	{
		VuoColor factorColor = {
			1.0 * (1.0 - factor) + colorB.r * factor,
			1.0 * (1.0 - factor) + colorB.g * factor,
			1.0 * (1.0 - factor) + colorB.b * factor,
			1.0 * (1.0 - factor) + colorB.a * factor
		};
		result.r = colorA.r * factorColor.r;
		result.g = colorA.g * factorColor.g;
		result.b = colorA.b * factorColor.b;
		result.a = colorA.a * factorColor.a;
	}
	else if (mode == 2)
	{
		result.r = colorA.r + colorB.r * factor;
		result.g = colorA.g + colorB.g * factor;
		result.b = colorA.b + colorB.b * factor;
		result.a = colorA.a + colorB.a * factor;
	}
	else if (mode == 3)
	{
		result.r = (1.0 - colorB.a) * colorA.r + colorB.a * colorB.r;
		result.g = (1.0 - colorB.a) * colorA.g + colorB.a * colorB.g;
		result.b = (1.0 - colorB.a) * colorA.b + colorB.a * colorB.b;
		result.a = (1.0 - colorB.a) * colorA.a + colorB.a * colorB.a;
		result.a = colorA.a + colorB.a - colorA.a * colorB.a;
	}

	*color = result;
}
