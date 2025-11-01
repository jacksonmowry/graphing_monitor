#pragma once

#include <stddef.h>
typedef struct Renderer {
    void* state;

    size_t height;
    size_t width;

    void (*draw_line)(void* state, size_t x1, size_t y1, size_t x2, size_t y2);
    void (*render)(void* state, double min, double max, size_t x_max);
    void (*cleanup)(void* state);
} Renderer;
