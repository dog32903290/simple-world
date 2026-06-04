/**
 * @file
 * my.image.generate.basic.constantImage node implementation.
 *
 * My World runtime proof node for image.constant / top.constant.
 */

#include "VuoImage.h"

VuoModuleMetadata({
					 "title" : "my_ConstantImage",
					 "description" : "My World donor node: /Users/chenbaiwei/Projects/my-world/fixtures/runtime/top_constant_to_output.graph.json. Runtime type: image.constant; browser alias: top.constant. Category: Operators/Lib/image/generate/basic. Primary output: Texture2D / VuoImage. ColorForTextures #9F008A. This is a visible Vuo body-layer proof for a frame-local constant texture.",
					 "keywords" : [ "my-world", "runtime", "texture2d", "image", "constant", "top.constant" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

static unsigned int clampDimension(VuoInteger value)
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
		VuoInputData(VuoColor, {"name":"Color","default":{"r":0.02,"g":0.02,"b":0.02,"a":1.0}}) Color,
		VuoInputData(VuoInteger, {"name":"Width","default":1280,"suggestedMin":1,"suggestedMax":4096,"suggestedStep":16}) Width,
		VuoInputData(VuoInteger, {"name":"Height","default":720,"suggestedMin":1,"suggestedMax":4096,"suggestedStep":16}) Height,
		VuoOutputData(VuoImage, {"name":"Image"}) Image
)
{
	unsigned int renderWidth = clampDimension(Width);
	unsigned int renderHeight = clampDimension(Height);
	*Image = VuoImage_makeColorImage(Color, renderWidth, renderHeight);
}
