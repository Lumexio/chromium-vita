#include "netsurf/scaffold.h"

#include <stdlib.h>
#include <string.h>

struct ns_scaffold_context {
    int initialized;
};

struct ns_scaffold_window {
    int width;
    int height;

    int pointer_x;
    int pointer_y;
    int pointer_pressed;
    int scroll_lines;

    char url[256];
    char title[128];
    char body[4096];
};

static uint32_t make_rgba(unsigned int r, unsigned int g, unsigned int b) {
    return (0xFFu << 24) | ((b & 0xFFu) << 16) | ((g & 0xFFu) << 8) | (r & 0xFFu);
}

static unsigned int hash_str(const char* s) {
    unsigned int h = 2166136261u;
    if (s == NULL) return h;
    while (*s != '\0') {
        h ^= (unsigned char)*s++;
        h *= 16777619u;
    }
    return h;
}

static void clear_surface(uint32_t* pixels, int width, int height, int stride, uint32_t color) {
    int y;
    for (y = 0; y < height; ++y) {
        int x;
        uint32_t* row = pixels + (y * stride);
        for (x = 0; x < width; ++x) row[x] = color;
    }
}

static void fill_rect(uint32_t* pixels, int width, int height, int stride, int x, int y, int w, int h,
                      uint32_t color) {
    int yy;
    if (w <= 0 || h <= 0) return;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x >= width || y >= height) return;
    if (x + w > width) w = width - x;
    if (y + h > height) h = height - y;
    if (w <= 0 || h <= 0) return;

    for (yy = 0; yy < h; ++yy) {
        int xx;
        uint32_t* row = pixels + (y + yy) * stride + x;
        for (xx = 0; xx < w; ++xx) row[xx] = color;
    }
}

int ns_scaffold_init(ns_scaffold_context** out_ctx) {
    ns_scaffold_context* ctx;
    if (out_ctx == NULL) return -1;
    ctx = (ns_scaffold_context*)calloc(1, sizeof(*ctx));
    if (ctx == NULL) return -1;
    ctx->initialized = 1;
    *out_ctx = ctx;
    return 0;
}

void ns_scaffold_shutdown(ns_scaffold_context* ctx) {
    free(ctx);
}

ns_scaffold_window* ns_scaffold_create_window(ns_scaffold_context* ctx, int width, int height) {
    ns_scaffold_window* window;
    if (ctx == NULL || !ctx->initialized || width <= 0 || height <= 0) return NULL;
    window = (ns_scaffold_window*)calloc(1, sizeof(*window));
    if (window == NULL) return NULL;

    window->width = width;
    window->height = height;
    window->pointer_x = width / 2;
    window->pointer_y = height / 2;
    strncpy(window->url, "about:blank", sizeof(window->url) - 1);
    strncpy(window->title, "NetSurf scaffold", sizeof(window->title) - 1);
    return window;
}

void ns_scaffold_destroy_window(ns_scaffold_window* window) {
    free(window);
}

void ns_scaffold_set_document(ns_scaffold_window* window, const char* url, const char* title,
                              const char* body_text) {
    if (window == NULL) return;
    window->scroll_lines = 0;

    if (url != NULL) {
        strncpy(window->url, url, sizeof(window->url) - 1);
        window->url[sizeof(window->url) - 1] = '\0';
    }
    if (title != NULL) {
        strncpy(window->title, title, sizeof(window->title) - 1);
        window->title[sizeof(window->title) - 1] = '\0';
    }
    if (body_text != NULL) {
        strncpy(window->body, body_text, sizeof(window->body) - 1);
        window->body[sizeof(window->body) - 1] = '\0';
    }
}

void ns_scaffold_send_key(ns_scaffold_window* window, ns_scaffold_key key, int pressed) {
    if (window == NULL || !pressed) return;

    switch (key) {
        case NS_SCAFFOLD_KEY_UP:
            window->scroll_lines -= 2;
            break;
        case NS_SCAFFOLD_KEY_DOWN:
            window->scroll_lines += 2;
            break;
        case NS_SCAFFOLD_KEY_LEFT:
            window->pointer_x -= 16;
            break;
        case NS_SCAFFOLD_KEY_RIGHT:
            window->pointer_x += 16;
            break;
        case NS_SCAFFOLD_KEY_ACTIVATE:
            window->pointer_pressed = !window->pointer_pressed;
            break;
    }

    if (window->pointer_x < 0) window->pointer_x = 0;
    if (window->pointer_y < 0) window->pointer_y = 0;
    if (window->pointer_x >= window->width) window->pointer_x = window->width - 1;
    if (window->pointer_y >= window->height) window->pointer_y = window->height - 1;
    if (window->scroll_lines < 0) window->scroll_lines = 0;
}

void ns_scaffold_send_pointer(ns_scaffold_window* window, int x, int y, int pressed) {
    if (window == NULL) return;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= window->width) x = window->width - 1;
    if (y >= window->height) y = window->height - 1;
    window->pointer_x = x;
    window->pointer_y = y;
    window->pointer_pressed = pressed ? 1 : 0;
}

void ns_scaffold_render_rgba(ns_scaffold_window* window, uint32_t* pixels, int width, int height,
                             int stride_pixels) {
    unsigned int hash;
    unsigned int base_r;
    unsigned int base_g;
    unsigned int base_b;
    const int top_h = 30;
    const int line_h = 12;
    int line_index;
    const char* cursor;
    if (window == NULL || pixels == NULL || width <= 0 || height <= 0 || stride_pixels < width) return;

    hash = hash_str(window->url);
    base_r = 0x16u + (hash & 0x1Fu);
    base_g = 0x1Au + ((hash >> 8) & 0x1Fu);
    base_b = 0x22u + ((hash >> 16) & 0x1Fu);
    clear_surface(pixels, width, height, stride_pixels, make_rgba(base_r, base_g, base_b));

    fill_rect(pixels, width, height, stride_pixels, 0, 0, width, top_h, make_rgba(0x28, 0x31, 0x42));
    fill_rect(pixels, width, height, stride_pixels, 0, top_h, width, 2, make_rgba(0xEC, 0xA4, 0x5B));

    line_index = 0;
    cursor = window->body;
    while (*cursor != '\0' && line_index < 28) {
        int len = 0;
        int y;
        while (*cursor != '\0' && *cursor != '\n' && len < 120) {
            ++len;
            ++cursor;
        }
        if (*cursor == '\n') ++cursor;
        y = top_h + 8 + (line_index * line_h) - (window->scroll_lines * 2);
        if (y >= top_h + 2 && y + 8 < height) {
            int bar_w = 16 + (len * 4);
            if (bar_w > width - 20) bar_w = width - 20;
            fill_rect(pixels, width, height, stride_pixels, 10, y, bar_w, 8, make_rgba(0xD8, 0xE3, 0xFB));
        }
        ++line_index;
    }

    fill_rect(pixels, width, height, stride_pixels, window->pointer_x - 2, window->pointer_y - 2, 5, 5,
              window->pointer_pressed ? make_rgba(0xF3, 0xC7, 0x4D) : make_rgba(0xFF, 0xFF, 0xFF));
}
