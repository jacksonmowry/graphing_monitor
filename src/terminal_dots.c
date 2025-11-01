#include "terminal_dots.h"
#include <assert.h>
#include <locale.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

typedef struct TDState {
    uint8_t* buffer;
    size_t buffer_height;
    size_t buffer_width;

    size_t height;
    size_t width;
} TDState;

double slope(double x1, double y1, double x2, double y2) {
    double bottom = x2 - x1;
    if (bottom == 0) {
        return 0;
    }

    return (y2 - y1) / (x2 - x1);
}

static size_t bit_mapping(size_t idx) {
    switch (idx) {
    case 0:
        return 0;
    case 1:
        return 1;
    case 2:
        return 2;
    case 3:
        return 4;
    case 4:
        return 5;
    case 5:
        return 6;
    case 6:
        return 4;
    case 7:
        return 7;
    }

    return 0;
}

static void set_pixel(void* state, size_t x, size_t y) {
    TDState* s = (TDState*)state;

    size_t byte = ((y / 4) * s->buffer_width) + (x / 2);
    size_t idx = bit_mapping((y % 4) * ((x % 2) * 4));

    if (byte >= s->buffer_height * s->buffer_width) {
        /* fprintf(stderr, */
        /*         "error: Attempting to set pixel with coordinates out of " */
        /*         "bounds\nSetting (%zu, %zu) (%zu, %zu), with max coords of "
         */
        /*         "(%zu, %zu)\n", */
        /*         x, y, x / 2, y / 4, s->buffer_width - 1, s->buffer_height -
         * 1); */
        /* exit(1); */
        return;
    }

    s->buffer[byte] |= (1 << idx);
}

static void draw_line(void* state, size_t x1, size_t y1, size_t x2, size_t y2) {
    TDState* s = (TDState*)state;

    double x1f = x1;
    double y1f = y1;
    double x2f = x2;
    double y2f = y2;
    bool steep = fabs(y2f - y1f) > fabs(x2f - x1f);

    if (steep) {
        double tmp = x1f;
        x1f = y1f;
        y1f = tmp;

        tmp = x2f;
        x2f = y2f;
        y2f = tmp;
    }

    if (x1f > x2f) {
        double tmp = x1f;
        x1f = x2f;
        x2f = tmp;

        tmp = y1f;
        y1f = y2f;
        y2f = tmp;
    }

    const double dx = x2f - x1f;
    const double dy = y2f - y1f;
    const double gradient = dx == 0 ? 1 : dy / dx;

    int xpx11 = 0;
    double intery = 0;
    {
        double xend = round(x1f);
        double yend = y1f + gradient * (xend - x1f);
        double xgap = 1 - rint(x1f + 0.5);
        xpx11 = xend;
        double ypx11 = rint(yend);

        if (steep) {
            set_pixel(s, xpx11, ypx11);
            set_pixel(s, xpx11, ypx11 + 1);
        } else {
            set_pixel(s, ypx11, xpx11);
            set_pixel(s, ypx11 + 1, xpx11);
        }

        intery = yend + gradient;
    }

    int xpx12 = 0;
    {
        double xend = round(x2f);
        double yend = y2f + gradient * (xend - x2f);
        double xgap = 1 - rint(x2f + 0.5);
        xpx12 = xend;
        double ypx12 = rint(yend);

        if (steep) {
            set_pixel(s, xpx12, ypx12);
            set_pixel(s, xpx12, ypx12 + 1);
        } else {
            set_pixel(s, ypx12, xpx12);
            set_pixel(s, ypx12 + 1, xpx12);
        }
    }

    if (steep) {
        for (size_t x = xpx11 + 1; x < xpx12; x++) {
            set_pixel(s, x, rint(intery));
            set_pixel(s, x, rint(intery) + 1);
            intery += gradient;
        }
    } else {
        for (size_t x = xpx11 + 1; x < xpx12; x++) {
            set_pixel(s, rint(intery), x);
            set_pixel(s, rint(intery) + 1, x);
            intery += gradient;
        }
    }
}

static void render(void* state, double min, double max, size_t x_max) {
    TDState* s = (TDState*)state;
    wchar_t buf[s->buffer_height * s->buffer_width + s->buffer_height];
    size_t buf_len = 0;

    double range = max - min;
    double line_range = range / s->buffer_height;

    for (ssize_t row = s->buffer_height - 1; row >= 0; row--) {
        size_t offset = row * s->buffer_width;
        for (size_t col = 0; col < s->buffer_width; col++) {
            wchar_t base_braille = L'\u2800';
            base_braille |= s->buffer[offset + col];

            buf[buf_len++] = base_braille;
        }
        buf[buf_len++] = '\0';
    }
    char x[s->buffer_width + 28];
    memset(x, ' ', s->buffer_width);
    int written = sprintf(x + 14, "0");
    x[14 + written] = ' ';
    written =
        sprintf(x + 14 + ((s->buffer_width - 14) / 2) - 4, "%8zu", x_max / 2);
    x[14 + ((s->buffer_width - 14) / 2) - 4 + written] = ' ';
    sprintf(x + (s->buffer_width), "%8zu", x_max);
    x[s->buffer_width + 28 - 1] = '\0';

    printf("\033[2J\033[H");
    for (size_t i = 0; i < s->buffer_height; i++) {
        if (i % 2 == 0 || i == s->buffer_height - 1) {
            printf("%12.2f |", max - (i * line_range));
        } else {
            printf("%12c |", ' ');
        }
        printf("%ls\n", buf + ((s->buffer_width + 1) * i));
    }
    puts(x);
    fflush(stdout);

    memset(s->buffer, 0, s->buffer_height * s->buffer_width);
}

void td_free(void* state) {
    TDState* s = (TDState*)state;

    free(s->buffer);
    free(s);
}

Renderer td_init(size_t width, size_t height) {
    setlocale(LC_CTYPE, "en_US.UTF-8"); // Set locale to support UTF-8

    TDState* s = (TDState*)calloc(sizeof(TDState), 1);
    s->buffer_height = (height + 3) / 4;
    s->buffer_width = (width + 1) / 2;
    s->buffer = calloc(((height + 3) / 4) * ((width + 1) / 2), 1);
    s->height = height;
    s->width = width;

    return (Renderer){
        .state = s,

        .height = height,
        .width = width,

        .draw_line = draw_line,
        .render = render,
        .cleanup = td_free,
    };
}
