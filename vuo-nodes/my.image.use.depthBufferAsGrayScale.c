/**
 * @file
 * my.image.use.depthBufferAsGrayScale node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_DepthBufferAsGrayScale
 * - Category: Operators/Lib/image/use
 * - Source: external/tixl/Operators/Lib/image/use/DepthBufferAsGrayScale.cs
 * - Default: NearFarRange=(0.01,1000), OutputRange=(0,5), ClampOutput=false, Mode=0 from DepthBufferAsGrayScale.t3
 * - Primary output: Texture2D Output (ColorForTextures #9F008A)
 *
 * Bounded Vuo shader adapter for external/tixl/Operators/Lib/Assets/shaders/img/post-fx/depth-to-linear.hlsl.
 * Vuo body-layer limit: TiXL uses a compute shader; this uses a fragment shader
 * with the same visible depth-to-gray formula and negative depth checker sentinel.
 */

#include "VuoImageRenderer.h"
#include "VuoPoint2d.h"

VuoModuleMetadata({
					 "title" : "my_DepthBufferAsGrayScale",
					 "description" : "TiXL DepthBufferAsGrayScale bounded Vuo shader adapter. Source: external/tixl/Operators/Lib/image/use/DepthBufferAsGrayScale.cs. Category: Operators/Lib/image/use. Primary output: Texture2D Output (ColorForTextures #9F008A). Default: NearFarRange=(0.01,1000), OutputRange=(0,5), ClampOutput=false, Mode=0.",
					 "keywords" : [ "tixl", "texture2d", "image", "depth", "gray", "depth-to-linear.hlsl", "negative depth checker", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform sampler2D texture2d;
	uniform vec2 nearFarRange;
	uniform vec2 outputRange;
	uniform bool clampOutput;
	uniform int mode;
	varying vec2 fragmentTextureCoordinate;

	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		float depth = texture2D(texture2d, st).r;
		if (depth < 0.0)
		{
			float checker = mod(floor(st.x * 160.0) + floor(st.y * 160.0), 16.0) > 0.0 ? 0.0 : 1.0;
			gl_FragColor = vec4(vec3(checker), 1.0);
			return;
		}
		float n = nearFarRange.x;
		float f = nearFarRange.y;
		float c = mode < 1 ? (-f * n) / (depth * (f - n) - f) : (2.0 * n) / (f + n - depth * (f - n));
		if (outputRange.x != 0.0 || outputRange.y != 0.0)
			c = (c - outputRange.x) / (outputRange.y - outputRange.x);
		if (clampOutput)
			c = clamp(c, 0.0, 1.0);
		gl_FragColor = vec4(vec3(c), 1.0);
	}
);

struct nodeInstanceData { VuoShader shader; };

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);
	instance->shader = VuoShader_make("my_DepthBufferAsGrayScale Shader");
	VuoShader_addSource(instance->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
	VuoRetain(instance->shader);
	return instance;
}

void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoImage) texture2d,
		VuoInputData(VuoPoint2d, {"default":{"x":0.01,"y":1000.0}}) nearFarRange,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":5.0}}) outputRange,
		VuoInputData(VuoBoolean, {"default":false}) clampOutput,
		VuoInputData(VuoInteger, {"default":0,"suggestedMin":0,"suggestedMax":1,"suggestedStep":1}) mode,
		VuoOutputData(VuoImage, {"name":"Output"}) output
)
{
	if (!texture2d)
	{
		*output = NULL;
		return;
	}
	VuoInteger renderWidth = texture2d->pixelsWide > 0 ? texture2d->pixelsWide : 160;
	VuoInteger renderHeight = texture2d->pixelsHigh > 0 ? texture2d->pixelsHigh : 160;
	VuoShader_setUniform_VuoImage((*instance)->shader, "texture2d", texture2d);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "nearFarRange", nearFarRange);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "outputRange", outputRange);
	VuoShader_setUniform_VuoBoolean((*instance)->shader, "clampOutput", clampOutput);
	VuoShader_setUniform_VuoInteger((*instance)->shader, "mode", mode);
	*output = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
