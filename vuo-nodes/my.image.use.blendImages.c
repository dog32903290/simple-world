/**
 * @file
 * my.image.use.blendImages node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_BlendImages
 * - Category: Operators/Lib/image/use
 * - Source: external/tixl/Operators/Lib/image/use/BlendImages.cs
 * - Default: BlendFraction=0, Input=null, Resolution=(0,0) from BlendImages.t3
 * - Primary output: Texture2D OutputImage (ColorForTextures #9F008A)
 *
 * Vuo bounded adapter: TiXL MultiInputSlot<Texture2D> is represented by three
 * image inputs plus inputCount. The TiXL compound clamps BlendFraction to
 * [0, 10000], picks floor(f) and floor(f)+1 by modulo, then crossfades.
 */

#include "VuoImageRenderer.h"
#include "VuoPoint2d.h"
#include <math.h>

VuoModuleMetadata({
					 "title" : "my_BlendImages",
					 "description" : "TiXL BlendImages bounded Vuo adapter. Source: external/tixl/Operators/Lib/image/use/BlendImages.cs. Category: Operators/Lib/image/use. Primary output: Texture2D OutputImage (ColorForTextures #9F008A). Default: BlendFraction=0, Input=null, Resolution=(0,0).",
					 "keywords" : [ "tixl", "texture2d", "image", "blend", "crossfade", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform sampler2D imageA;
	uniform sampler2D imageB;
	uniform float blendAmount;
	varying vec2 fragmentTextureCoordinate;

	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		vec4 a = texture2D(imageA, st);
		vec4 b = texture2D(imageB, st);
		gl_FragColor = mix(a, b, clamp(blendAmount, 0.0, 1.0));
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

	instance->shader = VuoShader_make("my_BlendImages Shader");
	VuoShader_addSource(instance->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
	VuoRetain(instance->shader);

	return instance;
}

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

static VuoReal clampBlendFraction(VuoReal value)
{
	if (value < 0.0)
		return 0.0;
	if (value > 10000.0)
		return 10000.0;
	return value;
}

static VuoImage imageAt(VuoImage input1, VuoImage input2, VuoImage input3, VuoInteger index)
{
	if (index == 0)
		return input1;
	if (index == 1)
		return input2;
	return input3;
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

void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoImage) input1,
		VuoInputData(VuoImage) input2,
		VuoInputData(VuoImage) input3,
		VuoInputData(VuoInteger, {"default":0,"suggestedMin":0,"suggestedMax":3,"suggestedStep":1}) inputCount,
		VuoInputData(VuoReal, {"default":0.0,"suggestedMin":0.0,"suggestedMax":10.0,"suggestedStep":0.01}) blendFraction,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) resolution,
		VuoOutputData(VuoImage, {"name":"OutputImage"}) outputImage
)
{
	VuoInteger count = clampInputCount(inputCount);
	if (count == 0)
	{
		*outputImage = NULL;
		return;
	}

	VuoReal f = clampBlendFraction(blendFraction);
	VuoInteger lower = (VuoInteger)floor(f);
	VuoReal amount = f - (VuoReal)lower;
	VuoImage imageA = imageAt(input1, input2, input3, positiveModulo(lower, count));
	VuoImage imageB = imageAt(input1, input2, input3, positiveModulo(lower + 1, count));

	if (!imageA && !imageB)
	{
		*outputImage = NULL;
		return;
	}
	if (!imageA)
		imageA = imageB;
	if (!imageB)
		imageB = imageA;

	VuoInteger renderWidth = renderDimension(resolution, imageA, true);
	VuoInteger renderHeight = renderDimension(resolution, imageA, false);

	VuoShader_setUniform_VuoImage((*instance)->shader, "imageA", imageA);
	VuoShader_setUniform_VuoImage((*instance)->shader, "imageB", imageB);
	VuoShader_setUniform_VuoReal((*instance)->shader, "blendAmount", amount);

	*outputImage = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImage_getColorDepth(imageA));
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
