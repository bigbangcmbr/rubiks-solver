#ifndef RUBIKS_CUBE_H
#define RUBIKS_CUBE_H

#include <stddef.h>

#define CUBE_FACE_COUNT 6
#define CUBE_FACE_SIZE 9
#define CUBE_STICKER_COUNT (CUBE_FACE_COUNT * CUBE_FACE_SIZE)
#define CUBE_MOVE_COUNT 18

/* Face order and move notation: U, R, F, D, L, B; suffix 2 or ' as usual. */
struct Cube {
    char stickers[CUBE_STICKER_COUNT];
};

void cube_init(Cube *cube);
void cube_apply_move(Cube *cube, int move);
int cube_is_solved(const Cube *cube);
void cube_encode(const Cube *cube, char out[CUBE_STICKER_COUNT + 1]);
const char *cube_move_name(int move);

#endif
