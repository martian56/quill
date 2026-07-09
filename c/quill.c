/* quill backend: GLFW windowing and GL clears behind a flat int64/cstr ABI so
 * Raven binds it without passing structs or floats. Window handles cross as an
 * int64 holding the pointer. */
#include <stdint.h>

#include "glfw/include/GLFW/glfw3.h"

#define WIN(h) ((GLFWwindow *)(intptr_t)(h))
#define H(p) ((int64_t)(intptr_t)(p))

// Bring up the GL 3.3 core renderer once a context is current (c/render.c).
extern int q_render_init(void);

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
