#include "litegl/litegl.h"
#include "litegl/litegl_gl.h"
#include "litegl/litegl_state.h"
#include "litegl/litegl_buffer.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct LiteGLContext {
    LiteGLDispatch    gl;
    LiteGLConfig      config;
    LiteGLShadowState state;
    LiteGLStats       stats;

    LiteGLRingBuffer  vbo_ring;
    LiteGLRingBuffer  ibo_ring;

    GLuint            default_vao;
    bool              in_frame;
};

struct LiteGLTexture {
    GLuint id;
    int    width;
    int    height;
    int    channels;
};

struct LiteGLShader {
    GLuint program;
    GLuint vert_shader;
    GLuint frag_shader;
};

struct LiteGLBuffer {
    GLuint gl_id;
    GLenum gl_target;
    GLenum gl_usage;
    size_t size;
};

LiteGLConfig litegl_default_config(LiteGLProcAddressFunc proc_loader) {
    LiteGLConfig cfg;
    cfg.proc_loader = proc_loader;
    cfg.enable_state_caching = true;
    cfg.enable_batching = true;
    cfg.dynamic_vbo_size = 2 * 1024 * 1024; /* 2MB default dynamic ring buffer */
    cfg.dynamic_ibo_size = 512 * 1024;      /* 512KB default dynamic index ring */
    return cfg;
}

LiteGLContext* litegl_create_context(const LiteGLConfig* config) {
    if (!config || !config->proc_loader) return NULL;

    LiteGLContext* ctx = (LiteGLContext*)calloc(1, sizeof(LiteGLContext));
    if (!ctx) return NULL;

    ctx->config = *config;

    if (!litegl_load_gl(&ctx->gl, config->proc_loader)) {
        free(ctx);
        return NULL;
    }

    litegl_state_init(&ctx->state);
    litegl_state_apply_defaults(&ctx->gl, &ctx->state);

    /* Generate default VAO (required in modern Core OpenGL) */
    if (ctx->gl.glGenVertexArrays) {
        ctx->gl.glGenVertexArrays(1, &ctx->default_vao);
        ctx->gl.glBindVertexArray(ctx->default_vao);
        ctx->state.bound_vao = ctx->default_vao;
    }

    /* Initialize ToGL-style dynamic ring buffers */
    litegl_ring_buffer_init(&ctx->gl, &ctx->vbo_ring, GL_ARRAY_BUFFER, config->dynamic_vbo_size);
    litegl_ring_buffer_init(&ctx->gl, &ctx->ibo_ring, GL_ELEMENT_ARRAY_BUFFER, config->dynamic_ibo_size);

    return ctx;
}

void litegl_destroy_context(LiteGLContext* ctx) {
    if (!ctx) return;

    litegl_ring_buffer_destroy(&ctx->gl, &ctx->vbo_ring);
    litegl_ring_buffer_destroy(&ctx->gl, &ctx->ibo_ring);

    if (ctx->default_vao && ctx->gl.glDeleteVertexArrays) {
        ctx->gl.glDeleteVertexArrays(1, &ctx->default_vao);
    }

    free(ctx);
}

void litegl_begin_frame(LiteGLContext* ctx) {
    if (!ctx) return;
    ctx->in_frame = true;
}

void litegl_end_frame(LiteGLContext* ctx) {
    if (!ctx) return;
    ctx->in_frame = false;
}

void litegl_clear(LiteGLContext* ctx, uint32_t clear_flags, float r, float g, float b, float a, float depth) {
    if (!ctx) return;

    GLbitfield mask = 0;
    if (clear_flags & 1) { /* Color */
        mask |= GL_COLOR_BUFFER_BIT;
        ctx->gl.glClearColor(r, g, b, a);
    }
    if (clear_flags & 2) { /* Depth */
        mask |= GL_DEPTH_BUFFER_BIT;
        if (ctx->gl.glClearDepth) {
            ctx->gl.glClearDepth(depth);
        }
    }
    ctx->gl.glClear(mask);
}

