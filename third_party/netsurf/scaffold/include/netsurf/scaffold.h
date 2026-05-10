#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ns_scaffold_context ns_scaffold_context;
typedef struct ns_scaffold_window ns_scaffold_window;

typedef enum ns_scaffold_key {
    NS_SCAFFOLD_KEY_UP = 0,
    NS_SCAFFOLD_KEY_DOWN,
    NS_SCAFFOLD_KEY_LEFT,
    NS_SCAFFOLD_KEY_RIGHT,
    NS_SCAFFOLD_KEY_ACTIVATE,
} ns_scaffold_key;

int  ns_scaffold_init(ns_scaffold_context** out_ctx);
void ns_scaffold_shutdown(ns_scaffold_context* ctx);

ns_scaffold_window* ns_scaffold_create_window(ns_scaffold_context* ctx, int width, int height);
void                ns_scaffold_destroy_window(ns_scaffold_window* window);

void ns_scaffold_set_document(ns_scaffold_window* window,
                              const char*         url,
                              const char*         title,
                              const char*         body_text);
void ns_scaffold_send_key(ns_scaffold_window* window, ns_scaffold_key key, int pressed);
void ns_scaffold_send_pointer(ns_scaffold_window* window, int x, int y, int pressed);
void ns_scaffold_render_rgba(ns_scaffold_window* window,
                             uint32_t*           pixels,
                             int                 width,
                             int                 height,
                             int                 stride_pixels);

#ifdef __cplusplus
}
#endif
