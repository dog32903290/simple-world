/**
 * @file
 * my.image.use.blend node implementation.
 *
 * Vuo body-layer adapter for TiXL Operators/Lib/image/use/Blend.
 * This keeps the first high-risk Texture2D lane connectable in Vuo without
 * claiming TiXL's full AlphaMode / GenerateMips / DX11 RenderTarget behavior.
 */

#include "VuoImageRenderer.h"

VuoModuleMetadata({
					 "title" : "my_Blend",
					 "description" : "TiXL Blend Texture2D body adapter. Source: external/tixl/Operators/Lib/image/use/Blend.cs. Category: Operators/Lib/image/use. Primary output: Texture2D / VuoImage. ColorForTextures #9F008A. This is not a full TiXL Blend clone.",
					 "keywords" : [ "tixl", "texture2d", "image", "blend", "composite" ],
					 "version" : "1.0.0",
					 "dependencies" : [
						 "VuoImageRenderer"
					 ],
				 });

static const char * fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform sampler2D background;
	uniform sampler2D foreground;
	uniform float foregroundOpacity;
	uniform bool replaceOpacity;
	varying vec2 fragmentTextureCoordinate;

	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		vec4 bg = texture2D(background, st);
		vec4 fg = texture2D(foreground, st);
		fg *= clamp(foregroundOpacity, 0.0, 1.0);
		vec4 blended = fg + bg * (1.0 - fg.a);
		if (replaceOpacity)
			blended.a = max(bg.a, fg.a);
		gl_FragColor = blended;
	}
);

struct nodeInstanceData
{
	VuoShader shader;
};

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData * instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);

	instance->shader = VuoShader_make("my_Blend Shader");
	VuoShader_addSource(instance->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
	VuoRetain(instance->shader);

	return instance;
}

void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoImage, {"name":"Background"}) Background,
		VuoInputData(VuoImage, {"name":"Foreground"}) Foreground,
		VuoInputData(VuoReal, {"default":0.75,"suggestedMin":0.0,"suggestedMax":1.0,"suggestedStep":0.01}) ForegroundOpacity,
		VuoInputData(VuoBoolean, {"default":true}) ReplaceOpacity,
		VuoOutputData(VuoImage, {"name":"Blended"}) Blended
)
{
	if (!Background && !Foreground)
	{
		*Blended = NULL;
		return;
	}

	if (!Background)
	{
		*Blended = Foreground;
		return;
	}

	if (!Foreground)
	{
		*Blended = Background;
		return;
	}

	VuoReal safeOpacity = ForegroundOpacity < 0.0 ? 0.0 : (ForegroundOpacity > 1.0 ? 1.0 : ForegroundOpacity);
	VuoInteger renderWidth = Background->pixelsWide > 0 ? Background->pixelsWide : Foreground->pixelsWide;
	VuoInteger renderHeight = Background->pixelsHigh > 0 ? Background->pixelsHigh : Foreground->pixelsHigh;

	VuoShader_setUniform_VuoImage   ((*instance)->shader, "background", Background);
	VuoShader_setUniform_VuoImage   ((*instance)->shader, "foreground", Foreground);
	VuoShader_setUniform_VuoReal    ((*instance)->shader, "foregroundOpacity", safeOpacity);
	VuoShader_setUniform_VuoBoolean ((*instance)->shader, "replaceOpacity", ReplaceOpacity);

	*Blended = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImage_getColorDepth(Background));
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
