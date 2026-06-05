/**
 * @file
 * my.image.generate.noise.fractalNoise node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_FractalNoise
 * - Category: Operators/Lib/image/generate/noise
 * - Source: external/tixl/Operators/Lib/image/generate/noise/FractalNoise.cs
 * - Default: ColorA=(0,0,0,1), ColorB=(1,1,1,1), GainAndBias=(0.5,0.5), Scale=1, Stretch=(2,2), Offset=(0,0), RandomPhase=5, Iterations=2, WarpXY=(0,0), WarpZ=0, Resolution=(256,256), GenerateMips=false, OutputFormat=R16G16B16A16_Float from FractalNoise.t3
 * - Primary output: Texture2D TextureOutput (ColorForTextures #9F008A)
 *
 * Bounded Vuo shader adapter for external/tixl/Operators/Lib/Assets/shaders/img/generate/FractalNoise.hlsl.
 * Vuo body-layer limit: TiXL OpenSimplex2S variants and DXGI output format are approximated with fbm value noise.
 */

#include "VuoImageRenderer.h"
#include "VuoPoint2d.h"
#include <math.h>

VuoModuleMetadata({
					 "title" : "my_FractalNoise",
					 "description" : "TiXL FractalNoise bounded Vuo shader adapter. Source: external/tixl/Operators/Lib/image/generate/noise/FractalNoise.cs. Category: Operators/Lib/image/generate/noise. Primary output: Texture2D TextureOutput (ColorForTextures #9F008A). Default: ColorA=(0,0,0,1), ColorB=(1,1,1,1), GainAndBias=(0.5,0.5), Scale=1, Stretch=(2,2), Offset=(0,0), RandomPhase=5, Iterations=2, WarpXY=(0,0), WarpZ=0, Resolution=(256,256), GenerateMips=false, OutputFormat=R16G16B16A16_Float.",
					 "keywords" : [ "tixl", "texture2d", "image", "generate", "noise", "fractal", "FractalNoise.hlsl", "bounded approximation", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform vec4 colorA;
	uniform vec4 colorB;
	uniform vec2 gainAndBias;
	uniform float scale;
	uniform vec2 stretch;
	uniform vec2 offset;
	uniform float randomPhase;
	uniform int iterations;
	uniform vec2 warpXY;
	uniform float warpZ;
	uniform vec2 targetSize;
	varying vec2 fragmentTextureCoordinate;

	float hash(vec3 p)
	{
		p = fract(p * vec3(0.1031, 0.11369, 0.13787));
		p += dot(p, p.yzx + 33.33);
		return fract((p.x + p.y) * p.z);
	}

	float valueNoise(vec3 p)
	{
		vec3 i = floor(p);
		vec3 f = fract(p);
		f = f * f * (3.0 - 2.0 * f);
		float n000 = hash(i + vec3(0.0, 0.0, 0.0));
		float n100 = hash(i + vec3(1.0, 0.0, 0.0));
		float n010 = hash(i + vec3(0.0, 1.0, 0.0));
		float n110 = hash(i + vec3(1.0, 1.0, 0.0));
		float n001 = hash(i + vec3(0.0, 0.0, 1.0));
		float n101 = hash(i + vec3(1.0, 0.0, 1.0));
		float n011 = hash(i + vec3(0.0, 1.0, 1.0));
		float n111 = hash(i + vec3(1.0, 1.0, 1.0));
		float nx00 = mix(n000, n100, f.x);
		float nx10 = mix(n010, n110, f.x);
		float nx01 = mix(n001, n101, f.x);
		float nx11 = mix(n011, n111, f.x);
		return mix(mix(nx00, nx10, f.y), mix(nx01, nx11, f.y), f.z);
	}

	float applyGainAndBias(float v, vec2 gb)
	{
		float bias = clamp(gb.y, 0.0001, 0.9999);
		float gain = clamp(gb.x, 0.0001, 0.9999);
		v = pow(clamp(v, 0.00001, 0.99999), log(bias) / log(0.5));
		return v < 0.5 ? 0.5 * pow(2.0 * v, log(gain) / log(0.5)) : 1.0 - 0.5 * pow(2.0 - 2.0 * v, log(gain) / log(0.5));
	}

	void main()
	{
		float aspectRatio = targetSize.x / max(targetSize.y, 1.0);
		vec2 uv = fragmentTextureCoordinate - vec2(0.5);
		uv.x *= aspectRatio;
		uv = uv * stretch * max(scale, 0.0001) + offset * vec2(-1.0 / max(aspectRatio, 0.0001), 1.0);
		vec3 p = vec3(uv, randomPhase * 0.1 + warpZ);
		int steps = clamp(iterations, 1, 5);
		float sum = 0.0;
		float amp = 0.55;
		float norm = 0.0;
		for (int i = 0; i < 5; ++i)
		{
			if (i < steps)
			{
				float n = abs(valueNoise(p) * 2.0 - 1.0);
				sum += n * amp;
				norm += amp;
				p = p * 2.03 + vec3(warpXY, warpZ) * n + vec3(12.4, 3.0, 0.7) * float(i + 1);
				amp *= 0.5;
			}
		}
		float v = applyGainAndBias(sum / max(norm, 0.0001), gainAndBias);
		gl_FragColor = mix(colorA, colorB, clamp(v, 0.0, 1.0));
	}
);

struct nodeInstanceData { VuoShader shader; };

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);
	instance->shader = VuoShader_make("my_FractalNoise Shader");
	VuoShader_addSource(instance->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
	VuoRetain(instance->shader);
	return instance;
}

static VuoInteger renderDimension(VuoPoint2d resolution, bool width)
{
	VuoInteger requested = (VuoInteger)llround(width ? resolution.x : resolution.y);
	return requested > 0 ? requested : 256;
}

void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoColor, {"default":{"r":0.0,"g":0.000001,"b":0.000001,"a":1.0}}) colorA,
		VuoInputData(VuoColor, {"default":{"r":1.0,"g":0.99999,"b":1.0,"a":1.0}}) colorB,
		VuoInputData(VuoPoint2d, {"default":{"x":0.5,"y":0.5}}) gainAndBias,
		VuoInputData(VuoReal, {"default":1.0}) scale,
		VuoInputData(VuoPoint2d, {"default":{"x":2.0,"y":2.0}}) stretch,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) offset,
		VuoInputData(VuoReal, {"default":5.0}) randomPhase,
		VuoInputData(VuoInteger, {"default":2,"suggestedMin":1,"suggestedMax":5,"suggestedStep":1}) iterations,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) warpXY,
		VuoInputData(VuoReal, {"default":0.0}) warpZ,
		VuoInputData(VuoPoint2d, {"default":{"x":256.0,"y":256.0}}) resolution,
		VuoInputData(VuoBoolean, {"default":false}) generateMips,
		VuoOutputData(VuoImage, {"name":"TextureOutput"}) textureOutput
)
{
	VuoInteger renderWidth = renderDimension(resolution, true);
	VuoInteger renderHeight = renderDimension(resolution, false);
	VuoShader_setUniform_VuoColor((*instance)->shader, "colorA", colorA);
	VuoShader_setUniform_VuoColor((*instance)->shader, "colorB", colorB);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "gainAndBias", gainAndBias);
	VuoShader_setUniform_VuoReal((*instance)->shader, "scale", scale);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "stretch", stretch);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "offset", offset);
	VuoShader_setUniform_VuoReal((*instance)->shader, "randomPhase", randomPhase);
	VuoShader_setUniform_VuoInteger((*instance)->shader, "iterations", iterations);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "warpXY", warpXY);
	VuoShader_setUniform_VuoReal((*instance)->shader, "warpZ", warpZ);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "targetSize", (VuoPoint2d){renderWidth, renderHeight});
	*textureOutput = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