static GLenum map_compare_func(LiteGLCompareFunc func) {
    switch (func) {
        case LGL_CMP_NEVER:        return GL_NEVER;
        case LGL_CMP_LESS:         return GL_LESS;
        case LGL_CMP_EQUAL:        return GL_EQUAL;
        case LGL_CMP_LESSEQUAL:    return GL_LEQUAL;
        case LGL_CMP_GREATER:      return GL_GREATER;
        case LGL_CMP_NOTEQUAL:     return GL_NOTEQUAL;
        case LGL_CMP_GREATEREQUAL: return GL_GEQUAL;
        case LGL_CMP_ALWAYS:       return GL_ALWAYS;
        default:                   return GL_LESS;
    }
}

static GLenum map_blend_factor(LiteGLBlendFactor factor) {
    switch (factor) {
        case LGL_BLEND_ZERO:         return GL_ZERO;
        case LGL_BLEND_ONE:          return GL_ONE;
        case LGL_BLEND_SRCCOLOR:     return GL_SRC_COLOR;
        case LGL_BLEND_INVSRCCOLOR:  return GL_ONE_MINUS_SRC_COLOR;
        case LGL_BLEND_SRCALPHA:     return GL_SRC_ALPHA;
        case LGL_BLEND_INVSRCALPHA:  return GL_ONE_MINUS_SRC_ALPHA;
        case LGL_BLEND_DESTALPHA:    return GL_DST_ALPHA;
        case LGL_BLEND_INVDESTALPHA: return GL_ONE_MINUS_DST_ALPHA;
        case LGL_BLEND_DESTCOLOR:    return GL_DST_COLOR;
        case LGL_BLEND_INVDESTCOLOR: return GL_ONE_MINUS_DST_COLOR;
        default:                     return GL_ONE;
    }
}

