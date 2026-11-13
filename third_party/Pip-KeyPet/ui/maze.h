/* A maze and a ball rolling in it: generation and physics, pure C (the app in apps.c draws it, sim/maze_test.c
   checks it). Coordinates are px in the maze's square, (0, 0) its top left, y down. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define MAZE_MAX 12

typedef struct {
    int n;                             /* cells a side */
    float c, t, r;                     /* cell size, wall thickness, ball radius (px) */
    uint8_t right[MAZE_MAX][MAZE_MAX]; /* a wall between cell (i, j) and (i + 1, j)... */
    uint8_t down[MAZE_MAX][MAZE_MAX];  /* ...and between (i, j) and (i, j + 1); [i][j] is column i, row j */
    float x, y, vx, vy;                /* the ball */
} maze_t;

/* A new maze, one way between any two cells (a depth-first carve), the ball in the top left cell. */
void maze_new(maze_t *m, int n, float side, uint32_t seed);
/* Roll the ball for dt seconds pushed by (ax, ay) px/s^2: friction, small bounces off the walls, never through one. */
void maze_step(maze_t *m, float ax, float ay, float dt);
bool maze_done(const maze_t *m); /* the ball has reached the bottom right cell */
/* Is there a wall on this side of cell (i, j)? side: 0 right, 1 down, 2 left, 3 up; the rim counts. */
bool maze_wall(const maze_t *m, int i, int j, int side);
/* The walls as rectangles (x0, y0, x1, y1) for drawing; returns how many were written, at most max. */
int maze_rects(const maze_t *m, float (*rects)[4], int max);
