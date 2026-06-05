/**
 * @file
 * my.image.use.firstValidTexture node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_FirstValidTexture
 * - Category: Operators/Lib/image/use
 * - Source: external/tixl/Operators/Lib/image/use/FirstValidTexture.cs
 * - Default: Input=null from FirstValidTexture.t3
 * - Primary output: Texture2D Output (ColorForTextures #9F008A)
 *
 * TiXL scans connected textures in order and keeps the previous output if no
 * valid texture is found. Vuo has no dynamic MultiInputSlot here, so this
 * bounded adapter exposes 3 image inputs plus inputCount and previousOutput.
 */

#include "VuoImage.h"

VuoModuleMetadata({
					 "title" : "my_FirstValidTexture",
					 "description" : "TiXL FirstValidTexture bounded Vuo adapter. Source: external/tixl/Operators/Lib/image/use/FirstValidTexture.cs. Category: Operators/Lib/image/use. Primary output: Texture2D Output (ColorForTextures #9F008A). Default: Input=null.",
					 "keywords" : [ "tixl", "texture2d", "image", "first", "valid", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
				 });

static VuoInteger clampInputCount(VuoInteger inputCount)
{
	if (inputCount < 0)
		return 0;
	if (inputCount > 3)
		return 3;
	return inputCount;
}

void nodeEvent
(
		VuoInputData(VuoImage) input1,
		VuoInputData(VuoImage) input2,
		VuoInputData(VuoImage) input3,
		VuoInputData(VuoInteger, {"default":0,"suggestedMin":0,"suggestedMax":3,"suggestedStep":1}) inputCount,
		VuoInputData(VuoImage) previousOutput,
		VuoOutputData(VuoImage, {"name":"Output"}) output
)
{
	VuoInteger count = clampInputCount(inputCount);

	if (count >= 1 && input1)
	{
		*output = input1 ? input1 : previousOutput;
		return;
	}

	if (count >= 2 && input2)
	{
		*output = input2;
		return;
	}

	if (count >= 3 && input3)
	{
		*output = input3;
		return;
	}

	*output = previousOutput;
}
