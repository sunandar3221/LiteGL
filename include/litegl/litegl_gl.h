#ifndef LITEGL_GL_H
#define LITEGL_GL_H

#include <stdint.h>
#include <stddef.h>
#include "litegl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* OpenGL basic types */
typedef unsigned int   GLenum;
typedef unsigned char  GLboolean;
typedef unsigned int   GLbitfield;
typedef void           GLvoid;
typedef signed char    GLbyte;
typedef short          GLshort;
typedef int            GLint;
typedef int            GLsizei;
typedef unsigned char  GLubyte;
typedef unsigned short GLushort;
typedef unsigned int   GLuint;
typedef float          GLfloat;
typedef float          GLclampf;
typedef double         GLdouble;
typedef double         GLclampd;
typedef char           GLchar;
typedef ptrdiff_t      GLintptr;
typedef ptrdiff_t      GLsizeiptr;

/* GL Constants */
#define GL_FALSE                          0
#define GL_TRUE                           1

#define GL_TRIANGLES                      0x0004
#define GL_TRIANGLE_STRIP                 0x0005
#define GL_LINES                          0x0001
#define GL_POINTS                         0x0000

#define GL_BYTE                           0x1400
#define GL_UNSIGNED_BYTE                  0x1401
#define GL_SHORT                          0x1402
#define GL_UNSIGNED_SHORT                 0x1403
#define GL_INT                            0x1404
#define GL_UNSIGNED_INT                   0x1405
#define GL_FLOAT                          0x1406

#define GL_DEPTH_BUFFER_BIT               0x00000100
#define GL_STENCIL_BUFFER_BIT             0x00000400
#define GL_COLOR_BUFFER_BIT               0x00004000

#define GL_NEVER                          0x0200
#define GL_LESS                           0x0201
#define GL_EQUAL                          0x0202
#define GL_LEQUAL                         0x0203
#define GL_GREATER                        0x0204
#define GL_NOTEQUAL                       0x0205
#define GL_GEQUAL                         0x0206
#define GL_ALWAYS                         0x0207

#define GL_ZERO                           0
#define GL_ONE                            1
#define GL_SRC_COLOR                      0x0300
#define GL_ONE_MINUS_SRC_COLOR            0x0301
#define GL_SRC_ALPHA                      0x0302
#define GL_ONE_MINUS_SRC_ALPHA            0x0303
#define GL_DST_ALPHA                      0x0304
#define GL_ONE_MINUS_DST_ALPHA            0x0305
#define GL_DST_COLOR                      0x0306
#define GL_ONE_MINUS_DST_COLOR            0x0307

#define GL_FRONT                          0x0404
#define GL_BACK                           0x0405
#define GL_FRONT_AND_BACK                 0x0408
#define GL_CW                             0x0900
#define GL_CCW                            0x0901

#define GL_CULL_FACE                      0x0B44
#define GL_DEPTH_TEST                     0x0B71
#define GL_BLEND                          0x0BE2
#define GL_SCISSOR_TEST                   0x0C11

#define GL_TEXTURE_2D                     0x0DE1
#define GL_TEXTURE_MIN_FILTER             0x2801
#define GL_TEXTURE_MAG_FILTER             0x2800
#define GL_TEXTURE_WRAP_S                 0x2802
#define GL_TEXTURE_WRAP_T                 0x2803
#define GL_REPEAT                         0x2901
#define GL_CLAMP_TO_EDGE                  0x812F
#define GL_NEAREST                        0x2600
#define GL_LINEAR                         0x2601

#define GL_RGB                            0x1907
#define GL_RGBA                           0x1908
#define GL_RED                            0x1903

#define GL_ARRAY_BUFFER                   0x8892
#define GL_ELEMENT_ARRAY_BUFFER           0x8893
#define GL_UNIFORM_BUFFER                 0x8A11

#define GL_STATIC_DRAW                    0x88E4
#define GL_DYNAMIC_DRAW                   0x88E8
#define GL_STREAM_DRAW                    0x88E0

#define GL_FRAGMENT_SHADER                0x8B30
#define GL_VERTEX_SHADER                  0x8B31
#define GL_COMPILE_STATUS                 0x8B81
#define GL_LINK_STATUS                    0x8B82
#define GL_INFO_LOG_LENGTH                0x8B84

#define GL_TEXTURE0                       0x84C0

