#include "dataset.h"

#include "cube.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static uint64_t next_random(uint64_t *state) {
    /* xorshift64*: deterministic and sufficient for synthetic data generation. */
    uint64_t x = *state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *state = x;
    return x * UINT64_C(2685821657736338717);
}

int dataset_generate(const char *path, size_t count, size_t scramble_length,
                     uint64_t seed) {
    FILE *file = fopen(path, "w");
    if (!file) {
        perror(path);
        return 0;
    }
    fprintf(file, "state,scramble\n");
    uint64_t rng = seed ? seed : UINT64_C(1);
    for (size_t sample = 0; sample < count; ++sample) {
        Cube cube;
        cube_init(&cube);
        char state[CUBE_STICKER_COUNT + 1];
        if (scramble_length > (SIZE_MAX - 1) / 4) {
            fclose(file);
            return 0;
        }
        char *scramble = malloc(scramble_length * 4 + 1);
        if (!scramble) {
            fclose(file);
            return 0;
        }
        size_t used = 0;
        int previous_face = -1;
        for (size_t step = 0; step < scramble_length; ++step) {
            int face;
            do { face = (int)(next_random(&rng) % CUBE_FACE_COUNT); }
            while (face == previous_face);
            int move = face * 3 + (int)(next_random(&rng) % 3);
            cube_apply_move(&cube, move);
            if (step) scramble[used++] = ' ';
            const char *name = cube_move_name(move);
            while (*name) scramble[used++] = *name++;
            previous_face = face;
        }
        scramble[used] = '\0';
        cube_encode(&cube, state);
        fprintf(file, "%s,\"%s\"\n", state, scramble);
        free(scramble);
    }
    int ok = !ferror(file);
    if (fclose(file) != 0) ok = 0;
    return ok;
}
