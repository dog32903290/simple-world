/**
 * @file
 * my.image.generate.fractal.mandelbrotFractal node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_MandelbrotFractal
 * - Category: Operators/Lib/image/generate/fractal
 * - Source: external/tixl/Operators/Lib/image/generate/fractal/MandelbrotFractal.cs
 * - Default: Phase=0, Scale=-0.5, Offset=(0.251,0), ColorScale=10, Gradient=black-to-white from MandelbrotFractal.t3
 * - Primary output: Texture2D TextureOutput (ColorForTextures #9F008A)
 *
 * Bounded Vuo shader adapter for external/tixl/Operators/Lib/Assets/shaders/img/generate/MandelbrotFractal.hlsl.
 * Vuo body-layer limit: TiXL Gradient datatype is represented as colorA/colorB.
 */

#define NODE_TITLE "my_MandelbrotFractal"
#include "VuoImageRenderer.h"
#include "VuoPoint2d.h"
#include <math.h>

VuoModuleMetadata({
					 "title" : "my_MandelbrotFractal",
					 "description" : "TiXL MandelbrotFractal bounded Vuo shader adapter. Source: external/tixl/Operators/Lib/image/generate/fractal/MandelbrotFractal.cs. Category: Operators/Lib/image/generate/fractal. Primary output: Texture2D TextureOutput (ColorForTextures #9F008A). Default: Phase=0, Scale=-0.5, Offset=(0.251,0), ColorScale=10, Gradient=black-to-white.",
					 "keywords" : [ "tixl", "texture2d", "image", "generate", "fractal", "mandelbrot", "MandelbrotFractal.hlsl", "bounded approximation", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform vec4 colorA;
	uniform vec4 colorB;
	uniform float phase;
	uniform float scale;
	uniform vec2 offset;
	uniform float colorScale;
	uniform vec2 targetSize;
	varying vec2 fragmentTextureCoordinate;
	void main()
	{
		float aspectRatio = targetSize.x / max(targetSize.y, 1.0);
		float zoom = pow(2.0, scale);
		vec2 c = (fragmentTextureCoordinate - 0.5) * vec2(aspectRatio, 1.0) * (3.0 / max(zoom, 0.0001)) + vec2(-0.75, 0.0) + offset;
		vec2 z = vec2(cos(phase) * 0.001, sin(phase) * 0.001);
		float iter = 0.0;
		for (int i = 0; i < 96; ++i)
		{
			if (dot(z, z) <= 4.0)
			{
				z = vec2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + c;
				iter += 1.0;
			}
		}
		float v = iter >= 96.0 ? 0.0 : iter / max(colorScale, 0.0001);
		gl_FragColor = mix(colorA, colorB, clamp(v, 0.0, 1.0));
	}
);

struct nodeInstanceData { VuoShader shader; };
struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);
	instance->shader = VuoShader_make("my_MandelbrotFractal Shader");
	VuoShader_addSource(instance->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
	VuoRetain(instance->shader);
	return instance;
}
static VuoInteger renderDimension(VuoPoint2d resolution, bool width)
{
	VuoInteger requested = (VuoInteger)llround(width ? resolution.x : resolution.y);
	return requested > 0 ? requested : 320;
}
void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoReal, {"default":0.0}) phase,
		VuoInputData(VuoReal, {"default":-0.5}) scale,
		VuoInputData(VuoPoint2d, {"default":{"x":0.251,"y":0.0}}) offset,
		VuoInputData(VuoReal, {"default":10.0}) colorScale,
		VuoInputData(VuoColor, {"default":{"r":0.0,"g":0.0,"b":0.0,"a":1.0}}) colorA,
		VuoInputData(VuoColor, {"default":{"r":1.0,"g":1.0,"b":1.0,"a":1.0}}) colorB,
		VuoInputData(VuoPoint2d, {"default":{"x":320.0,"y":320.0}}) resolution,
		VuoOutputData(VuoImage, {"name":"TextureOutput"}) textureOutput
)
{
	VuoInteger renderWidth = renderDimension(resolution, true);
	VuoInteger renderHeight = renderDimension(resolution, false);
	VuoShader_setUniform_VuoReal((*instance)->shader, "phase", phase);
	VuoShader_setUniform_VuoReal((*instance)->shader, "scale", scale);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "offset", offset);
	VuoShader_setUniform_VuoReal((*instance)->shader, "colorScale", colorScale);
	VuoShader_setUniform_VuoColor((*instance)->shader, "colorA", colorA);
	VuoShader_setUniform_VuoColor((*instance)->shader, "colorB", colorB);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "targetSize", (VuoPoint2d){renderWidth, renderHeight});
	*textureOutput = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}
void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance) { VuoRelease((*instance)->shader); }
