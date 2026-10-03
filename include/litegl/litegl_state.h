#ifndef LITEGL_STATE_H
#define LITEGL_STATE_H

#include "litegl.h"
#include "litegl_gl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LITEGL_MAX_TEXTURE_STAGES 16

/* Shadow state structure mirroring ToGL's GLState */
typedef struct {
    /* Depth Stencil */
    bool     depth_test_enable;
    bool     depth_write_mask;
    GLenum   depth_func;

    /* Blending */
    bool     blend_enable;
    GLenum   blend_src;
    GLenum   blend_dst;

    /* Rasterizer */
    bool     cull_face_enable;
    GLenum   cull_face_mode;
    GLenum   front_face;
    bool     scissor_test_enable;

    /* Color mask */
    bool     color_mask_r;
    bool     color_mask_g;
    bool     color_mask_b;
    bool     color_mask_a;

    /* Viewport & Scissor */
    int      viewport_x;
    int      viewport_y;
    int      viewport_w;
    int      viewport_h;

    int      scissor_x;
    int      scissor_y;
    int      scissor_w;
    int      scissor_h;

    /* Active bindings */
    GLuint   active_program;
    uint32_t active_texture_unit;
    GLuint   bound_textures_2d[LITEGL_MAX_TEXTURE_STAGES];
    GLuint   bound_array_buffer;
    GLuint   bound_element_buffer;
    GLuint   bound_vao;
} LiteGLShadowState;

void litegl_state_init(LiteGLShadowState* state);
void litegl_state_apply_defaults(LiteGLDispatch* gl, LiteGLShadowState* state);

#ifdef __cplusplus
}
#endif

#endif /* LITEGL_STATE_H */
