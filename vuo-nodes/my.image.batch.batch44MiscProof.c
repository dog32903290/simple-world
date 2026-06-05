#include "VuoImageRenderer.h"
VuoModuleMetadata({"title":"my_Batch44MiscProof","description":"Proof compositor for Batch 44 Lib.image.generate.misc.","keywords":["tixl","batch44","misc","proof"],"version":"1.0.0","dependencies":["VuoImageRenderer"]});
static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform sampler2D jumpFloodFillImage;
	uniform sampler2D sketchImage;
	uniform sampler2D slidingHistoryImage;
	varying vec2 fragmentTextureCoordinate;
	void main() { vec2 st=fragmentTextureCoordinate; float band=floor(clamp(st.x,0.0,0.9999)*3.0); vec2 localSt=vec2(fract(st.x*3.0),st.y); vec4 color=(band < 1.0 ? texture2D(jumpFloodFillImage, localSt) : (band < 2.0 ? texture2D(sketchImage, localSt) : texture2D(slidingHistoryImage, localSt))); float edge=step(localSt.x,0.012)+step(0.988,localSt.x); if(edge>0.0) color.rgb=mix(color.rgb,vec3(1.0),0.35); gl_FragColor=color; }
);
struct nodeInstanceData { VuoShader shader; };
struct nodeInstanceData * nodeInstanceInit(void) { struct nodeInstanceData *i=(struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData)); VuoRegister(i,free); i->shader=VuoShader_make("my_Batch44MiscProof Shader"); VuoShader_addSource(i->shader,VuoMesh_IndividualTriangles,NULL,NULL,fragmentShaderSource); VuoRetain(i->shader); return i; }
void nodeInstanceEvent(VuoInstanceData(struct nodeInstanceData *) instance,VuoInputEvent() renderTick,
		VuoInputData(VuoImage) jumpFloodFillImage,
		VuoInputData(VuoImage) sketchImage,
		VuoInputData(VuoImage) slidingHistoryImage,
		VuoInputData(VuoInteger,{"default":480}) width,VuoInputData(VuoInteger,{"default":160}) height,VuoOutputData(VuoImage,{"name":"Image"}) image)
{ VuoInteger w=width<1?480:width,h=height<1?160:height; VuoShader_setUniform_VuoImage((*instance)->shader,"jumpFloodFillImage",jumpFloodFillImage?jumpFloodFillImage:VuoImage_makeColorImage((VuoColor){0.02,0.02,0.02,1},w,h));
	VuoShader_setUniform_VuoImage((*instance)->shader,"sketchImage",sketchImage?sketchImage:VuoImage_makeColorImage((VuoColor){0.02,0.02,0.02,1},w,h));
	VuoShader_setUniform_VuoImage((*instance)->shader,"slidingHistoryImage",slidingHistoryImage?slidingHistoryImage:VuoImage_makeColorImage((VuoColor){0.02,0.02,0.02,1},w,h)); *image=VuoImageRenderer_render((*instance)->shader,w,h,VuoImageColorDepth_8); }
void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance) { VuoRelease((*instance)->shader); }
