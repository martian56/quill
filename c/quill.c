/* quill backend: GLFW windowing and GL clears behind a flat int64/cstr ABI so
 * Raven binds it without passing structs or floats. Window handles cross as an
 * int64 holding the pointer. */
#include <stdint.h>

#include "glfw/include/GLFW/glfw3.h"

#define WIN(h) ((GLFWwindow *)(intptr_t)(h))
#define H(p) ((int64_t)(intptr_t)(p))

int64_t q_init(void) {
    return glfwInit();
}

void q_terminate(void) {
    glfwTerminate();
}

int64_t q_window_open(int64_t width, int64_t height, const char *title) {
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    GLFWwindow *win = glfwCreateWindow((int)width, (int)height, title, NULL, NULL);
    if (!win) {
        return 0;
    }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);
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
