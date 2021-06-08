#ifndef VS_PLACEBO_LIBRARY_H
#define VS_PLACEBO_LIBRARY_H

#include <libplacebo/config.h>
#include <libplacebo/dispatch.h>
#include <libplacebo/shaders/sampling.h>
#include <libplacebo/utils/upload.h>
#if defined(PL_HAVE_VULKAN)
#include <libplacebo/vulkan.h>
#elif defined(PL_HAVE_OPENGL)
#include <libplacebo/opengl.h>
#else
#error "libplacebo does not have either vulkan or opengl backend."
#endif

struct format {
    int num_comps;
    int bitdepth;
};

struct plane {
    int subx, suby; // subsampling shift
    struct format fmt;
    size_t stride;
    void *data;
};

#define MAX_PLANES 4

struct image {
    int width, height;
    int num_planes;
    struct plane planes[MAX_PLANES];
};

struct priv {
    struct pl_context *ctx;
#if defined(PL_HAVE_VULKAN)
    const struct pl_vulkan *vk;
#elif defined(PL_HAVE_OPENGL)
    const struct pl_opengl *gl;
#endif
    const struct pl_gpu *gpu;
    struct pl_dispatch *dp;
    struct pl_shader_obj *dither_state;
    struct pl_renderer *rr;
    const struct pl_tex *tex_in[MAX_PLANES];
    const struct pl_tex *tex_out[MAX_PLANES];
};

void *init(void);
void uninit(void *priv);

// dirty hack for single threaded OpenGL.
void init_gl(void);
void fini_gl(void);

#endif //VS_PLACEBO_LIBRARY_H