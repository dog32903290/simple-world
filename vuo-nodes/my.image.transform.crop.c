/**
 * @file
 * my.image.transform.crop node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_Crop
 * - Category: Operators/Lib/image/transform
 * - Source: external/tixl/Operators/Lib/image/transform/Crop.cs
 * - Default: Texture2d=null, LeftRight=(0,0), TopBottom=(0,0), PaddingColor=(1,1,1,0) from Crop.t3
 * - Primary output: Texture2D Output (ColorForTextures #9F008A)
 *
 * Bounded Vuo shader adapter for external/tixl/Operators/Lib/Assets/shaders/img/CropImage-cs.hlsl.
 * Vuo body-layer limit: TiXL uses a compute shader and UAV target allocation;
 * this fragment shader preserves the visible crop/pad sampling law.
 */

#include "VuoImageRenderer.h"
#include "VuoPoint2d.h"
#include <math.h>

VuoModuleMetadata({
					 "title" : "my_Crop",
					 "description" : "TiXL Crop bounded Vuo shader adapter. Source: external/tixl/Operators/Lib/image/transform/Crop.cs. Category: Operators/Lib/image/transform. Primary output: Texture2D Output (ColorForTextures #9F008A). Default: Texture2d=null, LeftRight=(0,0), TopBottom=(0,0), PaddingColor=(1,1,1,0).",
					 "keywords" : [ "tixl", "texture2d", "image", "crop", "CropImage-cs.hlsl", "bounded approximation", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform sampler2D texture2d;
	uniform vec2 sourceSize;
	uniform vec2 targetSize;
	uniform vec2 leftRight;
	uniform vec2 topBottom;
	uniform vec4 paddingColor;
	varying vec2 fragmentTextureCoordinate;

	void main()
	{
		vec2 outPixel = floor(fragmentTextureCoordinate * targetSize);
		vec2 sourcePixel = outPixel - vec2(floor(leftRight.x + 0.4), floor(topBottom.x + 0.4));
		bool outside = sourcePixel.x < 0.0 || sourcePixel.y < 0.0 || sourcePixel.x >= sourceSize.x || sourcePixel.y >= sourceSize.y;
		vec2 uv = (sourcePixel + vec2(0.5)) / sourceSize;
		gl_FragColor = outside ? paddingColor : texture2D(texture2d, uv);
	}
);

struct nodeInstanceData { VuoShader shader; };

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);
	instance->shader = VuoShader_make("my_Crop Shader");
	VuoShader_addSource(instance->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
	VuoRetain(instance->shader);
	return instance;
}

static VuoInteger targetDimension(VuoInteger source, VuoReal before, VuoReal after)
{
	VuoInteger value = source + (VuoInteger)llround(before) + (VuoInteger)llround(after);
	if (value < 1) return 1;
	if (value > 4096) return 4096;
	return value;
}

void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoImage) texture2d,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) leftRight,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) topBottom,
		VuoInputData(VuoColor, {"default":{"r":1.0,"g":1.0,"b":1.0,"a":0.0}}) paddingColor,
		VuoOutputData(VuoImage, {"name":"Output"}) output
)
{
	if (!texture2d)
	{
		*output = NULL;
		return;
	}
	VuoInteger renderWidth = targetDimension(texture2d->pixelsWide, leftRight.x, leftRight.y);
	VuoInteger renderHeight = targetDimension(texture2d->pixelsHigh, topBottom.x, topBottom.y);
	VuoShader_setUniform_VuoImage((*instance)->shader, "texture2d", texture2d);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "sourceSize", (VuoPoint2d){texture2d->pixelsWide, texture2d->pixelsHigh});
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "targetSize", (VuoPoint2d){renderWidth, renderHeight});
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "leftRight", leftRight);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "topBottom", topBottom);
	VuoShader_setUniform_VuoColor((*instance)->shader, "paddingColor", paddingColor);
	*output = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImage_getColorDepth(texture2d));
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
