/**
 * @file
 * my.image.use.swapTextures node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_SwapTextures
 * - Category: Operators/Lib/image/use
 * - Source: external/tixl/Operators/Lib/image/use/SwapTextures.cs
 * - Default: TextureAInput=null, TextureBInput=null, EnableSwap=false from SwapTextures.t3
 * - Primary output: Texture2D TextureA (ColorForTextures #9F008A)
 *
 * TiXL reads TextureBInput into the temporary A slot and TextureAInput into the
 * temporary B slot, then either swaps or passes through according to EnableSwap.
 */

#include "VuoImage.h"

VuoModuleMetadata({
					 "title" : "my_SwapTextures",
					 "description" : "TiXL SwapTextures Vuo adapter. Source: external/tixl/Operators/Lib/image/use/SwapTextures.cs. Category: Operators/Lib/image/use. Primary output: Texture2D TextureA (ColorForTextures #9F008A). Default: TextureAInput=null, TextureBInput=null, EnableSwap=false.",
					 "keywords" : [ "tixl", "texture2d", "image", "swap", "route", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
				 });

void nodeEvent
(
		VuoInputData(VuoImage) textureAInput,
		VuoInputData(VuoImage) textureBInput,
		VuoInputData(VuoBoolean, {"default":false}) enableSwap,
		VuoOutputData(VuoImage, {"name":"TextureA"}) textureA,
		VuoOutputData(VuoImage, {"name":"TextureB"}) textureB
)
{
	*textureA = enableSwap ? textureBInput : textureAInput;
	*textureB = enableSwap ? textureAInput : textureBInput;
}
