/**
 * @file
 * my.image.use.combine3Images node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_Combine3Images
 * - Category: Operators/Lib/image/use
 * - Source: external/tixl/Operators/Lib/image/use/Combine3Images.cs
 * - Default: SelectChannel_R=0, SelectChannel_G=6, SelectChannel_B=12, SelectAlphaChannel=4 from Combine3Images.t3
 * - Primary output: Texture2D Output (ColorForTextures #9F008A)
 *
 * Bounded Vuo shader adapter for external/tixl/Operators/Lib/Assets/shaders/img/use/img-combine-3.hlsl.
 */

#include "VuoImageRenderer.h"

VuoModuleMetadata({
					 "title" : "my_Combine3Images",
					 "description" : "TiXL Combine3Images Vuo shader adapter. Source: external/tixl/Operators/Lib/image/use/Combine3Images.cs. Category: Operators/Lib/image/use. Primary output: Texture2D Output (ColorForTextures #9F008A). Default: SelectChannel_R=0, SelectChannel_G=6, SelectChannel_B=12, SelectAlphaChannel=4.",
					 "keywords" : [ "tixl", "texture2d", "image", "combine", "channel", "img-combine-3.hlsl", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform sampler2D imageA;
	uniform sampler2D imageB;
	uniform sampler2D imageC;
	uniform vec4 colorA;
	uniform vec4 colorB;
	uniform vec4 colorC;
	uniform int selectChannelR;
	uniform int selectChannelG;
	uniform int selectChannelB;
	uniform int selectAlphaChannel;
	varying vec2 fragmentTextureCoordinate;

	float selectedChannel(vec4 a, vec4 b, vec4 c, int select)
	{
		vec4 source = select < 5 ? a : (select < 10 ? b : c);
		int mode = int(mod(float(select), 5.0));
		if (mode == 0)
			return source.r;
		if (mode == 1)
			return source.g;
		if (mode == 2)
			return source.b;
		if (mode == 3)
			return (source.r + source.g + source.b) / 3.0;
		return clamp(0.239 * source.r + 0.686 * source.g + 0.075 * source.b, 0.0, 1.0);
	}

	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		vec4 a = texture2D(imageA, st) * colorA;
		vec4 b = texture2D(imageB, st) * colorB;
		vec4 c = texture2D(imageC, st) * colorC;
		float alpha = selectAlphaChannel == 0 ? a.a : (selectAlphaChannel == 1 ? b.a : (selectAlphaChannel == 2 ? c.a : (selectAlphaChannel == 3 ? 0.0 : 1.0)));
		gl_FragColor = vec4(
			selectedChannel(a, b, c, selectChannelR),
			selectedChannel(a, b, c, selectChannelG),
			selectedChannel(a, b, c, selectChannelB),
			alpha);
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

	instance->shader = VuoShader_make("my_Combine3Images Shader");
	VuoShader_addSource(instance->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
	VuoRetain(instance->shader);

	return instance;
}

static VuoInteger clampDimension(VuoInteger value)
{
	if (value < 1)
		return 160;
	if (value > 4096)
		return 4096;
	return value;
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
		VuoInputData(VuoColor, {"default":{"r":1.0,"g":0.0,"b":0.0,"a":1.0}}) colorA,
		VuoInputData(VuoImage) imageB,
		VuoInputData(VuoColor, {"default":{"r":0.0,"g":1.0,"b":0.0,"a":1.0}}) colorB,
		VuoInputData(VuoImage) imageC,
		VuoInputData(VuoColor, {"default":{"r":0.0,"g":0.0,"b":1.0,"a":1.0}}) colorC,
		VuoInputData(VuoInteger, {"default":0,"suggestedMin":0,"suggestedMax":14,"suggestedStep":1}) selectChannelR,
		VuoInputData(VuoInteger, {"default":6,"suggestedMin":0,"suggestedMax":14,"suggestedStep":1}) selectChannelG,
		VuoInputData(VuoInteger, {"default":12,"suggestedMin":0,"suggestedMax":14,"suggestedStep":1}) selectChannelB,
		VuoInputData(VuoInteger, {"default":4,"suggestedMin":0,"suggestedMax":4,"suggestedStep":1}) selectAlphaChannel,
		VuoInputData(VuoBoolean, {"default":false}) generateMips,
		VuoOutputData(VuoImage, {"name":"Output"}) output
)
{
	VuoInteger renderWidth = clampDimension(imageA ? imageA->pixelsWide : 160);
	VuoInteger renderHeight = clampDimension(imageA ? imageA->pixelsHigh : 160);
	VuoImage safeA = imageOrColor(imageA, colorA, renderWidth, renderHeight);
	VuoImage safeB = imageOrColor(imageB, colorB, renderWidth, renderHeight);
	VuoImage safeC = imageOrColor(imageC, colorC, renderWidth, renderHeight);

	VuoShader_setUniform_VuoImage((*instance)->shader, "imageA", safeA);
	VuoShader_setUniform_VuoImage((*instance)->shader, "imageB", safeB);
	VuoShader_setUniform_VuoImage((*instance)->shader, "imageC", safeC);
	VuoShader_setUniform_VuoColor((*instance)->shader, "colorA", colorA);
	VuoShader_setUniform_VuoColor((*instance)->shader, "colorB", colorB);
	VuoShader_setUniform_VuoColor((*instance)->shader, "colorC", colorC);
	VuoShader_setUniform_VuoInteger((*instance)->shader, "selectChannelR", selectChannelR);
	VuoShader_setUniform_VuoInteger((*instance)->shader, "selectChannelG", selectChannelG);
	VuoShader_setUniform_VuoInteger((*instance)->shader, "selectChannelB", selectChannelB);
	VuoShader_setUniform_VuoInteger((*instance)->shader, "selectAlphaChannel", selectAlphaChannel);

	*output = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