/* Buffer Mapping bitfields */
#define GL_MAP_READ_BIT                   0x0001
#define GL_MAP_WRITE_BIT                  0x0002
#define GL_MAP_INVALIDATE_RANGE_BIT       0x0004
#define GL_MAP_INVALIDATE_BUFFER_BIT      0x0008
#define GL_MAP_FLUSH_EXPLICIT_BIT         0x0010
#define GL_MAP_UNSYNCHRONIZED_BIT         0x0020

/* GL Function Pointers Dispatch Table */
typedef struct LiteGLDispatch {
    /* State */
    void (*glEnable)(GLenum cap);
    void (*glDisable)(GLenum cap);
    void (*glDepthFunc)(GLenum func);
    void (*glDepthMask)(GLboolean flag);
    void (*glBlendFunc)(GLenum sfactor, GLenum dfactor);
    void (*glCullFace)(GLenum mode);
    void (*glFrontFace)(GLenum mode);
    void (*glColorMask)(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
    void (*glViewport)(GLint x, GLint y, GLsizei width, GLsizei height);
    void (*glScissor)(GLint x, GLint y, GLsizei width, GLsizei height);
    void (*glClearColor)(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
    void (*glClearDepth)(GLdouble depth);
    void (*glClear)(GLbitfield mask);

    /* Textures */
    void (*glGenTextures)(GLsizei n, GLuint *textures);
    void (*glDeleteTextures)(GLsizei n, const GLuint *textures);
    void (*glBindTexture)(GLenum target, GLuint texture);
    void (*glActiveTexture)(GLenum texture);
    void (*glTexImage2D)(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels);
    void (*glTexParameteri)(GLenum target, GLenum pname, GLint param);

    /* Shaders */
    GLuint (*glCreateShader)(GLenum type);
    void (*glDeleteShader)(GLuint shader);
    void (*glShaderSource)(GLuint shader, GLsizei count, const GLchar *const*string, const GLint *length);
    void (*glCompileShader)(GLuint shader);
    void (*glGetShaderiv)(GLuint shader, GLenum pname, GLint *params);
    void (*glGetShaderInfoLog)(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog);

    GLuint (*glCreateProgram)(void);
    void (*glDeleteProgram)(GLuint program);
    void (*glAttachShader)(GLuint program, GLuint shader);
    void (*glDetachShader)(GLuint program, GLuint shader);
    void (*glLinkProgram)(GLuint program);
    void (*glGetProgramiv)(GLuint program, GLenum pname, GLint *params);
    void (*glGetProgramInfoLog)(GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
    void (*glUseProgram)(GLuint program);

    /* Uniforms */
    GLint (*glGetUniformLocation)(GLuint program, const GLchar *name);
    void (*glUniform1f)(GLint location, GLfloat v0);
    void (*glUniform2f)(GLint location, GLfloat v0, GLfloat v1);
    void (*glUniform4f)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
    void (*glUniformMatrix4fv)(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);

    /* Buffers */
    void (*glGenBuffers)(GLsizei n, GLuint *buffers);
    void (*glDeleteBuffers)(GLsizei n, const GLuint *buffers);
    void (*glBindBuffer)(GLenum target, GLuint buffer);
    void (*glBufferData)(GLenum target, GLsizeiptr size, const void *data, GLenum usage);
    void (*glBufferSubData)(GLenum target, GLintptr offset, GLsizeiptr size, const void *data);
    void *(*glMapBufferRange)(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access);
    GLboolean (*glUnmapBuffer)(GLenum target);

    /* VAO */
    void (*glGenVertexArrays)(GLsizei n, GLuint *arrays);
    void (*glDeleteVertexArrays)(GLsizei n, const GLuint *arrays);
    void (*glBindVertexArray)(GLuint array);
    void (*glEnableVertexAttribArray)(GLuint index);
    void (*glDisableVertexAttribArray)(GLuint index);
    void (*glVertexAttribPointer)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);

    /* Draw */
    void (*glDrawArrays)(GLenum mode, GLint first, GLsizei count);
    void (*glDrawElements)(GLenum mode, GLsizei count, GLenum type, const void *indices);

    /* Optional debug / sync */
    void (*glFinish)(void);
    GLenum (*glGetError)(void);
} LiteGLDispatch;

/* Function to populate dispatch table given a proc address loader */
bool litegl_load_gl(LiteGLDispatch* dispatch, LiteGLProcAddressFunc proc_loader);

#ifdef __cplusplus
}
#endif

#endif /* LITEGL_GL_H */