void litegl_set_render_state(LiteGLContext* ctx, LiteGLRenderState state, uint32_t value) {
    if (!ctx) return;
    ctx->stats.state_changes_requested++;

    if (!ctx->config.enable_state_caching) {
        /* Bypass caching mode for direct comparison */
        switch (state) {
            case LGL_RS_ZENABLE:
                if (value) ctx->gl.glEnable(GL_DEPTH_TEST);
                else ctx->gl.glDisable(GL_DEPTH_TEST);
                break;
            case LGL_RS_ZWRITEENABLE:
                ctx->gl.glDepthMask(value ? GL_TRUE : GL_FALSE);
                break;
            case LGL_RS_ZFUNC:
                ctx->gl.glDepthFunc(map_compare_func((LiteGLCompareFunc)value));
                break;
            case LGL_RS_ALPHABLENDENABLE:
                if (value) ctx->gl.glEnable(GL_BLEND);
                else ctx->gl.glDisable(GL_BLEND);
                break;
            case LGL_RS_SRCBLEND:
                ctx->gl.glBlendFunc(map_blend_factor((LiteGLBlendFactor)value), ctx->state.blend_dst);
                ctx->state.blend_src = map_blend_factor((LiteGLBlendFactor)value);
                break;
            case LGL_RS_DESTBLEND:
                ctx->gl.glBlendFunc(ctx->state.blend_src, map_blend_factor((LiteGLBlendFactor)value));
                ctx->state.blend_dst = map_blend_factor((LiteGLBlendFactor)value);
                break;
            case LGL_RS_CULLMODE:
                if (value == LGL_CULL_NONE) {
                    ctx->gl.glDisable(GL_CULL_FACE);
                } else {
                    ctx->gl.glEnable(GL_CULL_FACE);
                    ctx->gl.glCullFace(value == LGL_CULL_CW ? GL_FRONT : GL_BACK);
                }
                break;
            case LGL_RS_SCISSORTESTENABLE:
                if (value) ctx->gl.glEnable(GL_SCISSOR_TEST);
                else ctx->gl.glDisable(GL_SCISSOR_TEST);
                break;
            default: break;
        }
        return;
    }

    /* ToGL State Filtering */
    switch (state) {
        case LGL_RS_ZENABLE: {
            bool en = (value != 0);
            if (ctx->state.depth_test_enable == en) {
                ctx->stats.state_changes_filtered++;
                return;
            }
            ctx->state.depth_test_enable = en;
            if (en) ctx->gl.glEnable(GL_DEPTH_TEST);
            else ctx->gl.glDisable(GL_DEPTH_TEST);
            break;
        }
        case LGL_RS_ZWRITEENABLE: {
            bool mask = (value != 0);
            if (ctx->state.depth_write_mask == mask) {
                ctx->stats.state_changes_filtered++;
                return;
            }
            ctx->state.depth_write_mask = mask;
            ctx->gl.glDepthMask(mask ? GL_TRUE : GL_FALSE);
            break;
        }
        case LGL_RS_ZFUNC: {
            GLenum gl_fn = map_compare_func((LiteGLCompareFunc)value);
            if (ctx->state.depth_func == gl_fn) {
                ctx->stats.state_changes_filtered++;
                return;
            }
            ctx->state.depth_func = gl_fn;
            ctx->gl.glDepthFunc(gl_fn);
            break;
        }
        case LGL_RS_ALPHABLENDENABLE: {
            bool en = (value != 0);
            if (ctx->state.blend_enable == en) {
                ctx->stats.state_changes_filtered++;
                return;
            }
            ctx->state.blend_enable = en;
            if (en) ctx->gl.glEnable(GL_BLEND);
            else ctx->gl.glDisable(GL_BLEND);
            break;
        }
        case LGL_RS_SRCBLEND: {
            GLenum src = map_blend_factor((LiteGLBlendFactor)value);
            if (ctx->state.blend_src == src) {
                ctx->stats.state_changes_filtered++;
                return;
            }
            ctx->state.blend_src = src;
            ctx->gl.glBlendFunc(src, ctx->state.blend_dst);
            break;
        }
        case LGL_RS_DESTBLEND: {
            GLenum dst = map_blend_factor((LiteGLBlendFactor)value);
            if (ctx->state.blend_dst == dst) {
                ctx->stats.state_changes_filtered++;
                return;
            }
            ctx->state.blend_dst = dst;
            ctx->gl.glBlendFunc(ctx->state.blend_src, dst);
            break;
        }
        case LGL_RS_CULLMODE: {
            bool en = (value != LGL_CULL_NONE);
            GLenum mode = (value == LGL_CULL_CW) ? GL_FRONT : GL_BACK;
            if (ctx->state.cull_face_enable == en && (!en || ctx->state.cull_face_mode == mode)) {
                ctx->stats.state_changes_filtered++;
                return;
            }
            ctx->state.cull_face_enable = en;
            ctx->state.cull_face_mode = mode;
            if (en) {
                ctx->gl.glEnable(GL_CULL_FACE);
                ctx->gl.glCullFace(mode);
            } else {
                ctx->gl.glDisable(GL_CULL_FACE);
            }
            break;
        }
        case LGL_RS_SCISSORTESTENABLE: {
            bool en = (value != 0);
            if (ctx->state.scissor_test_enable == en) {
                ctx->stats.state_changes_filtered++;
                return;
            }
            ctx->state.scissor_test_enable = en;
            if (en) ctx->gl.glEnable(GL_SCISSOR_TEST);
            else ctx->gl.glDisable(GL_SCISSOR_TEST);
            break;
        }
        default: break;
    }
}

uint32_t litegl_get_render_state(LiteGLContext* ctx, LiteGLRenderState state) {
    if (!ctx) return 0;
    switch (state) {
        case LGL_RS_ZENABLE:           return ctx->state.depth_test_enable ? 1 : 0;
        case LGL_RS_ZWRITEENABLE:      return ctx->state.depth_write_mask ? 1 : 0;
        case LGL_RS_ALPHABLENDENABLE:  return ctx->state.blend_enable ? 1 : 0;
        case LGL_RS_SCISSORTESTENABLE: return ctx->state.scissor_test_enable ? 1 : 0;
        default: return 0;
    }
}

void litegl_set_viewport(LiteGLContext* ctx, int x, int y, int width, int height) {
    if (!ctx) return;
    if (ctx->config.enable_state_caching) {
        if (ctx->state.viewport_x == x && ctx->state.viewport_y == y &&
            ctx->state.viewport_w == width && ctx->state.viewport_h == height) {
            ctx->stats.state_changes_filtered++;
            return;
        }
    }
    ctx->state.viewport_x = x;
    ctx->state.viewport_y = y;
    ctx->state.viewport_w = width;
    ctx->state.viewport_h = height;
    ctx->gl.glViewport(x, y, width, height);
}

