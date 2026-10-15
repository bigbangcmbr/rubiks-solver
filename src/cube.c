#include "cube.h"

#include <string.h>

/* Each facelet is represented by its cubie position and outward normal. */
typedef struct { int x, y, z; } Vec;
typedef struct { Vec pos, normal; } Sticker;

static const char face_letters[CUBE_FACE_COUNT] = {'U', 'R', 'F', 'D', 'L', 'B'};

static Sticker sticker_at(int face, int row, int col) {
    Sticker s;
    switch (face) {
    case 0: s.pos = (Vec){col - 1, 1, row - 1}; s.normal = (Vec){0, 1, 0}; break; /* U */
    case 1: s.pos = (Vec){1, 1 - row, 1 - col}; s.normal = (Vec){1, 0, 0}; break; /* R */
    case 2: s.pos = (Vec){col - 1, 1 - row, 1}; s.normal = (Vec){0, 0, 1}; break; /* F */
    case 3: s.pos = (Vec){col - 1, -1, 1 - row}; s.normal = (Vec){0, -1, 0}; break; /* D */
    case 4: s.pos = (Vec){-1, 1 - row, col - 1}; s.normal = (Vec){-1, 0, 0}; break; /* L */
    default:s.pos = (Vec){1 - col, 1 - row, -1}; s.normal = (Vec){0, 0, -1}; break; /* B */
    }
    return s;
}

static Vec rotate_quarter(Vec v, int axis, int direction) {
    /* direction is +1 or -1 for a quarter turn around the positive axis. */
    if (direction < 0) {
        if (axis == 0) return (Vec){v.x, v.z, -v.y};
        if (axis == 1) return (Vec){-v.z, v.y, v.x};
        return (Vec){v.y, -v.x, v.z};
    }
    if (axis == 0) return (Vec){v.x, -v.z, v.y};
    if (axis == 1) return (Vec){v.z, v.y, -v.x};
    return (Vec){-v.y, v.x, v.z};
}

static int same_vec(Vec a, Vec b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

static int sticker_index(Sticker needle) {
    for (int f = 0; f < CUBE_FACE_COUNT; ++f)
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) {
                int i = f * 9 + r * 3 + c;
                Sticker candidate = sticker_at(f, r, c);
                if (same_vec(needle.pos, candidate.pos) &&
                    same_vec(needle.normal, candidate.normal)) return i;
            }
    return -1;
}

void cube_init(Cube *cube) {
    for (int f = 0; f < CUBE_FACE_COUNT; ++f)
        memset(&cube->stickers[f * 9], face_letters[f], 9);
}

void cube_apply_move(Cube *cube, int move) {
    static const int axes[CUBE_FACE_COUNT] = {1, 0, 2, 1, 0, 2};
    static const int signs[CUBE_FACE_COUNT] = {1, 1, 1, -1, -1, -1};
    if (move < 0 || move >= CUBE_MOVE_COUNT) return;
    int face = move / 3;
    int turn = move % 3 == 0 ? 1 : (move % 3 == 1 ? 2 : 3);
    int axis = axes[face], sign = signs[face];
    Cube next = *cube;
    for (int i = 0; i < CUBE_STICKER_COUNT; ++i) {
        Sticker s = sticker_at(i / 9, (i % 9) / 3, i % 3);
        int coordinate = axis == 0 ? s.pos.x : (axis == 1 ? s.pos.y : s.pos.z);
        if (coordinate == sign) {
            int direction = -sign;
            for (int q = 0; q < turn; ++q) {
                s.pos = rotate_quarter(s.pos, axis, direction);
                s.normal = rotate_quarter(s.normal, axis, direction);
            }
        }
        int dest = sticker_index(s);
        if (dest >= 0) next.stickers[dest] = cube->stickers[i];
    }
    *cube = next;
}

int cube_is_solved(const Cube *cube) {
    for (int f = 0; f < CUBE_FACE_COUNT; ++f)
        for (int i = 1; i < 9; ++i)
            if (cube->stickers[f * 9 + i] != cube->stickers[f * 9]) return 0;
    return 1;
}

void cube_encode(const Cube *cube, char out[CUBE_STICKER_COUNT + 1]) {
    memcpy(out, cube->stickers, CUBE_STICKER_COUNT);
    out[CUBE_STICKER_COUNT] = '\0';
}

const char *cube_move_name(int move) {
    static const char *names[CUBE_MOVE_COUNT] = {
        "U", "U2", "U'", "R", "R2", "R'", "F", "F2", "F'",
        "D", "D2", "D'", "L", "L2", "L'", "B", "B2", "B'"
    };
    return move >= 0 && move < CUBE_MOVE_COUNT ? names[move] : "?";
}
