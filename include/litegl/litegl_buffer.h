#ifndef LITEGL_BUFFER_H
#define LITEGL_BUFFER_H

#include "litegl.h"
#include "litegl_gl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Dynamic Ring Buffer implementation mimicking ToGL dynamic scratch VBO */
typedef struct {
    GLuint   gl_id;
    GLenum   gl_target;      /* GL_ARRAY_BUFFER or GL_ELEMENT_ARRAY_BUFFER */
    size_t   capacity;
    size_t   head_offset;    /* Current write cursor in bytes */
    uint8_t* mapped_memory;  /* Persistent or staging pointer */
    uint8_t* cpu_scratch;    /* CPU fallback scratch buffer if mapping unsupported */
} LiteGLRingBuffer;

bool litegl_ring_buffer_init(LiteGLDispatch* gl, LiteGLRingBuffer* ring, GLenum target, size_t capacity);
void litegl_ring_buffer_destroy(LiteGLDispatch* gl, LiteGLRingBuffer* ring);

/* Allocate space in the ring buffer. If remaining capacity is insufficient, wraps around and orphans buffer */
void* litegl_ring_buffer_alloc(LiteGLDispatch* gl, LiteGLRingBuffer* ring, size_t size, size_t alignment, uint32_t* out_byte_offset, bool* out_orphaned);
void litegl_ring_buffer_flush(LiteGLDispatch* gl, LiteGLRingBuffer* ring, size_t offset, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* LITEGL_BUFFER_H */
