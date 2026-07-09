/* quill renderer: a batched 2D pipeline on OpenGL 3.3 core. Solid rectangles,
 * rounded rectangles, and glyph quads share one vertex buffer. Each vertex
 * carries a mode: 0 solid fill, 1 glyph (atlas red channel is coverage), 2
 * rounded rect (a signed-distance field gives antialiased corners). Clipping
 * uses the scissor test and flushes at each change. GL functions past 1.1 are
 * loaded through GLFW's cross-platform proc loader. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "glfw/include/GLFW/glfw3.h"

typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;

#define GL_ARRAY_BUFFER 0x8892
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_R8 0x8229
#define GL_RED 0x1903
#define GL_CLAMP_TO_EDGE 0x812F

#define GLFN(ret, name, args) \
    typedef ret(APIENTRY * PFN_##name) args; \
    static PFN_##name p_##name;

GLFN(void, glGenBuffers, (GLsizei, GLuint *))
GLFN(void, glBindBuffer, (GLenum, GLuint))
GLFN(void, glBufferData, (GLenum, GLsizeiptr, const void *, GLenum))
GLFN(void, glBufferSubData, (GLenum, GLintptr, GLsizeiptr, const void *))
GLFN(void, glGenVertexArrays, (GLsizei, GLuint *))
GLFN(void, glBindVertexArray, (GLuint))
GLFN(void, glEnableVertexAttribArray, (GLuint))
GLFN(void, glVertexAttribPointer,
     (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *))
GLFN(GLuint, glCreateShader, (GLenum))
GLFN(void, glShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *))
GLFN(void, glCompileShader, (GLuint))
GLFN(void, glGetShaderiv, (GLuint, GLenum, GLint *))
GLFN(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *))
GLFN(GLuint, glCreateProgram, (void))
GLFN(void, glAttachShader, (GLuint, GLuint))
GLFN(void, glLinkProgram, (GLuint))
GLFN(void, glGetProgramiv, (GLuint, GLenum, GLint *))
GLFN(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *))
GLFN(void, glUseProgram, (GLuint))
GLFN(void, glDeleteShader, (GLuint))
GLFN(GLint, glGetUniformLocation, (GLuint, const GLchar *))
GLFN(void, glUniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat *))
GLFN(void, glUniform1i, (GLint, GLint))

static int load_gl(void) {
#define LOAD(name) \
    p_##name = (PFN_##name)glfwGetProcAddress(#name); \
    if (!p_##name) return -1;
    LOAD(glGenBuffers)
    LOAD(glBindBuffer)
    LOAD(glBufferData)
    LOAD(glBufferSubData)
    LOAD(glGenVertexArrays)
    LOAD(glBindVertexArray)
    LOAD(glEnableVertexAttribArray)
    LOAD(glVertexAttribPointer)
    LOAD(glCreateShader)
    LOAD(glShaderSource)
    LOAD(glCompileShader)
    LOAD(glGetShaderiv)
    LOAD(glGetShaderInfoLog)
    LOAD(glCreateProgram)
    LOAD(glAttachShader)
    LOAD(glLinkProgram)
    LOAD(glGetProgramiv)
    LOAD(glGetProgramInfoLog)
    LOAD(glUseProgram)
    LOAD(glDeleteShader)
    LOAD(glGetUniformLocation)
    LOAD(glUniformMatrix4fv)
    LOAD(glUniform1i)
#undef LOAD
    return 0;
}

static const char *VERT_SRC =
    "#version 330 core\n"
    "layout(location=0) in vec2 a_pos;\n"
    "layout(location=1) in vec2 a_uv;\n"
    "layout(location=2) in vec4 a_color;\n"
    "layout(location=3) in float a_mode;\n"
    "layout(location=4) in vec4 a_rrect;\n"
    "uniform mat4 u_proj;\n"
    "out vec2 v_uv;\n"
    "out vec4 v_color;\n"
    "out float v_mode;\n"
    "out vec4 v_rrect;\n"
    "void main() {\n"
    "    v_uv = a_uv;\n"
    "    v_color = a_color;\n"
    "    v_mode = a_mode;\n"
    "    v_rrect = a_rrect;\n"
    "    gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

static const char *FRAG_SRC =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "in vec4 v_color;\n"
    "in float v_mode;\n"
    "in vec4 v_rrect;\n"
    "uniform sampler2D u_tex;\n"
    "out vec4 frag_color;\n"
    "float sd_round_box(vec2 p, vec2 b, float r) {\n"
    "    vec2 q = abs(p) - b + vec2(r);\n"
    "    return min(max(q.x, q.y), 0.0) + length(max(q, vec2(0.0))) - r;\n"
    "}\n"
    "void main() {\n"
    "    if (v_mode > 2.5) {\n"
    "        frag_color = texture(u_tex, v_uv) * v_color;\n"
    "    } else if (v_mode > 1.5) {\n"
    "        vec2 b = vec2(v_rrect.x, v_rrect.y);\n"
    "        float d = sd_round_box(v_uv, b, v_rrect.z);\n"
    "        float aa = fwidth(d);\n"
    "        float cov = 1.0 - smoothstep(-aa, aa, d);\n"
    "        frag_color = vec4(v_color.rgb, v_color.a * cov);\n"
    "    } else if (v_mode > 0.5) {\n"
    "        float cov = texture(u_tex, v_uv).r;\n"
    "        frag_color = vec4(v_color.rgb, v_color.a * cov);\n"
    "    } else {\n"
    "        frag_color = v_color;\n"
    "    }\n"
    "}\n";

#define FLOATS_PER_VERTEX 13
#define MAX_VERTICES 65536
#define MAX_CLIPS 16

static GLuint g_program;
static GLuint g_vao;
static GLuint g_vbo;
static GLint g_proj_loc;
static GLuint g_atlas_tex;
static int g_atlas_w;
static int g_atlas_h;
static int g_fb_h;
static int g_clip[MAX_CLIPS][4];
static int g_clip_n;
static float g_verts[MAX_VERTICES * FLOATS_PER_VERTEX];
static int g_vert_count;

static GLuint compile_shader(GLenum kind, const char *src) {
    GLuint sh = p_glCreateShader(kind);
    p_glShaderSource(sh, 1, &src, NULL);
    p_glCompileShader(sh);
    GLint ok = 0;
    p_glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        p_glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        fprintf(stderr, "quill: shader compile failed: %s\n", log);
        return 0;
    }
    return sh;
}

int q_render_init(void) {
    if (load_gl() != 0) {
        fprintf(stderr, "quill: could not load OpenGL 3.3 functions\n");
        return -1;
    }

    GLuint vs = compile_shader(GL_VERTEX_SHADER, VERT_SRC);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, FRAG_SRC);
    if (!vs || !fs) {
        return -1;
    }
    g_program = p_glCreateProgram();
    p_glAttachShader(g_program, vs);
    p_glAttachShader(g_program, fs);
    p_glLinkProgram(g_program);
    GLint ok = 0;
    p_glGetProgramiv(g_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        p_glGetProgramInfoLog(g_program, sizeof(log), NULL, log);
        fprintf(stderr, "quill: program link failed: %s\n", log);
        return -1;
    }
    p_glDeleteShader(vs);
    p_glDeleteShader(fs);
    g_proj_loc = p_glGetUniformLocation(g_program, "u_proj");
    p_glUseProgram(g_program);
    p_glUniform1i(p_glGetUniformLocation(g_program, "u_tex"), 0);

    p_glGenVertexArrays(1, &g_vao);
    p_glBindVertexArray(g_vao);
    p_glGenBuffers(1, &g_vbo);
    p_glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    p_glBufferData(GL_ARRAY_BUFFER, sizeof(g_verts), NULL, GL_DYNAMIC_DRAW);
    GLsizei stride = FLOATS_PER_VERTEX * sizeof(float);
    p_glEnableVertexAttribArray(0);
    p_glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void *)0);
    p_glEnableVertexAttribArray(1);
    p_glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void *)(2 * sizeof(float)));
    p_glEnableVertexAttribArray(2);
    p_glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void *)(4 * sizeof(float)));
    p_glEnableVertexAttribArray(3);
    p_glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, (void *)(8 * sizeof(float)));
    p_glEnableVertexAttribArray(4);
    p_glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride, (void *)(9 * sizeof(float)));
    p_glBindVertexArray(0);
    return 0;
}

// Create (or replace) the single-channel glyph atlas texture.
void q_atlas_init(int width, int height) {
    if (g_atlas_tex == 0) {
        glGenTextures(1, &g_atlas_tex);
    }
    g_atlas_w = width;
    g_atlas_h = height;
    glBindTexture(GL_TEXTURE_2D, g_atlas_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void q_atlas_upload(const unsigned char *data) {
    if (g_atlas_tex == 0) {
        return;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glBindTexture(GL_TEXTURE_2D, g_atlas_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, g_atlas_w, g_atlas_h, GL_RED,
                    GL_UNSIGNED_BYTE, data);
}

static void flush(void) {
    if (g_vert_count == 0) {
        return;
    }
    p_glUseProgram(g_program);
    if (g_atlas_tex != 0) {
        glBindTexture(GL_TEXTURE_2D, g_atlas_tex);
    }
    p_glBindVertexArray(g_vao);
    p_glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    p_glBufferSubData(GL_ARRAY_BUFFER, 0,
                      (GLsizeiptr)g_vert_count * FLOATS_PER_VERTEX * sizeof(float),
                      g_verts);
    glDrawArrays(GL_TRIANGLES, 0, g_vert_count);
    g_vert_count = 0;
}

void q_frame_begin(int64_t width, int64_t height) {
    g_fb_h = (int)height;
    g_clip_n = 0;
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, (int)width, (int)height);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    float w = (float)width;
    float h = (float)height;
    float proj[16] = {
        2.0f / w, 0.0f, 0.0f, 0.0f,
        0.0f, -2.0f / h, 0.0f, 0.0f,
        0.0f, 0.0f, -1.0f, 0.0f,
        -1.0f, 1.0f, 0.0f, 1.0f};
    p_glUseProgram(g_program);
    p_glUniformMatrix4fv(g_proj_loc, 1, GL_FALSE, proj);
    g_vert_count = 0;
}

static void push_vertex(float x, float y, float u, float v,
                        float r, float g, float b, float a, float mode,
                        float hw, float hh, float radius) {
    float *p = &g_verts[g_vert_count * FLOATS_PER_VERTEX];
    p[0] = x;
    p[1] = y;
    p[2] = u;
    p[3] = v;
    p[4] = r;
    p[5] = g;
    p[6] = b;
    p[7] = a;
    p[8] = mode;
    p[9] = hw;
    p[10] = hh;
    p[11] = radius;
    p[12] = 0.0f;
    g_vert_count++;
}

// A single glyph vertex from the text layer; coverage sampled from the atlas.
void q_push_text_vertex(float x, float y, float u, float v,
                        float r, float g, float b, float a) {
    if (g_vert_count + 1 > MAX_VERTICES) {
        flush();
    }
    push_vertex(x, y, u, v, r, g, b, a, 1.0f, 0.0f, 0.0f, 0.0f);
}

void q_fill_rect(int64_t x, int64_t y, int64_t w, int64_t h,
                 int64_t r, int64_t g, int64_t b, int64_t a) {
    if (g_vert_count + 6 > MAX_VERTICES) {
        flush();
    }
    float x0 = (float)x, y0 = (float)y, x1 = (float)(x + w), y1 = (float)(y + h);
    float rf = (float)r / 255.0f, gf = (float)g / 255.0f;
    float bf = (float)b / 255.0f, af = (float)a / 255.0f;
    push_vertex(x0, y0, 0, 0, rf, gf, bf, af, 0.0f, 0, 0, 0);
    push_vertex(x1, y0, 0, 0, rf, gf, bf, af, 0.0f, 0, 0, 0);
    push_vertex(x1, y1, 0, 0, rf, gf, bf, af, 0.0f, 0, 0, 0);
    push_vertex(x0, y0, 0, 0, rf, gf, bf, af, 0.0f, 0, 0, 0);
    push_vertex(x1, y1, 0, 0, rf, gf, bf, af, 0.0f, 0, 0, 0);
    push_vertex(x0, y1, 0, 0, rf, gf, bf, af, 0.0f, 0, 0, 0);
}

void q_fill_round_rect(int64_t x, int64_t y, int64_t w, int64_t h, int64_t radius,
                       int64_t r, int64_t g, int64_t b, int64_t a) {
    if (g_vert_count + 6 > MAX_VERTICES) {
        flush();
    }
    float x0 = (float)x, y0 = (float)y, x1 = (float)(x + w), y1 = (float)(y + h);
    float hw = (float)w / 2.0f, hh = (float)h / 2.0f;
    float rad = (float)radius;
    float limit = hw < hh ? hw : hh;
    if (rad > limit) {
        rad = limit;
    }
    float rf = (float)r / 255.0f, gf = (float)g / 255.0f;
    float bf = (float)b / 255.0f, af = (float)a / 255.0f;
    // v_uv carries the position relative to the rect center, in pixels.
    push_vertex(x0, y0, -hw, -hh, rf, gf, bf, af, 2.0f, hw, hh, rad);
    push_vertex(x1, y0, hw, -hh, rf, gf, bf, af, 2.0f, hw, hh, rad);
    push_vertex(x1, y1, hw, hh, rf, gf, bf, af, 2.0f, hw, hh, rad);
    push_vertex(x0, y0, -hw, -hh, rf, gf, bf, af, 2.0f, hw, hh, rad);
    push_vertex(x1, y1, hw, hh, rf, gf, bf, af, 2.0f, hw, hh, rad);
    push_vertex(x0, y1, -hw, hh, rf, gf, bf, af, 2.0f, hw, hh, rad);
}

// Restrict drawing to a rectangle (top-left origin). Clips nest.
void q_push_clip(int64_t x, int64_t y, int64_t w, int64_t h) {
    flush();
    if (g_clip_n < MAX_CLIPS) {
        g_clip[g_clip_n][0] = (int)x;
        g_clip[g_clip_n][1] = (int)y;
        g_clip[g_clip_n][2] = (int)w;
        g_clip[g_clip_n][3] = (int)h;
        g_clip_n++;
    }
    glEnable(GL_SCISSOR_TEST);
    glScissor((int)x, g_fb_h - (int)(y + h), (int)w, (int)h);
}

void q_pop_clip(void) {
    flush();
    if (g_clip_n > 0) {
        g_clip_n--;
    }
    if (g_clip_n == 0) {
        glDisable(GL_SCISSOR_TEST);
    } else {
        int *c = g_clip[g_clip_n - 1];
        glScissor(c[0], g_fb_h - (c[1] + c[3]), c[2], c[3]);
    }
}

void q_frame_end(void) {
    flush();
}

extern int64_t q_image_tex(int64_t idx);

// Draw an image (by index) into a rectangle. The pending batch is flushed first
// so z-order is preserved; the image uses its own texture, and the next solid or
// glyph rebinds the atlas on its flush.
void q_draw_image(int64_t idx, int64_t x, int64_t y, int64_t w, int64_t h) {
    flush();
    int64_t tex = q_image_tex(idx);
    if (tex == 0) {
        return;
    }
    float x0 = (float)x, y0 = (float)y, x1 = (float)(x + w), y1 = (float)(y + h);
    g_vert_count = 0;
    push_vertex(x0, y0, 0, 0, 1, 1, 1, 1, 3.0f, 0, 0, 0);
    push_vertex(x1, y0, 1, 0, 1, 1, 1, 1, 3.0f, 0, 0, 0);
    push_vertex(x1, y1, 1, 1, 1, 1, 1, 1, 3.0f, 0, 0, 0);
    push_vertex(x0, y0, 0, 0, 1, 1, 1, 1, 3.0f, 0, 0, 0);
    push_vertex(x1, y1, 1, 1, 1, 1, 1, 1, 3.0f, 0, 0, 0);
    push_vertex(x0, y1, 0, 1, 1, 1, 1, 1, 3.0f, 0, 0, 0);
    p_glUseProgram(g_program);
    glBindTexture(GL_TEXTURE_2D, (GLuint)tex);
    p_glBindVertexArray(g_vao);
    p_glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    p_glBufferSubData(GL_ARRAY_BUFFER, 0,
                      (GLsizeiptr)g_vert_count * FLOATS_PER_VERTEX * sizeof(float), g_verts);
    glDrawArrays(GL_TRIANGLES, 0, g_vert_count);
    g_vert_count = 0;
}
