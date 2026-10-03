#include "litegl/litegl_state.h"
#include <string.h>

void litegl_state_init(LiteGLShadowState* state) {
    if (!state) return;
    memset(state, 0, sizeof(LiteGLShadowState));

    /* Default ToGL/GL initial states */
    state->depth_test_enable = false;
    state->depth_write_mask = true;
    state->depth_func = GL_LESS;

    state->blend_enable = false;
    state->blend_src = GL_ONE;
    state->blend_dst = GL_ZERO;

    state->cull_face_enable = false;
    state->cull_face_mode = GL_BACK;
    state->front_face = GL_CCW;
    state->scissor_test_enable = false;

    state->color_mask_r = true;
    state->color_mask_g = true;
    state->color_mask_b = true;
    state->color_mask_a = true;

    state->viewport_x = 0;
    state->viewport_y = 0;
    state->viewport_w = 0;
    state->viewport_h = 0;

    state->scissor_x = 0;
    state->scissor_y = 0;
    state->scissor_w = 0;
    state->scissor_h = 0;

    state->active_program = 0;
    state->active_texture_unit = 0;
    for (int i = 0; i < LITEGL_MAX_TEXTURE_STAGES; ++i) {
        state->bound_textures_2d[i] = 0;
    }
    state->bound_array_buffer = 0;
    state->bound_element_buffer = 0;
    state->bound_vao = 0;
}

void litegl_state_apply_defaults(LiteGLDispatch* gl, LiteGLShadowState* state) {
    if (!gl || !state) return;

    if (state->depth_test_enable) {
        if (gl->glEnable) gl->glEnable(GL_DEPTH_TEST);
    } else {
        if (gl->glDisable) gl->glDisable(GL_DEPTH_TEST);
    }
    if (gl->glDepthMask) gl->glDepthMask(state->depth_write_mask ? GL_TRUE : GL_FALSE);
    if (gl->glDepthFunc) gl->glDepthFunc(state->depth_func);

    if (state->blend_enable) {
        if (gl->glEnable) gl->glEnable(GL_BLEND);
    } else {
        if (gl->glDisable) gl->glDisable(GL_BLEND);
    }
    if (gl->glBlendFunc) gl->glBlendFunc(state->blend_src, state->blend_dst);

    if (state->cull_face_enable) {
        if (gl->glEnable) gl->glEnable(GL_CULL_FACE);
    } else {
        if (gl->glDisable) gl->glDisable(GL_CULL_FACE);
    }
    if (gl->glCullFace) gl->glCullFace(state->cull_face_mode);
    if (gl->glFrontFace) gl->glFrontFace(state->front_face);

    if (state->scissor_test_enable) {
        if (gl->glEnable) gl->glEnable(GL_SCISSOR_TEST);
    } else {
        if (gl->glDisable) gl->glDisable(GL_SCISSOR_TEST);
    }

    if (gl->glColorMask) {
        gl->glColorMask(
            state->color_mask_r ? GL_TRUE : GL_FALSE,
            state->color_mask_g ? GL_TRUE : GL_FALSE,
            state->color_mask_b ? GL_TRUE : GL_FALSE,
            state->color_mask_a ? GL_TRUE : GL_FALSE
        );
    }
}
