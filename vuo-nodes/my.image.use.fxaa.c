/**
 * @file
 * my.image.use.fxaa node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_Fxaa
 * - Category: Operators/Lib/image/use
 * - Source: external/tixl/Operators/Lib/image/use/Fxaa.cs
 * - Default: Image=null, Preset=0, KeepAlpha=false from Fxaa.t3
 * - Primary output: Texture2D TextureOutput (ColorForTextures #9F008A)
 *
 * Bounded Vuo shader adapter for external/tixl/Operators/Lib/Assets/shaders/img/use/FXAA.hlsl.
 * Vuo body-layer limit: this is a bounded approximation using local contrast
 * smoothing; it preserves KeepAlpha but not TiXL's compile-time FXAA preset path.
 */

#include "VuoImageRenderer.h"

VuoModuleMetadata({
					 "title" : "my_Fxaa",
					 "description" : "TiXL Fxaa bounded Vuo shader adapter. Source: external/tixl/Operators/Lib/image/use/Fxaa.cs. Category: Operators/Lib/image/use. Primary output: Texture2D TextureOutput (ColorForTextures #9F008A). Default: Image=null, Preset=0, KeepAlpha=false.",
					 "keywords" : [ "tixl", "texture2d", "image", "fxaa", "FXAA.hlsl", "bounded approximation", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform sampler2D image;
	uniform vec2 texelSize;
	uniform int preset;
	uniform bool keepAlpha;
	varying vec2 fragmentTextureCoordinate;

	float luma(vec3 c)
	{
		return dot(c, vec3(0.299, 0.587, 0.114));
	}

	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		vec4 center = texture2D(image, st);
		vec4 n = texture2D(image, st + vec2(0.0, texelSize.y));
		vec4 s = texture2D(image, st - vec2(0.0, texelSize.y));
		vec4 e = texture2D(image, st + vec2(texelSize.x, 0.0));
		vec4 w = texture2D(image, st - vec2(texelSize.x, 0.0));
		float localRange = max(max(luma(n.rgb), luma(s.rgb)), max(luma(e.rgb), luma(w.rgb))) - min(min(luma(n.rgb), luma(s.rgb)), min(luma(e.rgb), luma(w.rgb)));
		float strength = clamp(localRange * (0.65 + 0.07 * float(preset)), 0.0, 0.85);
		vec4 softened = (center * 4.0 + n + s + e + w) / 8.0;
		vec4 outColor = mix(center, softened, strength);
		outColor.a = keepAlpha ? center.a : 1.0;
		gl_FragColor = outColor;
	}
);

struct nodeInstanceData { VuoShader shader; };

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);
	instance->shader = VuoShader_make("my_Fxaa Shader");
	VuoShader_addSource(instance->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
	VuoRetain(instance->shader);
	return instance;
}

void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoImage) image,
		VuoInputData(VuoInteger, {"default":0,"suggestedMin":0,"suggestedMax":5,"suggestedStep":1}) preset,
		VuoInputData(VuoBoolean, {"default":false}) keepAlpha,
		VuoOutputData(VuoImage, {"name":"TextureOutput"}) textureOutput
)
{
	if (!image)
	{
		*textureOutput = NULL;
		return;
	}
	VuoInteger renderWidth = image->pixelsWide > 0 ? image->pixelsWide : 160;
	VuoInteger renderHeight = image->pixelsHigh > 0 ? image->pixelsHigh : 160;
	VuoShader_setUniform_VuoImage((*instance)->shader, "image", image);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "texelSize", (VuoPoint2d){1.0 / (VuoReal)renderWidth, 1.0 / (VuoReal)renderHeight});
	VuoShader_setUniform_VuoInteger((*instance)->shader, "preset", preset);
	VuoShader_setUniform_VuoBoolean((*instance)->shader, "keepAlpha", keepAlpha);
	*textureOutput = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImage_getColorDepth(image));
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
