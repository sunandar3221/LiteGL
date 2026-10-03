#include "litegl/litegl_gl.h"
#include <string.h>
#include <stdio.h>

#define LOAD_PROC(fn, type) \
    do { \
        dispatch->fn = (type)proc_loader(#fn); \
        if (!dispatch->fn) { \
            /* fprintf(stderr, "[LiteGL] Notice: symbol %s not found\n", #fn); */ \
        } \
    } while (0)

bool litegl_load_gl(LiteGLDispatch* dispatch, LiteGLProcAddressFunc proc_loader) {
    if (!dispatch || !proc_loader) return false;
    memset(dispatch, 0, sizeof(LiteGLDispatch));

    /* Core GL State */
    LOAD_PROC(glEnable, void (*)(GLenum));
    LOAD_PROC(glDisable, void (*)(GLenum));
    LOAD_PROC(glDepthFunc, void (*)(GLenum));
    LOAD_PROC(glDepthMask, void (*)(GLboolean));
    LOAD_PROC(glBlendFunc, void (*)(GLenum, GLenum));
    LOAD_PROC(glCullFace, void (*)(GLenum));
    LOAD_PROC(glFrontFace, void (*)(GLenum));
    LOAD_PROC(glColorMask, void (*)(GLboolean, GLboolean, GLboolean, GLboolean));
    LOAD_PROC(glViewport, void (*)(GLint, GLint, GLsizei, GLsizei));
    LOAD_PROC(glScissor, void (*)(GLint, GLint, GLsizei, GLsizei));
    LOAD_PROC(glClearColor, void (*)(GLfloat, GLfloat, GLfloat, GLfloat));
    LOAD_PROC(glClearDepth, void (*)(GLdouble));
    LOAD_PROC(glClear, void (*)(GLbitfield));

    /* Textures */
    LOAD_PROC(glGenTextures, void (*)(GLsizei, GLuint *));
    LOAD_PROC(glDeleteTextures, void (*)(GLsizei, const GLuint *));
    LOAD_PROC(glBindTexture, void (*)(GLenum, GLuint));
    LOAD_PROC(glActiveTexture, void (*)(GLenum));
    LOAD_PROC(glTexImage2D, void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *));
    LOAD_PROC(glTexParameteri, void (*)(GLenum, GLenum, GLint));

    /* Shaders */
    LOAD_PROC(glCreateShader, GLuint (*)(GLenum));
    LOAD_PROC(glDeleteShader, void (*)(GLuint));
    LOAD_PROC(glShaderSource, void (*)(GLuint, GLsizei, const GLchar *const*, const GLint *));
    LOAD_PROC(glCompileShader, void (*)(GLuint));
    LOAD_PROC(glGetShaderiv, void (*)(GLuint, GLenum, GLint *));
    LOAD_PROC(glGetShaderInfoLog, void (*)(GLuint, GLsizei, GLsizei *, GLchar *));
    LOAD_PROC(glCreateProgram, GLuint (*)(void));
    LOAD_PROC(glDeleteProgram, void (*)(GLuint));
    LOAD_PROC(glAttachShader, void (*)(GLuint, GLuint));
    LOAD_PROC(glDetachShader, void (*)(GLuint, GLuint));
    LOAD_PROC(glLinkProgram, void (*)(GLuint));
    LOAD_PROC(glGetProgramiv, void (*)(GLuint, GLenum, GLint *));
    LOAD_PROC(glGetProgramInfoLog, void (*)(GLuint, GLsizei, GLsizei *, GLchar *));
    LOAD_PROC(glUseProgram, void (*)(GLuint));

    /* Uniforms */
    LOAD_PROC(glGetUniformLocation, GLint (*)(GLuint, const GLchar *));
    LOAD_PROC(glUniform1f, void (*)(GLint, GLfloat));
    LOAD_PROC(glUniform2f, void (*)(GLint, GLfloat, GLfloat));
    LOAD_PROC(glUniform4f, void (*)(GLint, GLfloat, GLfloat, GLfloat, GLfloat));
    LOAD_PROC(glUniformMatrix4fv, void (*)(GLint, GLsizei, GLboolean, const GLfloat *));

    /* Buffers */
    LOAD_PROC(glGenBuffers, void (*)(GLsizei, GLuint *));
    LOAD_PROC(glDeleteBuffers, void (*)(GLsizei, const GLuint *));
    LOAD_PROC(glBindBuffer, void (*)(GLenum, GLuint));
    LOAD_PROC(glBufferData, void (*)(GLenum, GLsizeiptr, const void *, GLenum));
    LOAD_PROC(glBufferSubData, void (*)(GLenum, GLintptr, GLsizeiptr, const void *));
    LOAD_PROC(glMapBufferRange, void *(*)(GLenum, GLintptr, GLsizeiptr, GLbitfield));
    LOAD_PROC(glUnmapBuffer, GLboolean (*)(GLenum));

    /* Vertex Array Objects */
    LOAD_PROC(glGenVertexArrays, void (*)(GLsizei, GLuint *));
    LOAD_PROC(glDeleteVertexArrays, void (*)(GLsizei, const GLuint *));
    LOAD_PROC(glBindVertexArray, void (*)(GLuint));
    LOAD_PROC(glEnableVertexAttribArray, void (*)(GLuint));
    LOAD_PROC(glDisableVertexAttribArray, void (*)(GLuint));
    LOAD_PROC(glVertexAttribPointer, void (*)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *));

    /* Draw */
    LOAD_PROC(glDrawArrays, void (*)(GLenum, GLint, GLsizei));
    LOAD_PROC(glDrawElements, void (*)(GLenum, GLsizei, GLenum, const void *));

    /* Debug */
    LOAD_PROC(glFinish, void (*)(void));
    LOAD_PROC(glGetError, GLenum (*)(void));

    return (dispatch->glClear != NULL && dispatch->glDrawArrays != NULL);
}
