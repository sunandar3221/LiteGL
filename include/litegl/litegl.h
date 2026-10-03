#ifndef LITEGL_H
#define LITEGL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * LiteGL - Ultra-lightweight OpenGL rendering layer inspired by Valve's ToGL.
 * Designed specifically for low-overhead rendering on integrated GPUs and legacy hardware
 * where Vulkan is unavailable or suffers from driver overhead/stutters.
 *
 * Core ToGL Features in LiteGL:
 * 1. Aggressive GL State Shadowing & Redundant Filtering (eliminates costly GL context switching)
 * 2. Dynamic Streaming Buffers with Discard/Ring semantics (prevents GPU synchronization stalls)
 * 3. Uniform dirty tracking and batched updates
 * 4. High-efficiency draw batching
 * 5. Minimal memory footprint (<100KB heap usage)
 */

typedef struct LiteGLContext LiteGLContext;
typedef struct LiteGLBuffer LiteGLBuffer;
typedef struct LiteGLTexture LiteGLTexture;
typedef struct LiteGLShader LiteGLShader;

/* Render states mimicking Direct3D / ToGL state machine */
typedef enum {
    LGL_RS_ZENABLE          = 1,   /* Depth test enable: 0=disable, 1=enable */
    LGL_RS_ZWRITEENABLE     = 2,   /* Depth write mask: 0=disable, 1=enable */
    LGL_RS_ZFUNC            = 3,   /* Depth test function (LGL_CMP_*) */
    LGL_RS_ALPHABLENDENABLE = 4,   /* Alpha blend enable: 0=disable, 1=enable */
    LGL_RS_SRCBLEND         = 5,   /* Source blend factor (LGL_BLEND_*) */
    LGL_RS_DESTBLEND        = 6,   /* Destination blend factor (LGL_BLEND_*) */
    LGL_RS_CULLMODE         = 7,   /* Culling mode (LGL_CULL_*) */
    LGL_RS_SCISSORTESTENABLE= 8,   /* Scissor test enable: 0=disable, 1=enable */
    LGL_RS_COLORWRITEENABLE = 9,   /* Color mask (bitmask 0xF = RGBA) */
    LGL_RS_MAX
} LiteGLRenderState;

typedef enum {
    LGL_CMP_NEVER        = 1,
    LGL_CMP_LESS         = 2,
    LGL_CMP_EQUAL        = 3,
    LGL_CMP_LESSEQUAL    = 4,
    LGL_CMP_GREATER      = 5,
    LGL_CMP_NOTEQUAL     = 6,
    LGL_CMP_GREATEREQUAL = 7,
    LGL_CMP_ALWAYS       = 8
} LiteGLCompareFunc;

typedef enum {
    LGL_BLEND_ZERO            = 1,
    LGL_BLEND_ONE             = 2,
    LGL_BLEND_SRCCOLOR        = 3,
    LGL_BLEND_INVSRCCOLOR     = 4,
    LGL_BLEND_SRCALPHA         = 5,
    LGL_BLEND_INVSRCALPHA     = 6,
    LGL_BLEND_DESTALPHA       = 7,
    LGL_BLEND_INVDESTALPHA    = 8,
    LGL_BLEND_DESTCOLOR       = 9,
    LGL_BLEND_INVDESTCOLOR    = 10
} LiteGLBlendFactor;

typedef enum {
    LGL_CULL_NONE = 1,
    LGL_CULL_CW   = 2,
    LGL_CULL_CCW  = 3
} LiteGLCullMode;

typedef enum {
    LGL_TEX_FILTER_NEAREST = 0,
    LGL_TEX_FILTER_LINEAR  = 1
} LiteGLFilter;

typedef enum {
    LGL_TEX_WRAP_REPEAT = 0,
    LGL_TEX_WRAP_CLAMP  = 1
} LiteGLWrap;

typedef enum {
    LGL_BUFFER_VERTEX = 1,
    LGL_BUFFER_INDEX  = 2,
    LGL_BUFFER_UNIFORM = 3
} LiteGLBufferType;

typedef enum {
    LGL_BUFFER_STATIC  = 1,
    LGL_BUFFER_DYNAMIC = 2,
    LGL_BUFFER_STREAM  = 3
} LiteGLBufferUsage;

typedef enum {
    LGL_PRIM_TRIANGLES = 1,
    LGL_PRIM_TRIANGLE_STRIP = 2,
    LGL_PRIM_LINES     = 3,
    LGL_PRIM_POINTS    = 4
} LiteGLPrimitiveType;

/* Vertex Attribute definition */
typedef struct {
    uint32_t location;
    int32_t  size;       /* 1, 2, 3, 4 */
    uint32_t type;       /* e.g. GL_FLOAT */
    bool     normalized;
    uint32_t stride;
    size_t   offset;
} LiteGLVertexAttrib;