void litegl_set_scissor(LiteGLContext* ctx, int x, int y, int width, int height) {
    if (!ctx) return;
    if (ctx->config.enable_state_caching) {
        if (ctx->state.scissor_x == x && ctx->state.scissor_y == y &&
            ctx->state.scissor_w == width && ctx->state.scissor_h == height) {
            ctx->stats.state_changes_filtered++;
            return;
        }
    }
    ctx->state.scissor_x = x;
    ctx->state.scissor_y = y;
    ctx->state.scissor_w = width;
    ctx->state.scissor_h = height;
    ctx->gl.glScissor(x, y, width, height);
}

LiteGLTexture* litegl_create_texture_2d(LiteGLContext* ctx, int width, int height, int channels, const void* data, LiteGLFilter filter, LiteGLWrap wrap) {
    if (!ctx || width <= 0 || height <= 0) return NULL;

    LiteGLTexture* tex = (LiteGLTexture*)calloc(1, sizeof(LiteGLTexture));
    if (!tex) return NULL;

    tex->width = width;
    tex->height = height;
    tex->channels = channels;

    ctx->gl.glGenTextures(1, &tex->id);
    ctx->gl.glBindTexture(GL_TEXTURE_2D, tex->id);

    GLenum format = (channels == 4) ? GL_RGBA : (channels == 3 ? GL_RGB : GL_RED);
    GLint internal_fmt = (channels == 4) ? GL_RGBA : (channels == 3 ? GL_RGB : GL_RED);

    ctx->gl.glTexImage2D(GL_TEXTURE_2D, 0, internal_fmt, width, height, 0, format, GL_UNSIGNED_BYTE, data);

    GLint min_mag = (filter == LGL_TEX_FILTER_LINEAR) ? GL_LINEAR : GL_NEAREST;
    ctx->gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_mag);
    ctx->gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, min_mag);

    GLint wrap_mode = (wrap == LGL_TEX_WRAP_CLAMP) ? GL_CLAMP_TO_EDGE : GL_REPEAT;
    ctx->gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_mode);
    ctx->gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_mode);

    /* Restore cached binding */
    ctx->gl.glBindTexture(GL_TEXTURE_2D, ctx->state.bound_textures_2d[ctx->state.active_texture_unit]);

    return tex;
}

void litegl_destroy_texture(LiteGLContext* ctx, LiteGLTexture* tex) {
    if (!ctx || !tex) return;
    if (tex->id) {
        ctx->gl.glDeleteTextures(1, &tex->id);
    }
    free(tex);
}

void litegl_bind_texture(LiteGLContext* ctx, uint32_t stage, LiteGLTexture* tex) {
    if (!ctx || stage >= LITEGL_MAX_TEXTURE_STAGES) return;
    ctx->stats.texture_binds_requested++;

    GLuint id = tex ? tex->id : 0;

    if (ctx->config.enable_state_caching) {
        if (ctx->state.bound_textures_2d[stage] == id && ctx->state.active_texture_unit == stage) {
            ctx->stats.texture_binds_filtered++;
            return;
        }
    }

    if (ctx->state.active_texture_unit != stage && ctx->gl.glActiveTexture) {
        ctx->gl.glActiveTexture(GL_TEXTURE0 + stage);
        ctx->state.active_texture_unit = stage;
    }

    ctx->state.bound_textures_2d[stage] = id;
    ctx->gl.glBindTexture(GL_TEXTURE_2D, id);
}

