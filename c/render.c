/* quill renderer: a batched 2D quad pipeline on OpenGL 3.3 core. Rectangles are
 * accumulated into one vertex buffer per frame and drawn in a single pass. GL
 * functions past 1.1 are loaded through GLFW's cross-platform proc loader. */
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
#undef LOAD
    return 0;
}

static const char *VERT_SRC =
    "#version 330 core\n"
    "layout(location=0) in vec2 a_pos;\n"
    "layout(location=1) in vec4 a_color;\n"
    "uniform mat4 u_proj;\n"
    "out vec4 v_color;\n"
    "void main() {\n"
    "    v_color = a_color;\n"
    "    gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

static const char *FRAG_SRC =
    "#version 330 core\n"
    "in vec4 v_color;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "    frag_color = v_color;\n"
    "}\n";

#define FLOATS_PER_VERTEX 6
#define MAX_VERTICES 16384

static GLuint g_program;
static GLuint g_vao;
static GLuint g_vbo;
static GLint g_proj_loc;
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

    p_glGenVertexArrays(1, &g_vao);
    p_glBindVertexArray(g_vao);
    p_glGenBuffers(1, &g_vbo);
    p_glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    p_glBufferData(GL_ARRAY_BUFFER, sizeof(g_verts), NULL, GL_DYNAMIC_DRAW);
    GLsizei stride = FLOATS_PER_VERTEX * sizeof(float);
    p_glEnableVertexAttribArray(0);
    p_glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void *)0);
    p_glEnableVertexAttribArray(1);
    p_glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (void *)(2 * sizeof(float)));
    p_glBindVertexArray(0);
    return 0;
}

static void flush(void) {
    if (g_vert_count == 0) {
        return;
    }
    p_glUseProgram(g_program);
    p_glBindVertexArray(g_vao);
    p_glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    p_glBufferSubData(GL_ARRAY_BUFFER, 0,
                      (GLsizeiptr)g_vert_count * FLOATS_PER_VERTEX * sizeof(float),
                      g_verts);
    glDrawArrays(GL_TRIANGLES, 0, g_vert_count);
    g_vert_count = 0;
}

void q_frame_begin(int64_t width, int64_t height) {
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

static void push_vertex(float x, float y, float r, float g, float b, float a) {
    float *v = &g_verts[g_vert_count * FLOATS_PER_VERTEX];
    v[0] = x;
    v[1] = y;
    v[2] = r;
    v[3] = g;
    v[4] = b;
    v[5] = a;
    g_vert_count++;
}

void q_fill_rect(int64_t x, int64_t y, int64_t w, int64_t h,
                 int64_t r, int64_t g, int64_t b, int64_t a) {
    if (g_vert_count + 6 > MAX_VERTICES) {
        flush();
    }
    float x0 = (float)x;
    float y0 = (float)y;
    float x1 = (float)(x + w);
    float y1 = (float)(y + h);
    float rf = (float)r / 255.0f;
    float gf = (float)g / 255.0f;
    float bf = (float)b / 255.0f;
    float af = (float)a / 255.0f;
    push_vertex(x0, y0, rf, gf, bf, af);
    push_vertex(x1, y0, rf, gf, bf, af);
    push_vertex(x1, y1, rf, gf, bf, af);
    push_vertex(x0, y0, rf, gf, bf, af);
    push_vertex(x1, y1, rf, gf, bf, af);
    push_vertex(x0, y1, rf, gf, bf, af);
}

void q_frame_end(void) {
    flush();
}
