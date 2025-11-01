#include "renderer.h"
#include "terminal_dots.h"
#include "timespec.h"
#include <asm-generic/ioctls.h>
#include <bits/time.h>
#include <float.h>
#include <getopt.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

const char* backends[] = {"terminal"};
const size_t backends_len = sizeof(backends);

size_t get_pixel(double val, double bin_width, double min, size_t max) {
    size_t ret = floor((val - min) / bin_width);
    if (ret > max) {
        ret = max;
    }

    return ret;
}

typedef struct WorkerArgs {
    float** data;
    size_t* data_len;

    size_t datapoints;
    char* backend;
    size_t fps;
    bool debug;
} WorkerArgs;

static void cleanup(void* arg) {
    Renderer* r = (Renderer*)arg;

    r->cleanup(r->state);
}

void* worker(void* arg) {
    WorkerArgs* args = (WorkerArgs*)arg;
    size_t frame_misses = 0;
    size_t working_index = 0;

    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);

    Renderer r = td_init((w.ws_col - 32) * 2, (w.ws_row - 4) * 4);

    pthread_cleanup_push(cleanup, &r);

    float min = DBL_MAX;
    float max = DBL_MIN;

    struct timespec frame_time = timespec_from_double(1 / (double)args->fps);

    while (true) {
        struct timespec frame_start = {0};
        if (clock_gettime(CLOCK_REALTIME, &frame_start) == -1) {
            perror("clock_gettime");
            exit(1);
        }
        struct timespec next_frame = timespec_add(frame_start, frame_time);

        // Render thing
        size_t new_index = *args->data_len;

        if (new_index == working_index) {
            goto SLEEP;
        }
        for (size_t i = working_index; i < new_index; i++) {
            for (size_t j = 0; j < args->datapoints; j++) {
                min = args->data[j][i] < min ? args->data[j][i] : min;
                max = args->data[j][i] > max ? args->data[j][i] : max;
            }
        }

        const double y_range = max - min;
        const double y_pixel_width = y_range / r.height;
        const double x_range = new_index;
        const double x_pixel_width = x_range / r.width;

        for (size_t i = 0; i < args->datapoints; i++) {
            for (size_t j = 0; j < new_index - 1; j++) {
                const size_t x1 = get_pixel(j, x_pixel_width, 0, r.width - 1);
                const size_t y1 = get_pixel(args->data[i][j], y_pixel_width,
                                            min, r.height - 1);
                const size_t x2 =
                    get_pixel(j + 1, x_pixel_width, 0, r.width - 1);
                const size_t y2 = get_pixel(args->data[i][j + 1], y_pixel_width,
                                            min, r.height - 1);

                r.draw_line(r.state, y1, x1, y2, x2);
            }
        }

        r.render(r.state, min, max, new_index);

        working_index = new_index;

        // Sleep to match desired fps
    SLEEP:;
        struct timespec frame_end = {0};
        if (clock_gettime(CLOCK_REALTIME, &frame_end) == -1) {
            perror("clock_gettime");
            exit(1);
        }

        struct timespec sleep_length = timespec_sub(next_frame, frame_end);
        nanosleep(&sleep_length, NULL);
    }

    pthread_cleanup_pop(1);
    return NULL;
}

int main(int argc, char* argv[]) {
    size_t fps = -1;
    char* backend = NULL;
    size_t datapoints = 1;

    int c;
    int digit_optind = 0;

    while (true) {
        const int this_option_optind = optind ? optind : 1;
        int option_index = 0;
        static struct option long_options[] = {
            {"fps", required_argument, 0, 'f'},
            {"backend", required_argument, 0, 'b'},
            {"datapoints", optional_argument, 0, 'd'},
            {0, 0, 0, 0},
        };

        c = getopt_long(argc, argv, "f:b:d:", long_options, &option_index);

        if (c == -1) {
            break;
        }

        switch (c) {
        case 0:
            printf("options %s", long_options[option_index].name);
            if (optarg) {
                printf(" with arg %s", optarg);
            }
            printf("\n");
            break;
        case 'f':
            fps = strtoull(optarg, NULL, 0);
            break;
        case 'b':
            backend = optarg;
            if (strcmp(backend, "terminal")) {
                fprintf(stderr, "Only [");
                for (size_t i = 0; i < backends_len; i++) {
                    fprintf(stderr, "%s", backends[i]);

                    if (i != backends_len - 1) {
                        fprintf(stderr, ", ");
                    }
                }
                fprintf(stderr, "] are suppported.\n");
            }
            break;
        case 'd':
            datapoints = strtoull(optarg, NULL, 0);
        case '?':
            break;
        default:
            printf("?? getopt returned character code 0%o ??\n", c);
        }
    }

    bool error = false;
    if (fps == -1) {
        fprintf(stderr, "error: FPS must be provided\n");
        error = true;
    }
    if (backend == NULL) {
        fprintf(stderr, "error: Backend must be provided\n");
        error = true;
    }

    if (error) {
        exit(1);
    }

    // Allocate room for data, we just use one index since all arrays are of
    // equal length
    float* data[datapoints];
    size_t data_len = 0;
    for (size_t i = 0; i < datapoints; i++) {
        data[i] = mmap(NULL, 1 << 24, PROT_WRITE | PROT_READ,
                       MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
    }

    // Make bg thread
    pthread_t thread;
    WorkerArgs wa = (WorkerArgs){.data = data,
                                 .data_len = &data_len,

                                 .datapoints = datapoints,
                                 .backend = backend,
                                 .fps = fps};
    pthread_create(&thread, NULL, worker, &wa);

    char line[1024] = {0};
    while (fgets(line, sizeof(line), stdin)) {

        char* line_ptr = line + 0;
        char* token = strtok(line_ptr, ",");
        size_t line_idx = 0;

        do {
            data[line_idx][data_len] = strtof(token, NULL);

            line_idx++;
        } while ((token = strtok(NULL, ",")));

        data_len++;
    }

    struct timespec final_sleep = timespec_from_double(1);
    nanosleep(&final_sleep, NULL);
    pthread_cancel(thread);
    pthread_join(thread, NULL);
}
