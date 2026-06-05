/**
 * @file
 * my.image.batch.batch36FractalGenerateProof node implementation.
 */

#include "VuoImageRenderer.h"

VuoModuleMetadata({
					 "title" : "my_Batch36FractalGenerateProof",
					 "description" : "Proof-only compositor for Batch 36 MandelbrotFractal and MunchingSquares2 nodes.",
					 "keywords" : [ "tixl", "batch36", "texture2d", "image", "generate", "fractal", "munching", "proof", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform sampler2D mandelbrotImage;
	uniform sampler2D munchingImage;
	varying vec2 fragmentTextureCoordinate;
	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		vec2 localSt = vec2(fract(st.x * 2.0), st.y);
		vec4 color = st.x < 0.5 ? texture2D(mandelbrotImage, localSt) : texture2D(munchingImage, localSt);
		float edge = step(localSt.x, 0.02) + step(0.98, localSt.x);
		if (edge > 0.0) color.rgb = mix(color.rgb, vec3(1.0), 0.35);
		gl_FragColor = color;
	}
);
struct nodeInstanceData { VuoShader shader; };
struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);
	instance->shader = VuoShader_make("my_Batch36FractalGenerateProof Shader");
	VuoShader_addSource(instance->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
	VuoRetain(instance->shader);
	return instance;
}
static VuoImage imageOrColor(VuoImage image, VuoColor color, VuoInteger width, VuoInteger height)
{
	if (image) return image;
	return VuoImage_makeColorImage(color, (unsigned int)width, (unsigned int)height);
}
void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoImage) mandelbrotImage,
		VuoInputData(VuoImage) munchingImage,
		VuoInputData(VuoInteger, {"default":640}) width,
		VuoInputData(VuoInteger, {"default":320}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{
	VuoInteger renderWidth = width < 1 ? 640 : width;
	VuoInteger renderHeight = height < 1 ? 320 : height;
	VuoShader_setUniform_VuoImage((*instance)->shader, "mandelbrotImage", imageOrColor(mandelbrotImage, (VuoColor){0.1, 0.1, 0.1, 1.0}, renderWidth, renderHeight));
	VuoShader_setUniform_VuoImage((*instance)->shader, "munchingImage", imageOrColor(munchingImage, (VuoColor){0.6, 0.6, 0.6, 1.0}, renderWidth, renderHeight));
	*image = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}
void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance) { VuoRelease((*instance)->shader); }
