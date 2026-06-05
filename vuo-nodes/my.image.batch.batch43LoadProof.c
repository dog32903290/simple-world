#include "VuoImageRenderer.h"
VuoModuleMetadata({"title":"my_Batch43LoadProof","description":"Proof compositor for Batch 43 Lib.image.generate.load.","keywords":["tixl","batch43","load","proof"],"version":"1.0.0","dependencies":["VuoImageRenderer"]});
static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform sampler2D imageSequenceClipImage;
	uniform sampler2D loadImageImage;
	uniform sampler2D loadImageFromUrlImage;
	uniform sampler2D loadSvgAsTexture2DImage;
	varying vec2 fragmentTextureCoordinate;
	void main() { vec2 st=fragmentTextureCoordinate; float band=floor(clamp(st.x,0.0,0.9999)*4.0); vec2 localSt=vec2(fract(st.x*4.0),st.y); vec4 color=(band < 1.0 ? texture2D(imageSequenceClipImage, localSt) : (band < 2.0 ? texture2D(loadImageImage, localSt) : (band < 3.0 ? texture2D(loadImageFromUrlImage, localSt) : texture2D(loadSvgAsTexture2DImage, localSt)))); float edge=step(localSt.x,0.012)+step(0.988,localSt.x); if(edge>0.0) color.rgb=mix(color.rgb,vec3(1.0),0.35); gl_FragColor=color; }
);
struct nodeInstanceData { VuoShader shader; };
struct nodeInstanceData * nodeInstanceInit(void) { struct nodeInstanceData *i=(struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData)); VuoRegister(i,free); i->shader=VuoShader_make("my_Batch43LoadProof Shader"); VuoShader_addSource(i->shader,VuoMesh_IndividualTriangles,NULL,NULL,fragmentShaderSource); VuoRetain(i->shader); return i; }
void nodeInstanceEvent(VuoInstanceData(struct nodeInstanceData *) instance,VuoInputEvent() renderTick,
		VuoInputData(VuoImage) imageSequenceClipImage,
		VuoInputData(VuoImage) loadImageImage,
		VuoInputData(VuoImage) loadImageFromUrlImage,
		VuoInputData(VuoImage) loadSvgAsTexture2DImage,
		VuoInputData(VuoInteger,{"default":640}) width,VuoInputData(VuoInteger,{"default":160}) height,VuoOutputData(VuoImage,{"name":"Image"}) image)
{ VuoInteger w=width<1?640:width,h=height<1?160:height; VuoShader_setUniform_VuoImage((*instance)->shader,"imageSequenceClipImage",imageSequenceClipImage?imageSequenceClipImage:VuoImage_makeColorImage((VuoColor){0.02,0.02,0.02,1},w,h));
	VuoShader_setUniform_VuoImage((*instance)->shader,"loadImageImage",loadImageImage?loadImageImage:VuoImage_makeColorImage((VuoColor){0.02,0.02,0.02,1},w,h));
	VuoShader_setUniform_VuoImage((*instance)->shader,"loadImageFromUrlImage",loadImageFromUrlImage?loadImageFromUrlImage:VuoImage_makeColorImage((VuoColor){0.02,0.02,0.02,1},w,h));
	VuoShader_setUniform_VuoImage((*instance)->shader,"loadSvgAsTexture2DImage",loadSvgAsTexture2DImage?loadSvgAsTexture2DImage:VuoImage_makeColorImage((VuoColor){0.02,0.02,0.02,1},w,h)); *image=VuoImageRenderer_render((*instance)->shader,w,h,VuoImageColorDepth_8); }
void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance) { VuoRelease((*instance)->shader); }
