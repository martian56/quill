/* quill image loading: stb_image decodes PNG/JPG to RGBA, uploaded as a GL
 * texture. render.c draws it; this file owns the decode and a small registry so
 * Raven refers to images by index. */
#include <stdint.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO_WRITE
#include "stb_image.h"

#include "glfw/include/GLFW/glfw3.h"

#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#define GL_CLAMP_TO_EDGE 0x812F

#define MAX_IMAGES 256
static GLuint img_tex[MAX_IMAGES];
static int img_w[MAX_IMAGES];
static int img_h[MAX_IMAGES];
static int img_count;

// Load an image file into a GL texture. Returns its index, or -1 on failure.
int64_t q_image_load(const char *path) {
    if (img_count >= MAX_IMAGES) {
        return -1;
    }
    int w, h, n;
    unsigned char *data = stbi_load(path, &w, &h, &n, 4);
    if (data == NULL) {
        return -1;
    }
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    stbi_image_free(data);
    int idx = img_count;
    img_tex[idx] = tex;
    img_w[idx] = w;
    img_h[idx] = h;
    img_count++;
    return idx;
}

int64_t q_image_width(int64_t idx) {
    if (idx < 0 || idx >= img_count) {
        return 0;
    }
    return img_w[idx];
}

int64_t q_image_height(int64_t idx) {
    if (idx < 0 || idx >= img_count) {
        return 0;
    }
    return img_h[idx];
}

// The GL texture for an image index, for render.c.
int64_t q_image_tex(int64_t idx) {
    if (idx < 0 || idx >= img_count) {
        return 0;
    }
    return (int64_t)img_tex[idx];
}
