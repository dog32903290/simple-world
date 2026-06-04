/**
 * @file
 * my.render.dx11.api.clearRenderTarget node implementation.
 *
 * Vuo body-layer proof adapter for TiXL ClearRenderTarget.
 */

#include "VuoImage.h"
#include "VuoText.h"

VuoModuleMetadata({
					 "title" : "my_ClearRenderTarget",
					 "description" : "TiXL ClearRenderTarget body-layer proof. Source: external/tixl/Operators/Lib/render/_dx11/api/ClearRenderTarget.cs. Category: Operators/Lib/render/_dx11/api. Primary output in TiXL: Command / ColorForCommands #22B8C2. Vuo proof output: cleared Texture2D / VuoImage plus trace text. This does not prove RTV/DSV TextureView identity, command stream prepare/update/restore, or native GPU clearing.",
					 "keywords" : [ "tixl", "command", "clear", "render target", "dx11", "texture2d" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

static unsigned int clampRenderDimension(VuoInteger value)
{
	if (value < 1)
		return 1;
	if (value > 8192)
		return 8192;
	return (unsigned int)value;
}

void nodeEvent
(
		VuoInputEvent() renderTick,
		VuoInputData(VuoBoolean, {"name":"TargetValid","default":true}) TargetValid,
		VuoInputData(VuoColor, {"name":"ClearColor","default":{"r":0.02,"g":0.72,"b":0.76,"a":1.0}}) ClearColor,
		VuoInputData(VuoInteger, {"name":"Width","default":960,"suggestedMin":1,"suggestedMax":4096,"suggestedStep":16}) Width,
		VuoInputData(VuoInteger, {"name":"Height","default":540,"suggestedMin":1,"suggestedMax":4096,"suggestedStep":16}) Height,
		VuoOutputData(VuoImage, {"name":"ClearedColorBuffer"}) ClearedColorBuffer,
		VuoOutputData(VuoText, {"name":"CommandTrace"}) CommandTrace,
		VuoOutputData(VuoBoolean, {"name":"IsValid"}) IsValid
)
{
	if (!TargetValid)
	{
		*ClearedColorBuffer = NULL;
		*CommandTrace = VuoText_make("my_ClearRenderTarget: invalid target; no clear image emitted");
		*IsValid = false;
		return;
	}

	unsigned int renderWidth = clampRenderDimension(Width);
	unsigned int renderHeight = clampRenderDimension(Height);
	*ClearedColorBuffer = VuoImage_makeColorImage(ClearColor, renderWidth, renderHeight);
	*CommandTrace = VuoText_make("prepare:ClearRenderTarget -> update:clear color buffer -> restore:ClearRenderTarget");
	*IsValid = true;
}
