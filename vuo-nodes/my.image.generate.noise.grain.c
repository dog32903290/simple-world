/**
 * @file
 * my.image.generate.noise.grain node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_Grain
 * - Category: Operators/Lib/image/generate/noise
 * - Source: external/tixl/Operators/Lib/image/generate/noise/Grain.cs
 * - Default: Image=null, Amount=0.05, Color=0, Exponent=1, Brightness=0, Animate=5, RandomPhase=0, Scale=0, Resolution=(0,0), GenerateMipmaps=false from Grain.t3
 * - Primary output: Texture2D TextureOutput (ColorForTextures #9F008A)
 *
 * Bounded Vuo shader adapter for TiXL Grain shader setup.
 * Vuo body-layer limit: Image blending is represented as standalone procedural grain when Image is null.
 */

#include "VuoImageRenderer.h"
#include "VuoPoint2d.h"
#include <math.h>

VuoModuleMetadata({
					 "title" : "my_Grain",
					 "description" : "TiXL Grain bounded Vuo shader adapter. Source: external/tixl/Operators/Lib/image/generate/noise/Grain.cs. Category: Operators/Lib/image/generate/noise. Primary output: Texture2D TextureOutput (ColorForTextures #9F008A). Default: Image=null, Amount=0.05, Color=0, Exponent=1, Brightness=0, Animate=5, RandomPhase=0, Scale=0, Resolution=(0,0), GenerateMipmaps=false.",
					 "keywords" : [ "tixl", "texture2d", "image", "generate", "noise", "grain", "Grain.hlsl", "bounded approximation", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform float amount;
	uniform float colorAmount;
	uniform float exponent;
	uniform float brightness;
	uniform float animate;
	uniform float randomPhase;
	uniform float scale;
	uniform vec2 targetSize;
	varying vec2 fragmentTextureCoordinate;

	float hash(vec3 p)
	{
		return fract(sin(dot(p, vec3(12.9898, 78.233, 37.719))) * 43758.5453);
	}

	void main()
	{
		vec2 uv = fragmentTextureCoordinate * targetSize / max(scale + 1.0, 1.0);
		float phase = randomPhase + animate * 0.1;
		float mono = pow(hash(vec3(floor(uv), phase)), max(exponent, 0.0001));
		vec3 chroma = vec3(
			hash(vec3(floor(uv), phase + 3.1)),
			hash(vec3(floor(uv), phase + 7.7)),
			hash(vec3(floor(uv), phase + 11.3)));
		vec3 n = mix(vec3(mono), chroma, clamp(colorAmount, 0.0, 1.0));
		vec3 base = vec3(0.5 + brightness);
		vec3 outColor = base + (n - 0.5) * amount * 8.0;
		gl_FragColor = vec4(clamp(outColor, 0.0, 1.0), 1.0);
	}
);

struct nodeInstanceData { VuoShader shader; };

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);
	instance->shader = VuoShader_make("my_Grain Shader");
	VuoShader_addSource(instance->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
	VuoRetain(instance->shader);
	return instance;
}

static VuoInteger renderDimension(VuoPoint2d resolution, bool width)
{
	VuoInteger requested = (VuoInteger)llround(width ? resolution.x : resolution.y);
	return requested > 0 ? requested : 160;
}

void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoImage) image,
		VuoInputData(VuoReal, {"default":0.05}) amount,
		VuoInputData(VuoReal, {"default":0.0}) color,
		VuoInputData(VuoReal, {"default":1.0}) exponent,
		VuoInputData(VuoReal, {"default":0.0}) brightness,
		VuoInputData(VuoReal, {"default":5.0}) animate,
		VuoInputData(VuoReal, {"default":0.0}) randomPhase,
		VuoInputData(VuoReal, {"default":0.0}) scale,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) resolution,
		VuoInputData(VuoBoolean, {"default":false}) generateMipmaps,
		VuoOutputData(VuoImage, {"name":"TextureOutput"}) textureOutput
)
{
	VuoInteger renderWidth = renderDimension(resolution, true);
	VuoInteger renderHeight = renderDimension(resolution, false);
	VuoShader_setUniform_VuoReal((*instance)->shader, "amount", amount);
	VuoShader_setUniform_VuoReal((*instance)->shader, "colorAmount", color);
	VuoShader_setUniform_VuoReal((*instance)->shader, "exponent", exponent);
	VuoShader_setUniform_VuoReal((*instance)->shader, "brightness", brightness);
	VuoShader_setUniform_VuoReal((*instance)->shader, "animate", animate);
	VuoShader_setUniform_VuoReal((*instance)->shader, "randomPhase", randomPhase);
	VuoShader_setUniform_VuoReal((*instance)->shader, "scale", scale);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "targetSize", (VuoPoint2d){renderWidth, renderHeight});
	*textureOutput = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
