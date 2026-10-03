#include "litegl/litegl.h"
#include "litegl/litegl_gl.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

static int s_mock_glEnable_count = 0;
static int s_mock_glDisable_count = 0;
static int s_mock_glBlendFunc_count = 0;
static int s_mock_glUseProgram_count = 0;

static void mock_glEnable(GLenum cap) { (void)cap; s_mock_glEnable_count++; }
static void mock_glDisable(GLenum cap) { (void)cap; s_mock_glDisable_count++; }
static void mock_glBlendFunc(GLenum s, GLenum d) { (void)s; (void)d; s_mock_glBlendFunc_count++; }
static void mock_glUseProgram(GLuint p) { (void)p; s_mock_glUseProgram_count++; }
static void mock_glClear(GLbitfield mask) { (void)mask; }
static void mock_glDrawArrays(GLenum mode, GLint first, GLsizei count) { (void)mode; (void)first; (void)count; }
static void mock_glGenBuffers(GLsizei n, GLuint* b) { for (int i = 0; i < n; i++) b[i] = 100 + i; }
static void mock_glDeleteBuffers(GLsizei n, const GLuint* b) { (void)n; (void)b; }
static void mock_glBindBuffer(GLenum t, GLuint b) { (void)t; (void)b; }
static void mock_glBufferData(GLenum t, GLsizeiptr s, const void* d, GLenum u) { (void)t; (void)s; (void)d; (void)u; }

static void mock_glDepthMask(GLboolean f) { (void)f; }
static void mock_glDepthFunc(GLenum f) { (void)f; }
static void mock_glCullFace(GLenum m) { (void)m; }
static void mock_glFrontFace(GLenum m) { (void)m; }
static void mock_glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) { (void)r; (void)g; (void)b; (void)a; }

static void* mock_proc_loader(const char* name) {
    if (strcmp(name, "glEnable") == 0) return (void*)mock_glEnable;
    if (strcmp(name, "glDisable") == 0) return (void*)mock_glDisable;
    if (strcmp(name, "glBlendFunc") == 0) return (void*)mock_glBlendFunc;
    if (strcmp(name, "glUseProgram") == 0) return (void*)mock_glUseProgram;
    if (strcmp(name, "glClear") == 0) return (void*)mock_glClear;
    if (strcmp(name, "glDrawArrays") == 0) return (void*)mock_glDrawArrays;
    if (strcmp(name, "glGenBuffers") == 0) return (void*)mock_glGenBuffers;
    if (strcmp(name, "glDeleteBuffers") == 0) return (void*)mock_glDeleteBuffers;
    if (strcmp(name, "glBindBuffer") == 0) return (void*)mock_glBindBuffer;
    if (strcmp(name, "glBufferData") == 0) return (void*)mock_glBufferData;
    if (strcmp(name, "glDepthMask") == 0) return (void*)mock_glDepthMask;
    if (strcmp(name, "glDepthFunc") == 0) return (void*)mock_glDepthFunc;
    if (strcmp(name, "glCullFace") == 0) return (void*)mock_glCullFace;
    if (strcmp(name, "glFrontFace") == 0) return (void*)mock_glFrontFace;
    if (strcmp(name, "glColorMask") == 0) return (void*)mock_glColorMask;
    return NULL;
}

int main(void) {
    printf("[Test] Running LiteGL State Shadowing & Redundant Call Filter Unit Test...\n");

    LiteGLConfig cfg = litegl_default_config(mock_proc_loader);
    cfg.enable_state_caching = true;
    LiteGLContext* ctx = litegl_create_context(&cfg);
    assert(ctx != NULL);

    /* Test 1: Enable blend twice */
    s_mock_glEnable_count = 0;
    litegl_set_render_state(ctx, LGL_RS_ALPHABLENDENABLE, 1);
    assert(s_mock_glEnable_count == 1);

    /* Second identical call must be filtered by ToGL shadow cache */
    litegl_set_render_state(ctx, LGL_RS_ALPHABLENDENABLE, 1);
    assert(s_mock_glEnable_count == 1); /* Count shouldn't change! */

    /* Third identical call */
    litegl_set_render_state(ctx, LGL_RS_ALPHABLENDENABLE, 1);
    assert(s_mock_glEnable_count == 1);

    /* Test 2: Disable blend */
    s_mock_glDisable_count = 0;
    litegl_set_render_state(ctx, LGL_RS_ALPHABLENDENABLE, 0);
    assert(s_mock_glDisable_count == 1);

    /* Repeated disable */
    litegl_set_render_state(ctx, LGL_RS_ALPHABLENDENABLE, 0);
    assert(s_mock_glDisable_count == 1);

    /* Check stats */
    LiteGLStats stats;
    litegl_get_stats(ctx, &stats);
    printf("[Test] State changes requested: %lu | filtered: %lu\n",
           (unsigned long)stats.state_changes_requested,
           (unsigned long)stats.state_changes_filtered);
    assert(stats.state_changes_requested == 5);
    assert(stats.state_changes_filtered == 3);

    litegl_destroy_context(ctx);
    printf("[Test] ALL TESTS PASSED SUCCESSFULLY! ToGL state caching verified.\n");
    return 0;
}