/* Performance statistics for profiling and comparisons */
typedef struct {
    uint64_t draw_calls;
    uint64_t state_changes_requested;
    uint64_t state_changes_filtered;  /* redundant calls avoided thanks to ToGL-style shadow state */
    uint64_t texture_binds_requested;
    uint64_t texture_binds_filtered;
    uint64_t program_binds_requested;
    uint64_t program_binds_filtered;
    uint64_t buffer_binds_requested;
    uint64_t buffer_binds_filtered;
    uint64_t dynamic_buffer_flushes;
    uint64_t bytes_uploaded;
} LiteGLStats;

/* Function pointer loader type (e.g., SDL_GL_GetProcAddress or glXGetProcAddress) */
typedef void* (*LiteGLProcAddressFunc)(const char* name);

/* Context creation configuration */
typedef struct {
    LiteGLProcAddressFunc proc_loader;
    bool enable_state_caching;
    bool enable_batching;
    size_t dynamic_vbo_size; /* Default 2MB ring buffer */
    size_t dynamic_ibo_size; /* Default 512KB ring buffer */
} LiteGLConfig;

/* Initialization and Lifecycle */
LiteGLConfig litegl_default_config(LiteGLProcAddressFunc proc_loader);
LiteGLContext* litegl_create_context(const LiteGLConfig* config);
void litegl_destroy_context(LiteGLContext* ctx);

/* Frame boundaries */
void litegl_begin_frame(LiteGLContext* ctx);
void litegl_end_frame(LiteGLContext* ctx);

/* Clearing */
void litegl_clear(LiteGLContext* ctx, uint32_t clear_flags, float r, float g, float b, float a, float depth);

/* State management (ToGL cached) */
void litegl_set_render_state(LiteGLContext* ctx, LiteGLRenderState state, uint32_t value);
uint32_t litegl_get_render_state(LiteGLContext* ctx, LiteGLRenderState state);
void litegl_set_viewport(LiteGLContext* ctx, int x, int y, int width, int height);
void litegl_set_scissor(LiteGLContext* ctx, int x, int y, int width, int height);

/* Texture management */
LiteGLTexture* litegl_create_texture_2d(LiteGLContext* ctx, int width, int height, int channels, const void* data, LiteGLFilter filter, LiteGLWrap wrap);
void litegl_destroy_texture(LiteGLContext* ctx, LiteGLTexture* tex);
void litegl_bind_texture(LiteGLContext* ctx, uint32_t stage, LiteGLTexture* tex);

/* Shader management */
LiteGLShader* litegl_create_shader(LiteGLContext* ctx, const char* vertex_src, const char* fragment_src);
void litegl_destroy_shader(LiteGLContext* ctx, LiteGLShader* shader);
void litegl_bind_shader(LiteGLContext* ctx, LiteGLShader* shader);
int litegl_get_uniform_location(LiteGLShader* shader, const char* name);
void litegl_set_uniform_1f(LiteGLContext* ctx, int location, float v);
void litegl_set_uniform_2f(LiteGLContext* ctx, int location, float x, float y);
void litegl_set_uniform_4f(LiteGLContext* ctx, int location, float x, float y, float z, float w);
void litegl_set_uniform_mat4(LiteGLContext* ctx, int location, const float* mat4);

/* Buffers */
LiteGLBuffer* litegl_create_buffer(LiteGLContext* ctx, LiteGLBufferType type, LiteGLBufferUsage usage, size_t size, const void* initial_data);
void litegl_destroy_buffer(LiteGLContext* ctx, LiteGLBuffer* buf);
void litegl_update_buffer(LiteGLContext* ctx, LiteGLBuffer* buf, size_t offset, size_t size, const void* data, bool discard);
void litegl_bind_buffer(LiteGLContext* ctx, LiteGLBuffer* buf);

/* Dynamic Ring Streamer (D3D Discard / No-Overwrite model like ToGL) */
void* litegl_stream_alloc_vertices(LiteGLContext* ctx, size_t size, uint32_t* out_byte_offset);
void* litegl_stream_alloc_indices(LiteGLContext* ctx, size_t size, uint32_t* out_byte_offset);
void litegl_stream_upload_vertices(LiteGLContext* ctx, const void* data, size_t size, uint32_t* out_byte_offset);
void litegl_stream_upload_indices(LiteGLContext* ctx, const void* data, size_t size, uint32_t* out_byte_offset);

/* Draw calls */
void litegl_enable_vertex_attrib_array(LiteGLContext* ctx, uint32_t index);
void litegl_disable_vertex_attrib_array(LiteGLContext* ctx, uint32_t index);
void litegl_vertex_attrib_pointer(LiteGLContext* ctx, uint32_t index, int size, uint32_t type, bool normalized, uint32_t stride, const void* pointer);

void litegl_draw_arrays(LiteGLContext* ctx, LiteGLPrimitiveType prim, uint32_t first, uint32_t count);
void litegl_draw_elements(LiteGLContext* ctx, LiteGLPrimitiveType prim, uint32_t count, uint32_t index_type, const void* indices);

/* Flush any pending batched commands */
void litegl_flush(LiteGLContext* ctx);

/* Statistics and Diagnostics */
void litegl_get_stats(LiteGLContext* ctx, LiteGLStats* out_stats);
void litegl_reset_stats(LiteGLContext* ctx);

#ifdef __cplusplus
}
#endif

#endif /* LITEGL_H */
