/**
 * @file
 * my.image.use.useFallbackTexture node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_UseFallbackTexture
 * - Category: Operators/Lib/image/use
 * - Source: external/tixl/Operators/Lib/image/use/UseFallbackTexture.cs
 * - Default: TextureA=null, Fallback=null from UseFallbackTexture.t3
 * - Primary output: Texture2D Output (ColorForTextures #9F008A)
 *
 * Vuo bounded adapter: TiXL caches Fallback until that input is dirty. This
 * body-layer node has no TiXL DirtyFlag, so it uses the current fallback value
 * directly while preserving the visible TextureA-or-Fallback routing law.
 */

#include "VuoImage.h"

VuoModuleMetadata({
					 "title" : "my_UseFallbackTexture",
					 "description" : "TiXL UseFallbackTexture bounded adapter. Source: external/tixl/Operators/Lib/image/use/UseFallbackTexture.cs. Category: Operators/Lib/image/use. Primary output: Texture2D Output (ColorForTextures #9F008A). Default: TextureA=null, Fallback=null.",
					 "keywords" : [ "tixl", "texture2d", "image", "fallback", "route", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
				 });

void nodeEvent
(
		VuoInputData(VuoImage) textureA,
		VuoInputData(VuoImage) fallback,
		VuoOutputData(VuoImage, {"name":"Output"}) output
)
{
	*output = textureA ? textureA : fallback;
}