LiteGLShader* litegl_create_shader(LiteGLContext* ctx, const char* vertex_src, const char* fragment_src) {
    if (!ctx || !vertex_src || !fragment_src) return NULL;

    LiteGLShader* shader = (LiteGLShader*)calloc(1, sizeof(LiteGLShader));
    if (!shader) return NULL;

    GLint success;
    char info_log[1024];

    /* Vertex Shader */
    shader->vert_shader = ctx->gl.glCreateShader(GL_VERTEX_SHADER);
    ctx->gl.glShaderSource(shader->vert_shader, 1, &vertex_src, NULL);
    ctx->gl.glCompileShader(shader->vert_shader);
    ctx->gl.glGetShaderiv(shader->vert_shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        ctx->gl.glGetShaderInfoLog(shader->vert_shader, sizeof(info_log), NULL, info_log);
        fprintf(stderr, "[LiteGL] Vertex Shader Compilation Failed: %s\n", info_log);
        litegl_destroy_shader(ctx, shader);
        return NULL;
    }

    /* Fragment Shader */
    shader->frag_shader = ctx->gl.glCreateShader(GL_FRAGMENT_SHADER);
    ctx->gl.glShaderSource(shader->frag_shader, 1, &fragment_src, NULL);
    ctx->gl.glCompileShader(shader->frag_shader);
    ctx->gl.glGetShaderiv(shader->frag_shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        ctx->gl.glGetShaderInfoLog(shader->frag_shader, sizeof(info_log), NULL, info_log);
        fprintf(stderr, "[LiteGL] Fragment Shader Compilation Failed: %s\n", info_log);
        litegl_destroy_shader(ctx, shader);
        return NULL;
    }

    /* Program */
    shader->program = ctx->gl.glCreateProgram();
    ctx->gl.glAttachShader(shader->program, shader->vert_shader);
    ctx->gl.glAttachShader(shader->program, shader->frag_shader);
    ctx->gl.glLinkProgram(shader->program);
    ctx->gl.glGetProgramiv(shader->program, GL_LINK_STATUS, &success);
    if (!success) {
        ctx->gl.glGetProgramInfoLog(shader->program, sizeof(info_log), NULL, info_log);
        fprintf(stderr, "[LiteGL] Shader Link Failed: %s\n", info_log);
        litegl_destroy_shader(ctx, shader);
        return NULL;
    }

    return shader;
}

void litegl_destroy_shader(LiteGLContext* ctx, LiteGLShader* shader) {
    if (!ctx || !shader) return;
    if (shader->program) {
        if (shader->vert_shader) ctx->gl.glDetachShader(shader->program, shader->vert_shader);
        if (shader->frag_shader) ctx->gl.glDetachShader(shader->program, shader->frag_shader);
        ctx->gl.glDeleteProgram(shader->program);
    }
    if (shader->vert_shader) ctx->gl.glDeleteShader(shader->vert_shader);
    if (shader->frag_shader) ctx->gl.glDeleteShader(shader->frag_shader);
    free(shader);
}

void litegl_bind_shader(LiteGLContext* ctx, LiteGLShader* shader) {
    if (!ctx) return;
    ctx->stats.program_binds_requested++;

    GLuint id = shader ? shader->program : 0;
    if (ctx->config.enable_state_caching && ctx->state.active_program == id) {
        ctx->stats.program_binds_filtered++;
        return;
    }

    ctx->state.active_program = id;
    ctx->gl.glUseProgram(id);
}

int litegl_get_uniform_location(LiteGLShader* shader, const char* name) {
    if (!shader || !name) return -1;
    /* In OpenGL glGetUniformLocation is queried directly */
    return -1; /* Will query via ctx */
}

void litegl_set_uniform_1f(LiteGLContext* ctx, int location, float v) {
    if (!ctx || location < 0) return;
    ctx->gl.glUniform1f(location, v);
}

void litegl_set_uniform_2f(LiteGLContext* ctx, int location, float x, float y) {
    if (!ctx || location < 0) return;
    ctx->gl.glUniform2f(location, x, y);
}

void litegl_set_uniform_4f(LiteGLContext* ctx, int location, float x, float y, float z, float w) {
    if (!ctx || location < 0) return;
    ctx->gl.glUniform4f(location, x, y, z, w);
}

void litegl_set_uniform_mat4(LiteGLContext* ctx, int location, const float* mat4) {
    if (!ctx || location < 0 || !mat4) return;
    ctx->gl.glUniformMatrix4fv(location, 1, GL_FALSE, mat4);
}

