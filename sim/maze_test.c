/* Maze checks: every maze is perfect, a ball steered along the way gets out, and nothing shakes it through a wall.
   Run by sim/check.py. */
#include "maze.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(c, ...) do { if (!(c)) { fprintf(stderr, "FAIL: " __VA_ARGS__); fputc('\n', stderr); exit(1); } } while (0)

static int path[MAZE_MAX * MAZE_MAX][2], path_n, moves;

/* breadth first from the start: every cell reached, n*n - 1 open sides, and the way to the goal */
static void solve(const maze_t *m)
{
    int n = m->n, from[MAZE_MAX][MAZE_MAX], qi[MAZE_MAX * MAZE_MAX], qj[MAZE_MAX * MAZE_MAX], h = 0, t = 0, reached = 0, open = 0;
    static const int di[4] = {1, 0, -1, 0}, dj[4] = {0, 1, 0, -1};
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) from[i][j] = -1, open += !maze_wall(m, i, j, 0) + !maze_wall(m, i, j, 1);
    CHECK(open == n * n - 1, "n=%d: %d passages, a perfect maze has %d", n, open, n * n - 1);
    from[0][0] = 4, qi[t] = 0, qj[t++] = 0;
    while (h < t) {
        int i = qi[h], j = qj[h++];
        reached++;
        for (int d = 0; d < 4; d++)
            if (!maze_wall(m, i, j, d) && from[i + di[d]][j + dj[d]] < 0) from[i + di[d]][j + dj[d]] = d, qi[t] = i + di[d], qj[t++] = j + dj[d];
    }
    CHECK(reached == n * n, "n=%d: %d of %d cells reachable", n, reached, n * n);
    path_n = 0;
    for (int i = n - 1, j = n - 1; from[i][j] != 4;) {
        path[path_n][0] = i, path[path_n++][1] = j;
        int d = from[i][j];
        i -= di[d], j -= dj[d];
    }
}

/* the ball's cell may only change to a neighbour through an open side */
static void cell_moves(const maze_t *m, int *ci, int *cj, const char *what)
{
    int i = (int)floorf(m->x / m->c), j = (int)floorf(m->y / m->c);
    CHECK(i >= 0 && j >= 0 && i < m->n && j < m->n, "%s: the ball left the maze (%.1f, %.1f)", what, m->x, m->y);
    if (i == *ci && j == *cj) return;
    int d = i == *ci + 1 && j == *cj ? 0 : j == *cj + 1 && i == *ci ? 1 : i == *ci - 1 && j == *cj ? 2 : j == *cj - 1 && i == *ci ? 3 : -1;
    CHECK(d >= 0 && !maze_wall(m, *ci, *cj, d), "%s: through a wall from (%d,%d) to (%d,%d)", what, *ci, *cj, i, j);
    *ci = i, *cj = j, moves++;
}

int main(void)
{
    static const int sizes[] = {6, 8, 10, 12};
    uint32_t rng = 12345;
    for (int s = 0; s < 4; s++)
        for (uint32_t seed = 1; seed <= 12; seed++) {
            maze_t m;
            maze_new(&m, sizes[s], 308, seed * 7919);
            solve(&m);
            /* steered: tilt toward the next cell of the way, as a hand would, never more than about one g */
            int ci = 0, cj = 0, k = path_n - 1;
            float t = 0;
            for (; t < 120 && !maze_done(&m); t += 1 / 60.0f) {
                while (k > 0 && floorf(m.x / m.c) == path[k][0] && floorf(m.y / m.c) == path[k][1]) k--;
                float tx = (path[k][0] + 0.5f) * m.c, ty = (path[k][1] + 0.5f) * m.c;
                float ax = (tx - m.x) * 40 - m.vx * 9, ay = (ty - m.y) * 40 - m.vy * 9, a = sqrtf(ax * ax + ay * ay);
                if (a > 1100) ax *= 1100 / a, ay *= 1100 / a;
                maze_step(&m, ax, ay, 1 / 60.0f);
                cell_moves(&m, &ci, &cj, "steered");
            }
            CHECK(maze_done(&m), "n=%d seed %u: the steered ball did not get out in 2 min", sizes[s], seed);
            /* shaken: hard random pushes, slow frames now and then, the ball always going the legal way */
            maze_new(&m, sizes[s], 308, seed);
            ci = cj = 0, moves = 0;
            for (int f = 0; f < 3000; f++) {
                float ax = ((int)(rng = rng * 1664525u + 1013904223u) >> 8) % 6000, ay = ((int)(rng = rng * 1664525u + 1013904223u) >> 8) % 6000;
                maze_step(&m, ax, ay, f % 7 ? 1 / 60.0f : 0.05f); /* the app clamps a frame to 50 ms: at most 0.6 cells */
                cell_moves(&m, &ci, &cj, "shaken");
            }
            CHECK(moves > 30, "n=%d seed %u: the shaken ball hardly moved (%d cells)", sizes[s], seed, moves);
        }
    puts("maze_test ok");
    return 0;
}
