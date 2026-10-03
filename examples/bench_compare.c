#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <SDL2/SDL.h>

#include "litegl/litegl.h"
#include "litegl/litegl_gl.h"

#define NUM_OBJECTS 2500
#define BENCH_FRAMES 400

typedef struct {
    float x, y;
    float vx, vy;
    float size;
    float r, g, b, a;
    int   texture_id;
    int   blend_mode;
    int   depth_test;
} SceneObject;

static SceneObject s_objects[NUM_OBJECTS];

static void init_scene(void) {
    srand(42);
    for (int i = 0; i < NUM_OBJECTS; ++i) {
        s_objects[i].x = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
        s_objects[i].y = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
        s_objects[i].vx = (((float)rand() / (float)RAND_MAX) - 0.5f) * 0.01f;
        s_objects[i].vy = (((float)rand() / (float)RAND_MAX) - 0.5f) * 0.01f;
        s_objects[i].size = 0.02f + ((float)rand() / (float)RAND_MAX) * 0.03f;
        s_objects[i].r = (float)rand() / (float)RAND_MAX;
        s_objects[i].g = (float)rand() / (float)RAND_MAX;
        s_objects[i].b = (float)rand() / (float)RAND_MAX;
        s_objects[i].a = 0.5f + ((float)rand() / (float)RAND_MAX) * 0.5f;
        s_objects[i].texture_id = rand() % 4;
        s_objects[i].blend_mode = rand() % 2;
        s_objects[i].depth_test = rand() % 2;
    }
}

static void update_scene(void) {
    for (int i = 0; i < NUM_OBJECTS; ++i) {
        s_objects[i].x += s_objects[i].vx;
        s_objects[i].y += s_objects[i].vy;
        if (s_objects[i].x < -1.0f || s_objects[i].x > 1.0f) s_objects[i].vx = -s_objects[i].vx;
        if (s_objects[i].y < -1.0f || s_objects[i].y > 1.0f) s_objects[i].vy = -s_objects[i].vy;
    }
}

static const char* vertex_shader_src =
    "#version 330 core\n"
    "layout(location = 0) in vec2 inPos;\n"
    "layout(location = 1) in vec2 inTex;\n"
    "layout(location = 2) in vec4 inColor;\n"
    "out vec2 fragTex;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    fragTex = inTex;\n"
    "    fragColor = inColor;\n"
    "    gl_Position = vec4(inPos, 0.0, 1.0);\n"
    "}\n";

static const char* fragment_shader_src =
    "#version 330 core\n"
    "in vec2 fragTex;\n"
    "in vec4 fragColor;\n"
    "out vec4 outColor;\n"
    "uniform sampler2D uTex;\n"
    "void main() {\n"
    "    outColor = fragColor * texture(uTex, fragTex);\n"
    "}\n";

typedef struct {
    float x, y;
    float u, v;
    float r, g, b, a;
} Vertex2D;

static uint8_t* create_test_pattern(int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    uint8_t* p = (uint8_t*)malloc(w * h * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int idx = (y * w + x) * 4;
            bool check = ((x / 8) + (y / 8)) % 2 == 0;
            p[idx + 0] = check ? r : (r / 2);
            p[idx + 1] = check ? g : (g / 2);
            p[idx + 2] = check ? b : (b / 2);
            p[idx + 3] = 255;
        }
    }
    return p;
}

static double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