LiteGLBuffer* litegl_create_buffer(LiteGLContext* ctx, LiteGLBufferType type, LiteGLBufferUsage usage, size_t size, const void* initial_data) {
    if (!ctx || size == 0) return NULL;

    LiteGLBuffer* buf = (LiteGLBuffer*)calloc(1, sizeof(LiteGLBuffer));
    if (!buf) return NULL;

    buf->gl_target = (type == LGL_BUFFER_VERTEX) ? GL_ARRAY_BUFFER :
                     (type == LGL_BUFFER_INDEX ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER);
    buf->gl_usage  = (usage == LGL_BUFFER_STATIC) ? GL_STATIC_DRAW :
                     (usage == LGL_BUFFER_DYNAMIC ? GL_DYNAMIC_DRAW : GL_STREAM_DRAW);
    buf->size      = size;

    ctx->gl.glGenBuffers(1, &buf->gl_id);
    ctx->gl.glBindBuffer(buf->gl_target, buf->gl_id);
    ctx->gl.glBufferData(buf->gl_target, (GLsizeiptr)size, initial_data, buf->gl_usage);

    return buf;
}

void litegl_destroy_buffer(LiteGLContext* ctx, LiteGLBuffer* buf) {
    if (!ctx || !buf) return;
    if (buf->gl_id) {
        ctx->gl.glDeleteBuffers(1, &buf->gl_id);
    }
    free(buf);
}

void litegl_update_buffer(LiteGLContext* ctx, LiteGLBuffer* buf, size_t offset, size_t size, const void* data, bool discard) {
    if (!ctx || !buf || !data || size == 0) return;

    ctx->gl.glBindBuffer(buf->gl_target, buf->gl_id);
    if (discard) {
        /* Direct3D 9 DISCARD semantics via buffer orphaning */
        ctx->gl.glBufferData(buf->gl_target, (GLsizeiptr)buf->size, NULL, buf->gl_usage);
    }
    ctx->gl.glBufferSubData(buf->gl_target, (GLintptr)offset, (GLsizeiptr)size, data);
    ctx->stats.bytes_uploaded += size;
}

void litegl_bind_buffer(LiteGLContext* ctx, LiteGLBuffer* buf) {
    if (!ctx || !buf) return;
    ctx->stats.buffer_binds_requested++;

    if (buf->gl_target == GL_ARRAY_BUFFER) {
        if (ctx->config.enable_state_caching && ctx->state.bound_array_buffer == buf->gl_id) {
            ctx->stats.buffer_binds_filtered++;
            return;
        }
        ctx->state.bound_array_buffer = buf->gl_id;
        ctx->gl.glBindBuffer(GL_ARRAY_BUFFER, buf->gl_id);
    } else if (buf->gl_target == GL_ELEMENT_ARRAY_BUFFER) {
        if (ctx->config.enable_state_caching && ctx->state.bound_element_buffer == buf->gl_id) {
            ctx->stats.buffer_binds_filtered++;
            return;
        }
        ctx->state.bound_element_buffer = buf->gl_id;
        ctx->gl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf->gl_id);
    }
}

void* litegl_stream_alloc_vertices(LiteGLContext* ctx, size_t size, uint32_t* out_byte_offset) {
    if (!ctx) return NULL;
    bool orphaned = false;
    void* ptr = litegl_ring_buffer_alloc(&ctx->gl, &ctx->vbo_ring, size, 64, out_byte_offset, &orphaned);
    if (orphaned) {
        ctx->stats.dynamic_buffer_flushes++;
    }
    ctx->stats.bytes_uploaded += size;
    return ptr;
}

void* litegl_stream_alloc_indices(LiteGLContext* ctx, size_t size, uint32_t* out_byte_offset) {
    if (!ctx) return NULL;
    bool orphaned = false;
    void* ptr = litegl_ring_buffer_alloc(&ctx->gl, &ctx->ibo_ring, size, 16, out_byte_offset, &orphaned);
    if (orphaned) {
        ctx->stats.dynamic_buffer_flushes++;
    }
    ctx->stats.bytes_uploaded += size;
    return ptr;
}

void litegl_stream_upload_vertices(LiteGLContext* ctx, const void* data, size_t size, uint32_t* out_byte_offset) {
    if (!ctx || !data || size == 0) return;
    bool orphaned = false;
    uint32_t offset = 0;
    litegl_ring_buffer_alloc(&ctx->gl, &ctx->vbo_ring, size, 64, &offset, &orphaned);
    if (orphaned) {
        ctx->stats.dynamic_buffer_flushes++;
    }
    ctx->stats.bytes_uploaded += size;

    if (ctx->state.bound_array_buffer != ctx->vbo_ring.gl_id) {
        ctx->state.bound_array_buffer = ctx->vbo_ring.gl_id;
        ctx->gl.glBindBuffer(GL_ARRAY_BUFFER, ctx->vbo_ring.gl_id);
    }
    ctx->gl.glBufferSubData(GL_ARRAY_BUFFER, (GLintptr)offset, (GLsizeiptr)size, data);

    if (out_byte_offset) *out_byte_offset = offset;
}

