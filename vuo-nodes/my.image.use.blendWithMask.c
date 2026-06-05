/**
 * @file
 * my.image.use.blendWithMask node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_BlendWithMask
 * - Category: Operators/Lib/image/use
 * - Source: external/tixl/Operators/Lib/image/use/BlendWithMask.cs
 * - Default: ImageA=null, ImageB=null, Mask=null, Resolution=(0,0) from BlendWithMask.t3
 * - Primary output: Texture2D Output (ColorForTextures #9F008A)
 *
 * Bounded Vuo shader adapter for external/tixl/Operators/Lib/Assets/shaders/img/fx/BlendWithMask.hlsl.
 */

#include "VuoImageRenderer.h"
#include "VuoPoint2d.h"
#include <math.h>

VuoModuleMetadata({
					 "title" : "my_BlendWithMask",
					 "description" : "TiXL BlendWithMask Vuo shader adapter. Source: external/tixl/Operators/Lib/image/use/BlendWithMask.cs. Category: Operators/Lib/image/use. Primary output: Texture2D Output (ColorForTextures #9F008A). Default: ImageA=null, ImageB=null, Mask=null, Resolution=(0,0).",
					 "keywords" : [ "tixl", "texture2d", "image", "mask", "blend", "BlendWithMask.hlsl", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform sampler2D imageA;
	uniform sampler2D imageB;
	uniform sampler2D mask;
	uniform vec4 colorA;
	uniform vec4 colorB;
	varying vec2 fragmentTextureCoordinate;

	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		vec4 a = texture2D(imageA, st) * colorA;
		vec4 b = texture2D(imageB, st) * colorB;
		float maskValue = texture2D(mask, st).r; // mask.r
		gl_FragColor = mix(a, b, maskValue);
	}
);

struct nodeInstanceData
{
	VuoShader shader;
};

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);

	instance->shader = VuoShader_make("my_BlendWithMask Shader");
	VuoShader_addSource(instance->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
	VuoRetain(instance->shader);

	return instance;
}

static VuoInteger renderDimension(VuoPoint2d resolution, VuoImage primary, bool width)
{
	VuoInteger requested = (VuoInteger)llround(width ? resolution.x : resolution.y);
	if (requested > 0)
		return requested;
	if (primary)
		return width ? primary->pixelsWide : primary->pixelsHigh;
	return 160;
}

static VuoImage imageOrColor(VuoImage image, VuoColor color, VuoInteger width, VuoInteger height)
{
	if (image)
		return image;
	return VuoImage_makeColorImage(color, (unsigned int)width, (unsigned int)height);
}

void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoImage) imageA,
		VuoInputData(VuoColor, {"default":{"r":1.0,"g":1.0,"b":1.0,"a":1.0}}) colorA,
		VuoInputData(VuoImage) imageB,
		VuoInputData(VuoColor, {"default":{"r":1.0,"g":1.0,"b":1.0,"a":1.0}}) colorB,
		VuoInputData(VuoImage) mask,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) resolution,
		VuoOutputData(VuoImage, {"name":"Output"}) output
)
{
	VuoInteger renderWidth = renderDimension(resolution, imageA ? imageA : imageB, true);
	VuoInteger renderHeight = renderDimension(resolution, imageA ? imageA : imageB, false);
	VuoImage safeA = imageOrColor(imageA, colorA, renderWidth, renderHeight);
	VuoImage safeB = imageOrColor(imageB, colorB, renderWidth, renderHeight);
	VuoImage safeMask = imageOrColor(mask, (VuoColor){0.0, 0.0, 0.0, 1.0}, renderWidth, renderHeight);

	VuoShader_setUniform_VuoImage((*instance)->shader, "imageA", safeA);
	VuoShader_setUniform_VuoImage((*instance)->shader, "imageB", safeB);
	VuoShader_setUniform_VuoImage((*instance)->shader, "mask", safeMask);
	VuoShader_setUniform_VuoColor((*instance)->shader, "colorA", colorA);
	VuoShader_setUniform_VuoColor((*instance)->shader, "colorB", colorB);

	*output = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
