/* quill backend: GLFW windowing and GL clears behind a flat int64/cstr ABI so
 * Raven binds it without passing structs or floats. Window handles cross as an
 * int64 holding the pointer. */
#include <stdint.h>

#include "glfw/include/GLFW/glfw3.h"

#define WIN(h) ((GLFWwindow *)(intptr_t)(h))
#define H(p) ((int64_t)(intptr_t)(p))

// Bring up the GL 3.3 core renderer once a context is current (c/render.c).
extern int q_render_init(void);

// Typed characters and edit keys are delivered through GLFW callbacks (C to C,
// which import-only FFI allows) into ring buffers that Raven drains each frame.
#define QCAP 256
static unsigned int char_q[QCAP];
static int char_head, char_tail;
static int key_q[QCAP];
static int key_head, key_tail;

static void char_cb(GLFWwindow *win, unsigned int codepoint) {
    (void)win;
    int next = (char_tail + 1) % QCAP;
    if (next != char_head) {
        char_q[char_tail] = codepoint;
        char_tail = next;
    }
}

static void key_cb(GLFWwindow *win, int key, int scancode, int action, int mods) {
    (void)win;
    (void)scancode;
    (void)mods;
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        int next = (key_tail + 1) % QCAP;
        if (next != key_head) {
            key_q[key_tail] = key;
            key_tail = next;
        }
    }
}

// Accumulated vertical scroll, in wheel notches, taken and reset by Raven.
static double scroll_y;

static void scroll_cb(GLFWwindow *win, double xoff, double yoff) {
    (void)win;
    (void)xoff;
    scroll_y += yoff;
}

// Vertical scroll since the last call, in pixels (a notch is 40px), then reset.
int64_t q_take_scroll_y(void) {
    int64_t px = (int64_t)(scroll_y * 40.0);
    scroll_y = 0.0;
    return px;
}

int64_t q_poll_char(void) {
    if (char_head == char_tail) {
        return -1;
    }
    unsigned int cp = char_q[char_head];
    char_head = (char_head + 1) % QCAP;
    return (int64_t)cp;
}

int64_t q_poll_key(void) {
    if (key_head == key_tail) {
        return -1;
    }
    int k = key_q[key_head];
    key_head = (key_head + 1) % QCAP;
    return (int64_t)k;
}

// Encode a Unicode codepoint as a UTF-8 C string. The buffer is reused, so the
// caller must copy the result before the next call (Raven's from_cstr does).
const char *q_char_utf8(int64_t codepoint) {
    static char b[5];
    unsigned int c = (unsigned int)codepoint;
    int n = 0;
    if (c < 0x80) {
        b[0] = (char)c;
        n = 1;
    } else if (c < 0x800) {
        b[0] = (char)(0xC0 | (c >> 6));
        b[1] = (char)(0x80 | (c & 0x3F));
        n = 2;
    } else if (c < 0x10000) {
        b[0] = (char)(0xE0 | (c >> 12));
        b[1] = (char)(0x80 | ((c >> 6) & 0x3F));
        b[2] = (char)(0x80 | (c & 0x3F));
        n = 3;
    } else {
        b[0] = (char)(0xF0 | (c >> 18));
        b[1] = (char)(0x80 | ((c >> 12) & 0x3F));
        b[2] = (char)(0x80 | ((c >> 6) & 0x3F));
        b[3] = (char)(0x80 | (c & 0x3F));
        n = 4;
    }
    b[n] = 0;
    return b;
}

int64_t q_init(void) {
    return glfwInit();
}

void q_terminate(void) {
    glfwTerminate();
}

int64_t q_window_open(int64_t width, int64_t height, const char *title) {
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    GLFWwindow *win = glfwCreateWindow((int)width, (int)height, title, NULL, NULL);
    if (!win) {
        return 0;
    }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);
    glfwSetCharCallback(win, char_cb);
    glfwSetKeyCallback(win, key_cb);
    glfwSetScrollCallback(win, scroll_cb);
    if (q_render_init() != 0) {
        glfwDestroyWindow(win);
        return 0;
    }
    return H(win);
}

int64_t q_window_should_close(int64_t win) {
    return glfwWindowShouldClose(WIN(win));
}

void q_poll(void) {
    glfwPollEvents();
}

// Block until an event arrives (timeout_ms <= 0) or the timeout elapses, then
// process events. Lets an idle app sleep instead of spinning.
void q_wait(int64_t timeout_ms) {
    if (timeout_ms <= 0) {
        glfwWaitEvents();
    } else {
        glfwWaitEventsTimeout((double)timeout_ms / 1000.0);
    }
}

int64_t q_time_ms(void) {
    return (int64_t)(glfwGetTime() * 1000.0);
}

void q_window_swap(int64_t win) {
    glfwSwapBuffers(WIN(win));
}

void q_window_close(int64_t win) {
    glfwDestroyWindow(WIN(win));
}

int64_t q_framebuffer_width(int64_t win) {
    int w, h;
    glfwGetFramebufferSize(WIN(win), &w, &h);
    return w;
}

int64_t q_framebuffer_height(int64_t win) {
    int w, h;
    glfwGetFramebufferSize(WIN(win), &w, &h);
    return h;
}

void q_clear_rgb(int64_t r, int64_t g, int64_t b) {
    glClearColor((float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

// Cursor position scaled from window coordinates to framebuffer pixels, so it
// lines up with what the renderer draws under any display scaling.
static double cursor_scaled(int64_t win, int axis) {
    double cx, cy;
    glfwGetCursorPos(WIN(win), &cx, &cy);
    int ww, wh, fw, fh;
    glfwGetWindowSize(WIN(win), &ww, &wh);
    glfwGetFramebufferSize(WIN(win), &fw, &fh);
    if (axis == 0) {
        return ww > 0 ? cx * (double)fw / (double)ww : cx;
    }
    return wh > 0 ? cy * (double)fh / (double)wh : cy;
}

int64_t q_cursor_x(int64_t win) {
    return (int64_t)cursor_scaled(win, 0);
}

int64_t q_cursor_y(int64_t win) {
    return (int64_t)cursor_scaled(win, 1);
}

int64_t q_mouse_down(int64_t win) {
    return glfwGetMouseButton(WIN(win), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS ? 1 : 0;
}