void litegl_stream_upload_indices(LiteGLContext* ctx, const void* data, size_t size, uint32_t* out_byte_offset) {
    if (!ctx || !data || size == 0) return;
    bool orphaned = false;
    uint32_t offset = 0;
    litegl_ring_buffer_alloc(&ctx->gl, &ctx->ibo_ring, size, 16, &offset, &orphaned);
    if (orphaned) {
        ctx->stats.dynamic_buffer_flushes++;
    }
    ctx->stats.bytes_uploaded += size;

    if (ctx->state.bound_element_buffer != ctx->ibo_ring.gl_id) {
        ctx->state.bound_element_buffer = ctx->ibo_ring.gl_id;
        ctx->gl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ctx->ibo_ring.gl_id);
    }
    ctx->gl.glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, (GLintptr)offset, (GLsizeiptr)size, data);

    if (out_byte_offset) *out_byte_offset = offset;
}

void litegl_enable_vertex_attrib_array(LiteGLContext* ctx, uint32_t index) {
    if (!ctx || !ctx->gl.glEnableVertexAttribArray) return;
    ctx->gl.glEnableVertexAttribArray(index);
}

void litegl_disable_vertex_attrib_array(LiteGLContext* ctx, uint32_t index) {
    if (!ctx || !ctx->gl.glDisableVertexAttribArray) return;
    ctx->gl.glDisableVertexAttribArray(index);
}

void litegl_vertex_attrib_pointer(LiteGLContext* ctx, uint32_t index, int size, uint32_t type, bool normalized, uint32_t stride, const void* pointer) {
    if (!ctx || !ctx->gl.glVertexAttribPointer) return;
    ctx->gl.glVertexAttribPointer(index, size, type, normalized ? GL_TRUE : GL_FALSE, stride, pointer);
}

static GLenum map_primitive(LiteGLPrimitiveType prim) {
    switch (prim) {
        case LGL_PRIM_TRIANGLES:      return GL_TRIANGLES;
        case LGL_PRIM_TRIANGLE_STRIP: return GL_TRIANGLE_STRIP;
        case LGL_PRIM_LINES:          return GL_LINES;
        case LGL_PRIM_POINTS:         return GL_POINTS;
        default:                      return GL_TRIANGLES;
    }
}

void litegl_draw_arrays(LiteGLContext* ctx, LiteGLPrimitiveType prim, uint32_t first, uint32_t count) {
    if (!ctx || count == 0) return;
    ctx->stats.draw_calls++;
    ctx->gl.glDrawArrays(map_primitive(prim), (GLint)first, (GLsizei)count);
}

void litegl_draw_elements(LiteGLContext* ctx, LiteGLPrimitiveType prim, uint32_t count, uint32_t index_type, const void* indices) {
    if (!ctx || count == 0) return;
    ctx->stats.draw_calls++;
    ctx->gl.glDrawElements(map_primitive(prim), (GLsizei)count, index_type, indices);
}

void litegl_flush(LiteGLContext* ctx) {
    if (!ctx) return;
    /* Flush any pending staging transfers */
    if (ctx->vbo_ring.head_offset > 0) {
        litegl_ring_buffer_flush(&ctx->gl, &ctx->vbo_ring, 0, ctx->vbo_ring.head_offset);
    }
    if (ctx->ibo_ring.head_offset > 0) {
        litegl_ring_buffer_flush(&ctx->gl, &ctx->ibo_ring, 0, ctx->ibo_ring.head_offset);
    }
}

void litegl_get_stats(LiteGLContext* ctx, LiteGLStats* out_stats) {
    if (!ctx || !out_stats) return;
    *out_stats = ctx->stats;
}

void litegl_reset_stats(LiteGLContext* ctx) {
    if (!ctx) return;
    memset(&ctx->stats, 0, sizeof(LiteGLStats));
}
