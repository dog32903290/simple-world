#include "VuoImageRenderer.h"
VuoModuleMetadata({"title":"my_Batch40DistortProof","description":"Proof compositor for Batch 40 Lib.image.fx.distort.","keywords":["tixl","batch40","distort","proof"],"version":"1.0.0","dependencies":["VuoImageRenderer"]});
static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform sampler2D bubbleZoomImage;
	uniform sampler2D chromaticDistortionImage;
	uniform sampler2D displaceImage;
	uniform sampler2D distortAndShadeImage;
	uniform sampler2D edgeRepeatImage;
	uniform sampler2D fieldToImageImage;
	uniform sampler2D kochKaleidoskopeImage;
	uniform sampler2D polarCoordinatesImage;
	uniform sampler2D timeDisplaceImage;
	varying vec2 fragmentTextureCoordinate;
	void main() { vec2 st=fragmentTextureCoordinate; float band=floor(clamp(st.x,0.0,0.9999)*9.0); vec2 localSt=vec2(fract(st.x*9.0),st.y); vec4 color=(band < 1.0 ? texture2D(bubbleZoomImage, localSt) : (band < 2.0 ? texture2D(chromaticDistortionImage, localSt) : (band < 3.0 ? texture2D(displaceImage, localSt) : (band < 4.0 ? texture2D(distortAndShadeImage, localSt) : (band < 5.0 ? texture2D(edgeRepeatImage, localSt) : (band < 6.0 ? texture2D(fieldToImageImage, localSt) : (band < 7.0 ? texture2D(kochKaleidoskopeImage, localSt) : (band < 8.0 ? texture2D(polarCoordinatesImage, localSt) : texture2D(timeDisplaceImage, localSt))))))))); float edge=step(localSt.x,0.012)+step(0.988,localSt.x); if(edge>0.0) color.rgb=mix(color.rgb,vec3(1.0),0.35); gl_FragColor=color; }
);
struct nodeInstanceData { VuoShader shader; };
struct nodeInstanceData * nodeInstanceInit(void) { struct nodeInstanceData *i=(struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData)); VuoRegister(i,free); i->shader=VuoShader_make("my_Batch40DistortProof Shader"); VuoShader_addSource(i->shader,VuoMesh_IndividualTriangles,NULL,NULL,fragmentShaderSource); VuoRetain(i->shader); return i; }
void nodeInstanceEvent(VuoInstanceData(struct nodeInstanceData *) instance,VuoInputEvent() renderTick,
		VuoInputData(VuoImage) bubbleZoomImage,
		VuoInputData(VuoImage) chromaticDistortionImage,
		VuoInputData(VuoImage) displaceImage,
		VuoInputData(VuoImage) distortAndShadeImage,
		VuoInputData(VuoImage) edgeRepeatImage,
		VuoInputData(VuoImage) fieldToImageImage,
		VuoInputData(VuoImage) kochKaleidoskopeImage,
		VuoInputData(VuoImage) polarCoordinatesImage,
		VuoInputData(VuoImage) timeDisplaceImage,
		VuoInputData(VuoInteger,{"default":1440}) width,VuoInputData(VuoInteger,{"default":160}) height,VuoOutputData(VuoImage,{"name":"Image"}) image)
{ VuoInteger w=width<1?1440:width,h=height<1?160:height; VuoShader_setUniform_VuoImage((*instance)->shader,"bubbleZoomImage",bubbleZoomImage?bubbleZoomImage:VuoImage_makeColorImage((VuoColor){0.02,0.02,0.02,1},w,h));
	VuoShader_setUniform_VuoImage((*instance)->shader,"chromaticDistortionImage",chromaticDistortionImage?chromaticDistortionImage:VuoImage_makeColorImage((VuoColor){0.02,0.02,0.02,1},w,h));
	VuoShader_setUniform_VuoImage((*instance)->shader,"displaceImage",displaceImage?displaceImage:VuoImage_makeColorImage((VuoColor){0.02,0.02,0.02,1},w,h));
	VuoShader_setUniform_VuoImage((*instance)->shader,"distortAndShadeImage",distortAndShadeImage?distortAndShadeImage:VuoImage_makeColorImage((VuoColor){0.02,0.02,0.02,1},w,h));
	VuoShader_setUniform_VuoImage((*instance)->shader,"edgeRepeatImage",edgeRepeatImage?edgeRepeatImage:VuoImage_makeColorImage((VuoColor){0.02,0.02,0.02,1},w,h));
	VuoShader_setUniform_VuoImage((*instance)->shader,"fieldToImageImage",fieldToImageImage?fieldToImageImage:VuoImage_makeColorImage((VuoColor){0.02,0.02,0.02,1},w,h));
	VuoShader_setUniform_VuoImage((*instance)->shader,"kochKaleidoskopeImage",kochKaleidoskopeImage?kochKaleidoskopeImage:VuoImage_makeColorImage((VuoColor){0.02,0.02,0.02,1},w,h));
	VuoShader_setUniform_VuoImage((*instance)->shader,"polarCoordinatesImage",polarCoordinatesImage?polarCoordinatesImage:VuoImage_makeColorImage((VuoColor){0.02,0.02,0.02,1},w,h));
	VuoShader_setUniform_VuoImage((*instance)->shader,"timeDisplaceImage",timeDisplaceImage?timeDisplaceImage:VuoImage_makeColorImage((VuoColor){0.02,0.02,0.02,1},w,h)); *image=VuoImageRenderer_render((*instance)->shader,w,h,VuoImageColorDepth_8); }
void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance) { VuoRelease((*instance)->shader); }
