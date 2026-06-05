/**
 * @file
 * my.image.use.pickTexture node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_PickTexture
 * - Category: Operators/Lib/image/use
 * - Source: external/tixl/Operators/Lib/image/use/PickTexture.cs
 * - Default: Index=0, Input=null from PickTexture.t3
 * - Primary output: Texture2D Selected (ColorForTextures #9F008A)
 *
 * TiXL picks from a MultiInputSlot by positive modulo and keeps the previous
 * Selected value when no textures are connected. Vuo exposes a bounded 3-input
 * form with explicit previousSelected.
 */

#include "VuoImage.h"

VuoModuleMetadata({
					 "title" : "my_PickTexture",
					 "description" : "TiXL PickTexture bounded Vuo adapter. Source: external/tixl/Operators/Lib/image/use/PickTexture.cs. Category: Operators/Lib/image/use. Primary output: Texture2D Selected (ColorForTextures #9F008A). Default: Index=0, Input=null.",
					 "keywords" : [ "tixl", "texture2d", "image", "pick", "modulo", "ColorForTextures", "#9F008A" ],
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

static VuoInteger positiveModulo(VuoInteger value, VuoInteger divisor)
{
	if (divisor <= 0)
		return 0;
	VuoInteger remainder = value % divisor;
	return remainder < 0 ? remainder + divisor : remainder;
}

void nodeEvent
(
		VuoInputData(VuoImage) input1,
		VuoInputData(VuoImage) input2,
		VuoInputData(VuoImage) input3,
		VuoInputData(VuoInteger, {"default":0,"suggestedMin":0,"suggestedMax":3,"suggestedStep":1}) inputCount,
		VuoInputData(VuoInteger, {"default":0,"suggestedStep":1}) index,
		VuoInputData(VuoImage) previousSelected,
		VuoOutputData(VuoImage, {"name":"Selected"}) selected
)
{
	VuoInteger count = clampInputCount(inputCount);
	if (count == 0)
	{
		*selected = previousSelected;
		return;
	}

	VuoInteger picked = positiveModulo(index, count);
	if (picked == 0)
		*selected = input1;
	else if (picked == 1)
		*selected = input2;
	else
		*selected = input3;
}
