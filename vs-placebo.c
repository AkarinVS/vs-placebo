#include "VapourSynth.h"
#include "deband.h"
#include "tonemap.h"
#include "resample.h"
#include "shader.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include <libplacebo/dispatch.h>
#include <libplacebo/utils/upload.h>

#include "vs-placebo.h"

#if defined(__APPLE__) && defined(PL_HAVE_OPENGL) && !defined(PL_HAVE_VULKAN)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>
#include <OpenGL/GL.h>

static CGLContextObj gl_context;
void init_gl() {
	CGLPixelFormatAttribute attributes[4] = {
		kCGLPFAAccelerated,
		kCGLPFAOpenGLProfile,
		(CGLPixelFormatAttribute) kCGLOGLPVersion_3_2_Core,
		(CGLPixelFormatAttribute) 0
	};
	CGLPixelFormatObj pix;
	CGLError errorCode;
	GLint num; // the number of possible pixel formats
	errorCode = CGLChoosePixelFormat( attributes, &pix, &num );
	if (errorCode != 0) {
		abort();
	}
	errorCode = CGLCreateContext(pix, NULL, &gl_context);
	if (errorCode != 0) {
		abort();
	}
	CGLDestroyPixelFormat(pix);

	errorCode = CGLSetCurrentContext(gl_context);
	if (errorCode != 0) {
		abort();
	}

	//fprintf(stderr, "init version: %s\n", glGetString(GL_VERSION));
}

void fini_gl() {
	//fprintf(stderr, "fini version: %s\n", glGetString(GL_VERSION));
	CGLSetCurrentContext(NULL);
	CGLDestroyContext(gl_context);
}
#else
inline void init_gl() {}
inline void fini_gl() {}
#endif

void logging(void *log_priv, enum pl_log_level level, const char *msg) {
    printf("%s\n", msg);
}

void *init(void) {
    struct priv *p = calloc(1, sizeof(struct priv));
    if (!p)
        return NULL;

    p->ctx = pl_context_create(PL_API_VER, &(struct pl_context_params) {
        .log_cb = logging,
        .log_level = PL_LOG_ERR
    });
    if (!p->ctx) {
        fprintf(stderr, "Failed initializing libplacebo\n");
        goto error;
    }

#ifdef PL_HAVE_VULKAN
    struct pl_vulkan_params vp = pl_vulkan_default_params;
    struct pl_vk_inst_params ip = pl_vk_inst_default_params;
//    ip.debug = true;
    vp.instance_params = &ip;
    p->vk = pl_vulkan_create(p->ctx, &vp);

    if (!p->vk) {
        fprintf(stderr, "Failed creating vulkan context\n");
        goto error;
    }

    // Give this a shorter name for convenience
    p->gpu = p->vk->gpu;
#elif defined(PL_HAVE_OPENGL)
    init_gl();
    struct pl_opengl_params gp = pl_opengl_default_params;
    gp.allow_software = true;
    gp.debug = true;

    p->gl = pl_opengl_create(p->ctx, &gp);
    if (!p->gl) {
	    fprintf(stderr, "Failed creating opengl context\n");
	    goto error;
    }

    fini_gl();
    p->gpu = p->gl->gpu;
#else
#error "unknown libplacebo gpu backend."
#endif

    p->dp = pl_dispatch_create(p->ctx, p->gpu);
    if (!p->dp) {
        fprintf(stderr, "Failed creating shader dispatch object\n");
        goto error;
    }

    p->rr = pl_renderer_create(p->ctx, p->gpu);
    if (!p->rr) {
        fprintf(stderr, "Failed creating renderer\n");
        goto error;
    }

    return p;

    error:
    uninit(p);
    return NULL;
}

void uninit(void *priv)
{
    struct priv *p = priv;
    for (int i = 0; i < MAX_PLANES; i++) {
        pl_tex_destroy(p->gpu, &p->tex_in[i]);
        pl_tex_destroy(p->gpu, &p->tex_out[i]);
    }

    pl_renderer_destroy(&p->rr);
    pl_shader_obj_destroy(&p->dither_state);
    pl_dispatch_destroy(&p->dp);
#if defined(PL_HAVE_VULKAN)
    pl_vulkan_destroy(&p->vk);
#elif defined(PL_HAVE_OPENGL)
    pl_opengl_destroy(&p->gl);
    fini_gl();
#endif
    pl_context_destroy(&p->ctx);

    free(p);
}

VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    configFunc("com.vs.placebo", "placebo", "libplacebo plugin for VapourSynth", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("Deband", "clip:clip;planes:int:opt;iterations:int:opt;threshold:float:opt;radius:float:opt;grain:float:opt;dither:int:opt;dither_algo:int:opt;renderer_api:int:opt", DebandCreate, 0, plugin);
    registerFunc("Resample", "clip:clip;width:int;height:int;filter:data:opt;clamp:float:opt;blur:float:opt;taper:float:opt;radius:float:opt;param1:float:opt;param2:float:opt;"
                             "sx:float:opt;sy:float:opt;antiring:float:opt;lut_entries:int:opt;cutoff:float:opt;"
                             "sigmoidize:int:opt;sigmoid_center:float:opt;sigmoid_slope:float:opt;linearize:int:opt;trc:int:opt", ResampleCreate, 0, plugin);
    registerFunc("Tonemap", "clip:clip;"
                            "srcp:int:opt;srct:int:opt;srcl:int:opt;src_peak:float:opt;src_avg:float:opt;src_scale:float:opt;"
                            "dstp:int:opt;dstt:int:opt;dstl:int:opt;dst_peak:float:opt;dst_avg:float:opt;dst_scale:float:opt;"
                            "dynamic_peak_detection:int:opt;smoothing_period:float:opt;scene_threshold_low:float:opt;scene_threshold_high:float:opt;"
                            "intent:int:opt;"
                            "tone_mapping_algo:int:opt;tone_mapping_param:float:opt;desaturation_strength:float:opt;desaturation_exponent:float:opt;desaturation_base:float:opt;"
                            "max_boost:float:opt;gamut_warning:int:opt;gamut_clipping:int:opt"
                            , TMCreate, 0, plugin);
    registerFunc("Shader", "clip:clip;shader:data:opt;width:int:opt;height:int:opt;chroma_loc:int:opt;matrix:int:opt;trc:int:opt;"
                           "linearize:int:opt;sigmoidize:int:opt;sigmoid_center:float:opt;sigmoid_slope:float:opt;"
                           "lut_entries:int:opt;antiring:float:opt;"
                           "filter:data:opt;clamp:float:opt;blur:float:opt;taper:float:opt;radius:float:opt;param1:float:opt;param2:float:opt;shader_s:data:opt;", SCreate, 0, plugin);
}
