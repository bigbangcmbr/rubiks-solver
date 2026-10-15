#include "dataset.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *program) {
    fprintf(stderr, "Usage: %s generate <output.csv> <count> <scramble-length> [seed]\n", program);
}

static int parse_size(const char *text, size_t *value) {
    char *end = NULL;
    errno = 0;
    unsigned long long n = strtoull(text, &end, 10);
    if (errno || !end || *end || n > (unsigned long long)SIZE_MAX) return 0;
    *value = (size_t)n;
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 5 || argc > 6 || strcmp(argv[1], "generate") != 0) {
        usage(argv[0]);
        return 2;
    }
    size_t count, length;
    if (!parse_size(argv[3], &count) || !parse_size(argv[4], &length)) {
        fprintf(stderr, "count and scramble-length must be non-negative integers\n");
        return 2;
    }
    uint64_t seed = UINT64_C(1);
    if (argc == 6) {
        char *end = NULL;
        errno = 0;
        seed = strtoull(argv[5], &end, 10);
        if (errno || !end || *end) {
            fprintf(stderr, "seed must be an unsigned integer\n");
            return 2;
        }
    }
    if (!dataset_generate(argv[2], count, length, seed)) return 1;
    printf("Wrote %zu synthetic samples to %s (seed=%" PRIu64 ")\n", count, argv[2], seed);
    return 0;
}
