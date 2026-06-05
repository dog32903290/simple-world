/**
 * @file
 * my.image.transform.makeTileableImage node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_MakeTileableImage
 * - Category: Operators/Lib/image/transform
 * - Source: external/tixl/Operators/Lib/image/transform/MakeTileableImage.cs
 * - Default: ImageA=null, EdgeFallOff=0.2, TilingMode=3, IsEnabled=true from MakeTileableImage.t3
 * - Primary output: Texture2D Selected (ColorForTextures #9F008A)
 *
 * Bounded Vuo shader adapter for the TiXL compound graph that blends offset
 * TransformImage copies through linear masks.
 */

#include "VuoImageRenderer.h"

VuoModuleMetadata({
					 "title" : "my_MakeTileableImage",
					 "description" : "TiXL MakeTileableImage bounded Vuo shader adapter. Source: external/tixl/Operators/Lib/image/transform/MakeTileableImage.cs. Category: Operators/Lib/image/transform. Primary output: Texture2D Selected (ColorForTextures #9F008A). Default: ImageA=null, EdgeFallOff=0.2, TilingMode=3, IsEnabled=true.",
					 "keywords" : [ "tixl", "texture2d", "image", "tileable", "TransformImage", "BlendWithMask", "bounded approximation", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform sampler2D imageA;
	uniform float edgeFallOff;
	uniform int tilingMode;
	uniform bool isEnabled;
	varying vec2 fragmentTextureCoordinate;

	float edgeMask(float v, float falloff)
	{
		float f = max(falloff, 0.0001);
		float lo = 1.0 - smoothstep(0.0, f, v);
		float hi = smoothstep(1.0 - f, 1.0, v);
		return max(lo, hi);
	}

	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		vec4 color = texture2D(imageA, st);
		if (!isEnabled || tilingMode == 0)
		{
			gl_FragColor = color;
			return;
		}
		float mx = (tilingMode == 1 || tilingMode == 3) ? edgeMask(st.x, edgeFallOff) : 0.0;
		float my = (tilingMode == 2 || tilingMode == 3) ? edgeMask(st.y, edgeFallOff) : 0.0;
		vec4 xShift = texture2D(imageA, fract(st + vec2(0.5, 0.0)));
		vec4 yShift = texture2D(imageA, fract(st + vec2(0.0, 0.5)));
		vec4 xyShift = texture2D(imageA, fract(st + vec2(0.5, 0.5)));
		vec4 blended = mix(color, xShift, mx);
		blended = mix(blended, yShift, my);
		blended = mix(blended, xyShift, mx * my);
		gl_FragColor = blended;
	}
);

struct nodeInstanceData { VuoShader shader; };

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);
	instance->shader = VuoShader_make("my_MakeTileableImage Shader");
	VuoShader_addSource(instance->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
	VuoRetain(instance->shader);
	return instance;
}

void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoImage) imageA,
		VuoInputData(VuoReal, {"default":0.2,"suggestedMin":0.0,"suggestedMax":0.5,"suggestedStep":0.01}) edgeFallOff,
		VuoInputData(VuoInteger, {"default":3,"suggestedMin":0,"suggestedMax":3,"suggestedStep":1}) tilingMode,
		VuoInputData(VuoBoolean, {"default":true}) isEnabled,
		VuoOutputData(VuoImage, {"name":"Selected"}) selected
)
{
	if (!imageA)
	{
		*selected = NULL;
		return;
	}
	VuoShader_setUniform_VuoImage((*instance)->shader, "imageA", imageA);
	VuoShader_setUniform_VuoReal((*instance)->shader, "edgeFallOff", edgeFallOff);
	VuoShader_setUniform_VuoInteger((*instance)->shader, "tilingMode", tilingMode);
	VuoShader_setUniform_VuoBoolean((*instance)->shader, "isEnabled", isEnabled);
	*selected = VuoImageRenderer_render((*instance)->shader, imageA->pixelsWide, imageA->pixelsHigh, VuoImage_getColorDepth(imageA));
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
