/* quill text: fontstash + stb_truetype build a glyph atlas and lay out strings.
 * The render backend routes fontstash's atlas uploads and triangles into the
 * shared GL batch in render.c. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

#define FONTSTASH_IMPLEMENTATION
#include "fontstash.h"

extern void q_atlas_init(int width, int height);
extern void q_atlas_upload(const unsigned char *data);
extern void q_push_text_vertex(float x, float y, float u, float v,
                               float r, float g, float b, float a);

static FONScontext *g_fons;

static int rc_create(void *up, int w, int h) {
    q_atlas_init(w, h);
    return 1;
}

static int rc_resize(void *up, int w, int h) {
    q_atlas_init(w, h);
    return 1;
}

static void rc_update(void *up, int *rect, const unsigned char *data) {
    q_atlas_upload(data);
}

static void rc_draw(void *up, const float *verts, const float *tc,
                    const unsigned int *colors, int nverts) {
    int i = 0;
    while (i < nverts) {
        unsigned int c = colors[i];
        float r = (float)(c & 0xff) / 255.0f;
        float g = (float)((c >> 8) & 0xff) / 255.0f;
        float b = (float)((c >> 16) & 0xff) / 255.0f;
        float a = (float)((c >> 24) & 0xff) / 255.0f;
        q_push_text_vertex(verts[i * 2], verts[i * 2 + 1], tc[i * 2], tc[i * 2 + 1], r, g, b, a);
        i++;
    }
}

static void rc_delete(void *up) {
    (void)up;
}

static int ensure_fons(void) {
    if (g_fons != NULL) {
        return 0;
    }
    FONSparams params;
    memset(&params, 0, sizeof(params));
    params.width = 512;
    params.height = 512;
    params.flags = FONS_ZERO_TOPLEFT;
    params.renderCreate = rc_create;
    params.renderResize = rc_resize;
    params.renderUpdate = rc_update;
    params.renderDraw = rc_draw;
    params.renderDelete = rc_delete;
    g_fons = fonsCreateInternal(&params);
    return g_fons == NULL ? -1 : 0;
}

// Load a TTF from disk. Returns the font id, or -1 on failure.
int64_t q_font_load(const char *path) {
    if (ensure_fons() != 0) {
        return -1;
    }
    return fonsAddFont(g_fons, "ui", path);
}

void q_text(int64_t font, int64_t x, int64_t y, int64_t px, const char *str,
            int64_t r, int64_t g, int64_t b, int64_t a) {
    if (g_fons == NULL || font < 0) {
        return;
    }
    unsigned int color = ((unsigned int)(r & 0xff)) | ((unsigned int)(g & 0xff) << 8) |
                         ((unsigned int)(b & 0xff) << 16) | ((unsigned int)(a & 0xff) << 24);
    fonsSetFont(g_fons, (int)font);
    fonsSetSize(g_fons, (float)px);
    fonsSetColor(g_fons, color);
    fonsSetAlign(g_fons, FONS_ALIGN_LEFT | FONS_ALIGN_TOP);
    fonsDrawText(g_fons, (float)x, (float)y, str, NULL);
}

int64_t q_measure_width(int64_t font, int64_t px, const char *str) {
    if (g_fons == NULL || font < 0) {
        return 0;
    }
    fonsSetFont(g_fons, (int)font);
    fonsSetSize(g_fons, (float)px);
    fonsSetAlign(g_fons, FONS_ALIGN_LEFT | FONS_ALIGN_TOP);
    float w = fonsTextBounds(g_fons, 0.0f, 0.0f, str, NULL, NULL);
    return (int64_t)(w + 0.5f);
}
