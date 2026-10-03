#include "litegl/litegl_buffer.h"
#include <stdlib.h>
#include <string.h>

bool litegl_ring_buffer_init(LiteGLDispatch* gl, LiteGLRingBuffer* ring, GLenum target, size_t capacity) {
    if (!gl || !ring || capacity == 0) return false;

    ring->gl_target = target;
    ring->capacity = capacity;
    ring->head_offset = 0;
    ring->mapped_memory = NULL;

    ring->cpu_scratch = (uint8_t*)malloc(capacity);
    if (!ring->cpu_scratch) return false;

    gl->glGenBuffers(1, &ring->gl_id);
    if (ring->gl_id == 0) {
        free(ring->cpu_scratch);
        ring->cpu_scratch = NULL;
        return false;
    }

    gl->glBindBuffer(target, ring->gl_id);
    gl->glBufferData(target, (GLsizeiptr)capacity, NULL, GL_STREAM_DRAW);
    gl->glBindBuffer(target, 0);

    return true;
}

void litegl_ring_buffer_destroy(LiteGLDispatch* gl, LiteGLRingBuffer* ring) {
    if (!ring) return;

    if (ring->gl_id && gl) {
        gl->glDeleteBuffers(1, &ring->gl_id);
        ring->gl_id = 0;
    }

    if (ring->cpu_scratch) {
        free(ring->cpu_scratch);
        ring->cpu_scratch = NULL;
    }

    ring->capacity = 0;
    ring->head_offset = 0;
}

void* litegl_ring_buffer_alloc(LiteGLDispatch* gl, LiteGLRingBuffer* ring, size_t size, size_t alignment, uint32_t* out_byte_offset, bool* out_orphaned) {
    if (!gl || !ring || size == 0 || size > ring->capacity) return NULL;

    if (alignment == 0) alignment = 16;
    size_t aligned_head = (ring->head_offset + (alignment - 1)) & ~(alignment - 1);
    bool orphaned = false;

    if (aligned_head + size > ring->capacity) {
        /* Ring buffer wrap-around: ToGL Discard / Orphan semantics */
        aligned_head = 0;
        orphaned = true;

        /* Re-allocate buffer storage on GPU to sever synchronization with previous in-flight commands */
        gl->glBindBuffer(ring->gl_target, ring->gl_id);
        gl->glBufferData(ring->gl_target, (GLsizeiptr)ring->capacity, NULL, GL_STREAM_DRAW);
    }

    *out_byte_offset = (uint32_t)aligned_head;
    if (out_orphaned) *out_orphaned = orphaned;

    ring->head_offset = aligned_head + size;
    return ring->cpu_scratch + aligned_head;
}

void litegl_ring_buffer_flush(LiteGLDispatch* gl, LiteGLRingBuffer* ring, size_t offset, size_t size) {
    if (!gl || !ring || size == 0) return;

    gl->glBindBuffer(ring->gl_target, ring->gl_id);
    gl->glBufferSubData(ring->gl_target, (GLintptr)offset, (GLsizeiptr)size, ring->cpu_scratch + offset);
}
