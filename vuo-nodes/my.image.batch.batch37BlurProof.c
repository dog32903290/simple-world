#include "VuoImageRenderer.h"
VuoModuleMetadata({"title":"my_Batch37BlurProof","description":"Proof-only compositor for Batch 37 blur nodes.","keywords":["tixl","batch37","blur","proof"],"version":"1.0.0","dependencies":["VuoImageRenderer"]});
static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform sampler2D bloomImage;
	uniform sampler2D blurImage;
	uniform sampler2D directionalBlurImage;
	uniform sampler2D fastBlurImage;
	uniform sampler2D sharpenImage;
	varying vec2 fragmentTextureCoordinate;
	void main() {
		vec2 st = fragmentTextureCoordinate;
		float band = floor(clamp(st.x, 0.0, 0.9999) * 5.0);
		vec2 localSt = vec2(fract(st.x * 5.0), st.y);
		vec4 color = (band < 1.0 ? texture2D(bloomImage, localSt) : (band < 2.0 ? texture2D(blurImage, localSt) : (band < 3.0 ? texture2D(directionalBlurImage, localSt) : (band < 4.0 ? texture2D(fastBlurImage, localSt) : texture2D(sharpenImage, localSt)))));
		float edge = step(localSt.x, 0.02) + step(0.98, localSt.x);
		if (edge > 0.0) color.rgb = mix(color.rgb, vec3(1.0), 0.4);
		gl_FragColor = color;
	}
);
struct nodeInstanceData { VuoShader shader; };
struct nodeInstanceData * nodeInstanceInit(void) { struct nodeInstanceData *instance=(struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData)); VuoRegister(instance, free); instance->shader=VuoShader_make("my_Batch37BlurProof Shader"); VuoShader_addSource(instance->shader,VuoMesh_IndividualTriangles,NULL,NULL,fragmentShaderSource); VuoRetain(instance->shader); return instance; }
void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoImage) bloomImage,
		VuoInputData(VuoImage) blurImage,
		VuoInputData(VuoImage) directionalBlurImage,
		VuoInputData(VuoImage) fastBlurImage,
		VuoInputData(VuoImage) sharpenImage,
		VuoInputData(VuoInteger, {"default":800}) width,
		VuoInputData(VuoInteger, {"default":160}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{ VuoInteger renderWidth=width<1?800:width; VuoInteger renderHeight=height<1?160:height; VuoShader_setUniform_VuoImage((*instance)->shader, "bloomImage", bloomImage ? bloomImage : VuoImage_makeColorImage((VuoColor){0.1,0.1,0.1,1}, renderWidth, renderHeight));
	VuoShader_setUniform_VuoImage((*instance)->shader, "blurImage", blurImage ? blurImage : VuoImage_makeColorImage((VuoColor){0.1,0.1,0.1,1}, renderWidth, renderHeight));
	VuoShader_setUniform_VuoImage((*instance)->shader, "directionalBlurImage", directionalBlurImage ? directionalBlurImage : VuoImage_makeColorImage((VuoColor){0.1,0.1,0.1,1}, renderWidth, renderHeight));
	VuoShader_setUniform_VuoImage((*instance)->shader, "fastBlurImage", fastBlurImage ? fastBlurImage : VuoImage_makeColorImage((VuoColor){0.1,0.1,0.1,1}, renderWidth, renderHeight));
	VuoShader_setUniform_VuoImage((*instance)->shader, "sharpenImage", sharpenImage ? sharpenImage : VuoImage_makeColorImage((VuoColor){0.1,0.1,0.1,1}, renderWidth, renderHeight)); *image=VuoImageRenderer_render((*instance)->shader,renderWidth,renderHeight,VuoImageColorDepth_8); }
void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance) { VuoRelease((*instance)->shader); }
