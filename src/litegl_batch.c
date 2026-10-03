#include "litegl/litegl_batch.h"
#include "litegl/litegl_buffer.h"
#include "litegl/litegl_gl.h"

#include <stdlib.h>
#include <string.h>

struct LiteGLBatcher {
    LiteGLContext*     ctx;
    LiteGLShader*      shader;
    LiteGLTexture*     current_texture;
    uint32_t           texture_stage;

    size_t             max_quads;
    size_t             vertex_count;
    size_t             max_vertices;

    LiteGLBatchVertex* vertex_buffer;
    LiteGLBuffer*      static_ibo;

    bool               in_batch;
};

LiteGLBatcher* litegl_create_batcher(LiteGLContext* ctx, size_t max_quads) {
    if (!ctx || max_quads == 0) return NULL;

    LiteGLBatcher* b = (LiteGLBatcher*)calloc(1, sizeof(LiteGLBatcher));
    if (!b) return NULL;

    b->ctx = ctx;
    b->max_quads = max_quads;
    b->max_vertices = max_quads * 4;
    b->vertex_buffer = (LiteGLBatchVertex*)malloc(sizeof(LiteGLBatchVertex) * b->max_vertices);
    if (!b->vertex_buffer) {
        free(b);
        return NULL;
    }

    /* Pre-fill static index buffer (6 indices per quad: 0,1,2, 2,3,0) */
    size_t total_indices = max_quads * 6;
    uint32_t* indices = (uint32_t*)malloc(sizeof(uint32_t) * total_indices);
    if (!indices) {
        free(b->vertex_buffer);
        free(b);
        return NULL;
    }

    for (size_t q = 0; q < max_quads; ++q) {
        uint32_t v_base = (uint32_t)(q * 4);
        size_t i_base = q * 6;
        indices[i_base + 0] = v_base + 0;
        indices[i_base + 1] = v_base + 1;
        indices[i_base + 2] = v_base + 2;
        indices[i_base + 3] = v_base + 2;
        indices[i_base + 4] = v_base + 3;
        indices[i_base + 5] = v_base + 0;
    }

    b->static_ibo = litegl_create_buffer(ctx, LGL_BUFFER_INDEX, LGL_BUFFER_STATIC, sizeof(uint32_t) * total_indices, indices);
    free(indices);

    return b;
}

void litegl_destroy_batcher(LiteGLBatcher* b) {
    if (!b) return;

    if (b->static_ibo) {
        litegl_destroy_buffer(b->ctx, b->static_ibo);
    }
    if (b->vertex_buffer) {
        free(b->vertex_buffer);
    }
    free(b);
}

void litegl_batch_begin(LiteGLBatcher* b, LiteGLShader* shader) {
    if (!b) return;
    b->shader = shader;
    b->vertex_count = 0;
    b->in_batch = true;

    if (shader) {
        litegl_bind_shader(b->ctx, shader);
    }
}

void litegl_batch_set_state(LiteGLBatcher* b, LiteGLRenderState state, uint32_t value) {
    if (!b) return;
    /* If state actually changes, flush pending quads before switching */
    if (litegl_get_render_state(b->ctx, state) != value) {
        if (b->vertex_count > 0) {
            litegl_batch_flush(b);
        }
        litegl_set_render_state(b->ctx, state, value);
    }
}

void litegl_batch_set_texture(LiteGLBatcher* b, uint32_t stage, LiteGLTexture* tex) {
    if (!b) return;
    if (b->current_texture != tex || b->texture_stage != stage) {
        if (b->vertex_count > 0) {
            litegl_batch_flush(b);
        }
        b->current_texture = tex;
        b->texture_stage = stage;
        litegl_bind_texture(b->ctx, stage, tex);
    }
}

void litegl_batch_quad(
    LiteGLBatcher* b,
    float x0, float y0, float u0, float v0,
    float x1, float y1, float u1, float v1,
    float x2, float y2, float u2, float v2,
    float x3, float y3, float u3, float v3,
    uint32_t color
) {
    if (!b) return;

    if (b->vertex_count + 4 > b->max_vertices) {
        litegl_batch_flush(b);
    }

    LiteGLBatchVertex* v = &b->vertex_buffer[b->vertex_count];
    v[0] = (LiteGLBatchVertex){ x0, y0, u0, v0, color };
    v[1] = (LiteGLBatchVertex){ x1, y1, u1, v1, color };
    v[2] = (LiteGLBatchVertex){ x2, y2, u2, v2, color };
    v[3] = (LiteGLBatchVertex){ x3, y3, u3, v3, color };

    b->vertex_count += 4;
}

void litegl_batch_rect(
    LiteGLBatcher* b,
    float x, float y, float width, float height,
    float u0, float v0, float u1, float v1,
    uint32_t color
) {
    float x0 = x;
    float y0 = y;
    float x1 = x + width;
    float y1 = y + height;

    litegl_batch_quad(
        b,
        x0, y0, u0, v0,
        x1, y0, u1, v0,
        x1, y1, u1, v1,
        x0, y1, u0, v1,
        color
    );
}

void litegl_batch_flush(LiteGLBatcher* b) {
    if (!b || b->vertex_count == 0) return;

    size_t quads = b->vertex_count / 4;
    size_t index_count = quads * 6;
    size_t upload_bytes = b->vertex_count * sizeof(LiteGLBatchVertex);

    /* Stream vertices into dynamic ring buffer with zero stalls */
    uint32_t vbo_offset = 0;
    litegl_stream_upload_vertices(b->ctx, b->vertex_buffer, upload_bytes, &vbo_offset);

    /* Bind static index buffer */
    litegl_bind_buffer(b->ctx, b->static_ibo);

    /* Configure 20-byte packed vertex layout */
    litegl_enable_vertex_attrib_array(b->ctx, 0);
    litegl_vertex_attrib_pointer(b->ctx, 0, 2, GL_FLOAT, false, sizeof(LiteGLBatchVertex), (void*)(size_t)vbo_offset);

    litegl_enable_vertex_attrib_array(b->ctx, 1);
    litegl_vertex_attrib_pointer(b->ctx, 1, 2, GL_FLOAT, false, sizeof(LiteGLBatchVertex), (void*)(size_t)(vbo_offset + sizeof(float) * 2));

    litegl_enable_vertex_attrib_array(b->ctx, 2);
    litegl_vertex_attrib_pointer(b->ctx, 2, 4, GL_UNSIGNED_BYTE, true, sizeof(LiteGLBatchVertex), (void*)(size_t)(vbo_offset + sizeof(float) * 4));

    /* Issue single draw call for all accumulated quads */
    litegl_draw_elements(b->ctx, LGL_PRIM_TRIANGLES, (uint32_t)index_count, GL_UNSIGNED_INT, (void*)0);

    b->vertex_count = 0;
}

void litegl_batch_end(LiteGLBatcher* b) {
    if (!b) return;
    if (b->vertex_count > 0) {
        litegl_batch_flush(b);
    }
    b->in_batch = false;
}
