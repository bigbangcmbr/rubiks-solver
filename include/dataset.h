#ifndef RUBIKS_DATASET_H
#define RUBIKS_DATASET_H

#include <stddef.h>
#include <stdint.h>

/* Write synthetic states and their generating scrambles to a CSV file. */
int dataset_generate(const char *path, size_t count, size_t scramble_length,
                     uint64_t seed);

#endif