int main(int argc, char* argv[]) {
    bool bench_mode = true;
    int bench_target_frames = BENCH_FRAMES;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            bench_target_frames = atoi(argv[++i]);
        }
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "Failed to initialize SDL: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_Window* window = SDL_CreateWindow(
        "LiteGL vs Naive OpenGL Benchmark",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1024, 768,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN
    );

    if (!window) {
        fprintf(stderr, "Failed to create SDL window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext gl_ctx = SDL_GL_CreateContext(window);
    if (!gl_ctx) {
        fprintf(stderr, "Failed to create OpenGL context: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    /* Disable VSync for uncapped raw throughput benchmark */
    SDL_GL_SetSwapInterval(0);

    init_scene();

    /* Textures */
    uint8_t* tex_data[4];
    tex_data[0] = create_test_pattern(64, 64, 255, 60, 60);
    tex_data[1] = create_test_pattern(64, 64, 60, 255, 60);
    tex_data[2] = create_test_pattern(64, 64, 60, 60, 255);
    tex_data[3] = create_test_pattern(64, 64, 255, 255, 60);

    /* -------------------------------------------------------------------------
     * BENCHMARK 1: NAIVE OPENGL
     * ------------------------------------------------------------------------- */
    printf("\n===================================================================\n");
    printf(" [1/2] RUNNING BENCHMARK: NAIVE STANDARD OPENGL (Baseline)\n");
    printf(" - Objects per frame: %d\n", NUM_OBJECTS);
    printf(" - Frame target: %d frames\n", bench_target_frames);
    printf(" - Simulating un-cached game loop (redundant GL states & un-ringed buffer uploads)\n");
    printf("===================================================================\n");

    LiteGLConfig cfg_naive = litegl_default_config(SDL_GL_GetProcAddress);
    cfg_naive.enable_state_caching = false; /* Disable ToGL caching */
    LiteGLContext* lgl_naive = litegl_create_context(&cfg_naive);

    LiteGLShader* shader_naive = litegl_create_shader(lgl_naive, vertex_shader_src, fragment_shader_src);
    LiteGLTexture* textures_naive[4];
    for (int i = 0; i < 4; ++i) {
        textures_naive[i] = litegl_create_texture_2d(lgl_naive, 64, 64, 4, tex_data[i], LGL_TEX_FILTER_LINEAR, LGL_TEX_WRAP_REPEAT);
    }

    LiteGLBuffer* dynamic_vbo_naive = litegl_create_buffer(lgl_naive, LGL_BUFFER_VERTEX, LGL_BUFFER_DYNAMIC, sizeof(Vertex2D) * 6, NULL);

    double t0_naive = get_time_sec();
    for (int frame = 0; frame < bench_target_frames; ++frame) {
        update_scene();
        litegl_clear(lgl_naive, 3, 0.05f, 0.05f, 0.08f, 1.0f, 1.0f);

        for (int i = 0; i < NUM_OBJECTS; ++i) {
            SceneObject* obj = &s_objects[i];

            /* Naive OpenGL: frequent redundant program binds & state sets */
            litegl_bind_shader(lgl_naive, shader_naive);

            if (obj->blend_mode) {
                litegl_set_render_state(lgl_naive, LGL_RS_ALPHABLENDENABLE, 1);
                litegl_set_render_state(lgl_naive, LGL_RS_SRCBLEND, LGL_BLEND_SRCALPHA);
                litegl_set_render_state(lgl_naive, LGL_RS_DESTBLEND, LGL_BLEND_INVSRCALPHA);
            } else {
                litegl_set_render_state(lgl_naive, LGL_RS_ALPHABLENDENABLE, 0);
            }

            if (obj->depth_test) {
                litegl_set_render_state(lgl_naive, LGL_RS_ZENABLE, 1);
            } else {
                litegl_set_render_state(lgl_naive, LGL_RS_ZENABLE, 0);
            }

            litegl_bind_texture(lgl_naive, 0, textures_naive[obj->texture_id]);

            /* Naive buffer update: per-quad upload causing driver pipeline bubble */
            float s = obj->size;
            Vertex2D verts[6] = {
                { obj->x - s, obj->y - s,  0.0f, 0.0f,  obj->r, obj->g, obj->b, obj->a },
                { obj->x + s, obj->y - s,  1.0f, 0.0f,  obj->r, obj->g, obj->b, obj->a },
                { obj->x + s, obj->y + s,  1.0f, 1.0f,  obj->r, obj->g, obj->b, obj->a },
                { obj->x - s, obj->y - s,  0.0f, 0.0f,  obj->r, obj->g, obj->b, obj->a },
                { obj->x + s, obj->y + s,  1.0f, 1.0f,  obj->r, obj->g, obj->b, obj->a },
                { obj->x - s, obj->y + s,  0.0f, 1.0f,  obj->r, obj->g, obj->b, obj->a },
            };

            litegl_update_buffer(lgl_naive, dynamic_vbo_naive, 0, sizeof(verts), verts, false);
            litegl_bind_buffer(lgl_naive, dynamic_vbo_naive);

            /* Setup vertex attributes */
            litegl_enable_vertex_attrib_array(lgl_naive, 0);
            litegl_vertex_attrib_pointer(lgl_naive, 0, 2, GL_FLOAT, false, sizeof(Vertex2D), (void*)0);
            litegl_enable_vertex_attrib_array(lgl_naive, 1);
            litegl_vertex_attrib_pointer(lgl_naive, 1, 2, GL_FLOAT, false, sizeof(Vertex2D), (void*)(sizeof(float) * 2));
            litegl_enable_vertex_attrib_array(lgl_naive, 2);
            litegl_vertex_attrib_pointer(lgl_naive, 2, 4, GL_FLOAT, false, sizeof(Vertex2D), (void*)(sizeof(float) * 4));

            litegl_draw_arrays(lgl_naive, LGL_PRIM_TRIANGLES, 0, 6);
        }

        SDL_GL_SwapWindow(window);
    }
    double t1_naive = get_time_sec();
    double duration_naive = t1_naive - t0_naive;
    double fps_naive = (double)bench_target_frames / duration_naive;
    double frametime_naive = (duration_naive / (double)bench_target_frames) * 1000.0;

    LiteGLStats stats_naive;
    litegl_get_stats(lgl_naive, &stats_naive);

    printf(" [Naive OpenGL] Result: %d frames in %.3f s | FPS: %.1f | Frame Time: %.2f ms\n",
           bench_target_frames, duration_naive, fps_naive, frametime_naive);
    printf(" [Naive OpenGL] Draw Calls: %lu | State Changes Requested: %lu | Filtered: %lu\n",
           (unsigned long)stats_naive.draw_calls,
           (unsigned long)stats_naive.state_changes_requested,
           (unsigned long)stats_naive.state_changes_filtered);

    /* Cleanup Naive */
    litegl_destroy_buffer(lgl_naive, dynamic_vbo_naive);
    for (int i = 0; i < 4; ++i) litegl_destroy_texture(lgl_naive, textures_naive[i]);
    litegl_destroy_shader(lgl_naive, shader_naive);
    litegl_destroy_context(lgl_naive);

    /* -------------------------------------------------------------------------
     * BENCHMARK 2: LITEGL (ToGL-Based Shadow State & Dynamic Ring Streamer)
     * ------------------------------------------------------------------------- */
    printf("\n===================================================================\n");
    printf(" [2/2] RUNNING BENCHMARK: LITEGL (ToGL-Based)\n");
    printf(" - With aggressive state caching (redundant calls filtered)\n");
    printf(" - With ToGL dynamic ring buffer streaming (zero GPU pipeline stalls)\n");
    printf("===================================================================\n");

    init_scene(); /* Reset scene RNG to ensure identical workload */

    LiteGLConfig cfg_lite = litegl_default_config(SDL_GL_GetProcAddress);
    cfg_lite.enable_state_caching = true; /* ToGL state shadowing enabled */
    LiteGLContext* lgl_lite = litegl_create_context(&cfg_lite);

    LiteGLShader* shader_lite = litegl_create_shader(lgl_lite, vertex_shader_src, fragment_shader_src);
    LiteGLTexture* textures_lite[4];
    for (int i = 0; i < 4; ++i) {
        textures_lite[i] = litegl_create_texture_2d(lgl_lite, 64, 64, 4, tex_data[i], LGL_TEX_FILTER_LINEAR, LGL_TEX_WRAP_REPEAT);
    }

    double t0_lite = get_time_sec();
    for (int frame = 0; frame < bench_target_frames; ++frame) {
        update_scene();
        litegl_begin_frame(lgl_lite);
        litegl_clear(lgl_lite, 3, 0.05f, 0.05f, 0.08f, 1.0f, 1.0f);

        for (int i = 0; i < NUM_OBJECTS; ++i) {
            SceneObject* obj = &s_objects[i];

            /* LiteGL: ToGL state shadowing automatically filters redundant calls */
            litegl_bind_shader(lgl_lite, shader_lite);

            if (obj->blend_mode) {
                litegl_set_render_state(lgl_lite, LGL_RS_ALPHABLENDENABLE, 1);
                litegl_set_render_state(lgl_lite, LGL_RS_SRCBLEND, LGL_BLEND_SRCALPHA);
                litegl_set_render_state(lgl_lite, LGL_RS_DESTBLEND, LGL_BLEND_INVSRCALPHA);
            } else {
                litegl_set_render_state(lgl_lite, LGL_RS_ALPHABLENDENABLE, 0);
            }

            if (obj->depth_test) {
                litegl_set_render_state(lgl_lite, LGL_RS_ZENABLE, 1);
            } else {
                litegl_set_render_state(lgl_lite, LGL_RS_ZENABLE, 0);
            }

            litegl_bind_texture(lgl_lite, 0, textures_lite[obj->texture_id]);

            /* LiteGL Dynamic Ring Streamer: Zero-stall D3D Discard/No-Overwrite streaming */
            float s = obj->size;
            Vertex2D verts[6] = {
                { obj->x - s, obj->y - s,  0.0f, 0.0f,  obj->r, obj->g, obj->b, obj->a },
                { obj->x + s, obj->y - s,  1.0f, 0.0f,  obj->r, obj->g, obj->b, obj->a },
                { obj->x + s, obj->y + s,  1.0f, 1.0f,  obj->r, obj->g, obj->b, obj->a },
                { obj->x - s, obj->y - s,  0.0f, 0.0f,  obj->r, obj->g, obj->b, obj->a },
                { obj->x + s, obj->y + s,  1.0f, 1.0f,  obj->r, obj->g, obj->b, obj->a },
                { obj->x - s, obj->y + s,  0.0f, 1.0f,  obj->r, obj->g, obj->b, obj->a },
            };

            uint32_t offset = 0;
            litegl_stream_upload_vertices(lgl_lite, verts, sizeof(verts), &offset);

            litegl_enable_vertex_attrib_array(lgl_lite, 0);
            litegl_vertex_attrib_pointer(lgl_lite, 0, 2, GL_FLOAT, false, sizeof(Vertex2D), (void*)(size_t)offset);
            litegl_enable_vertex_attrib_array(lgl_lite, 1);
            litegl_vertex_attrib_pointer(lgl_lite, 1, 2, GL_FLOAT, false, sizeof(Vertex2D), (void*)(size_t)(offset + sizeof(float) * 2));
            litegl_enable_vertex_attrib_array(lgl_lite, 2);
            litegl_vertex_attrib_pointer(lgl_lite, 2, 4, GL_FLOAT, false, sizeof(Vertex2D), (void*)(size_t)(offset + sizeof(float) * 4));

            litegl_draw_arrays(lgl_lite, LGL_PRIM_TRIANGLES, 0, 6);
        }

        litegl_end_frame(lgl_lite);
        SDL_GL_SwapWindow(window);
    }
    double t1_lite = get_time_sec();
    double duration_lite = t1_lite - t0_lite;
    double fps_lite = (double)bench_target_frames / duration_lite;
    double frametime_lite = (duration_lite / (double)bench_target_frames) * 1000.0;

    LiteGLStats stats_lite;
    litegl_get_stats(lgl_lite, &stats_lite);

    printf(" [LiteGL] Result: %d frames in %.3f s | FPS: %.1f | Frame Time: %.2f ms\n",
           bench_target_frames, duration_lite, fps_lite, frametime_lite);
    printf(" [LiteGL] Draw Calls: %lu | State Changes Requested: %lu | Filtered (Avoided): %lu (%.1f%%)\n",
           (unsigned long)stats_lite.draw_calls,
           (unsigned long)stats_lite.state_changes_requested,
           (unsigned long)stats_lite.state_changes_filtered,
           100.0 * (double)stats_lite.state_changes_filtered / (double)(stats_lite.state_changes_requested ? stats_lite.state_changes_requested : 1));
    printf(" [LiteGL] Texture Binds Requested: %lu | Filtered: %lu (%.1f%%)\n",
           (unsigned long)stats_lite.texture_binds_requested,
           (unsigned long)stats_lite.texture_binds_filtered,
           100.0 * (double)stats_lite.texture_binds_filtered / (double)(stats_lite.texture_binds_requested ? stats_lite.texture_binds_requested : 1));
    printf(" [LiteGL] Program Binds Requested: %lu | Filtered: %lu (%.1f%%)\n",
           (unsigned long)stats_lite.program_binds_requested,
           (unsigned long)stats_lite.program_binds_filtered,
           100.0 * (double)stats_lite.program_binds_filtered / (double)(stats_lite.program_binds_requested ? stats_lite.program_binds_requested : 1));

    /* Summary Comparison */
    double speedup = (fps_lite - fps_naive) / fps_naive * 100.0;
    printf("\n===================================================================\n");
    printf("                    BENCHMARK COMPARISON SUMMARY                   \n");
    printf("===================================================================\n");
    printf(" Renderer                 | FPS        | Frame Time   | Driver Overhead \n");
    printf(" -------------------------+------------+--------------+----------------\n");
    printf(" Naive Standard OpenGL    | %7.1f    | %6.2f ms    | High (0%% filtered)\n", fps_naive, frametime_naive);
    printf(" LiteGL (Valve ToGL-base) | %7.1f    | %6.2f ms    | Low  (%.1f%% filtered)\n",
           fps_lite, frametime_lite,
           100.0 * (double)stats_lite.state_changes_filtered / (double)(stats_lite.state_changes_requested ? stats_lite.state_changes_requested : 1));
    printf(" -------------------------+------------+--------------+----------------\n");
    if (speedup >= 0) {
        printf(" PERFORMANCE GAIN: LiteGL is +%.1f%% FASTER than Naive OpenGL! (%.2fx speedup)\n", speedup, fps_lite / fps_naive);
    } else {
        printf(" PERFORMANCE: Naive: %.1f vs LiteGL: %.1f\n", fps_naive, fps_lite);
    }
    printf(" Total redundant GL driver calls eliminated: %lu calls\n",
           (unsigned long)(stats_lite.state_changes_filtered + stats_lite.texture_binds_filtered + stats_lite.program_binds_filtered));
    printf("===================================================================\n\n");

    /* Cleanup LiteGL */
    for (int i = 0; i < 4; ++i) {
        litegl_destroy_texture(lgl_lite, textures_lite[i]);
        free(tex_data[i]);
    }
    litegl_destroy_shader(lgl_lite, shader_lite);
    litegl_destroy_context(lgl_lite);

    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
