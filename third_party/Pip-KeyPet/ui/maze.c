#include "maze.h"
#include <math.h>
#include <string.h>

#define FRICTION 1.4f /* 1/s: speed lost to rolling */
#define BOUNCE 0.25f  /* of the speed into a wall that comes back */

static uint32_t next(uint32_t *s) /* xorshift32 */
{
    *s ^= *s << 13, *s ^= *s >> 17, *s ^= *s << 5;
    return *s;
}

bool maze_wall(const maze_t *m, int i, int j, int side)
{
    switch (side) {
    case 0: return i >= m->n - 1 || m->right[i][j];
    case 1: return j >= m->n - 1 || m->down[i][j];
    case 2: return i <= 0 || m->right[i - 1][j];
    default: return j <= 0 || m->down[i][j - 1];
    }
}

void maze_new(maze_t *m, int n, float side, uint32_t seed)
{
    memset(m, 0, sizeof *m);
    m->n = n < 2 ? 2 : n > MAZE_MAX ? MAZE_MAX : n;
    n = m->n;
    m->c = side / n;
    m->t = m->c > 30 ? 5 : 4;
    m->r = fminf(m->c * 0.3f, 11);
    memset(m->right, 1, sizeof m->right), memset(m->down, 1, sizeof m->down);
    /* depth-first: walk to a random unvisited neighbour knocking the wall down, back up when stuck */
    uint32_t s = seed ? seed : 0x2545F491u;
    uint8_t seen[MAZE_MAX][MAZE_MAX] = {{0}}, stack[MAZE_MAX * MAZE_MAX][2];
    int top = 0;
    stack[top][0] = 0, stack[top][1] = 0, top++, seen[0][0] = 1;
    while (top) {
        int i = stack[top - 1][0], j = stack[top - 1][1], opts[4], k = 0;
        static const int di[4] = {1, 0, -1, 0}, dj[4] = {0, 1, 0, -1};
        for (int d = 0; d < 4; d++) {
            int a = i + di[d], b = j + dj[d];
            if (a >= 0 && a < n && b >= 0 && b < n && !seen[a][b]) opts[k++] = d;
        }
        if (!k) { top--; continue; }
        int d = opts[next(&s) % k], a = i + di[d], b = j + dj[d];
        if (d == 0) m->right[i][j] = 0;
        else if (d == 1) m->down[i][j] = 0;
        else if (d == 2) m->right[a][b] = 0;
        else m->down[a][b] = 0;
        seen[a][b] = 1;
        stack[top][0] = (uint8_t)a, stack[top][1] = (uint8_t)b, top++;
    }
    m->x = m->y = m->c * 0.5f;
}

/* the walls of cell (i, j)'s right and lower sides, and the rim on the left and the top for the first ones */
static int cell_rects(const maze_t *m, int i, int j, float (*r)[4])
{
    float c = m->c, h = m->t * 0.5f, x = i * c, y = j * c;
    int k = 0;
    if (maze_wall(m, i, j, 0)) r[k][0] = x + c - h, r[k][1] = y - h, r[k][2] = x + c + h, r[k][3] = y + c + h, k++;
    if (maze_wall(m, i, j, 1)) r[k][0] = x - h, r[k][1] = y + c - h, r[k][2] = x + c + h, r[k][3] = y + c + h, k++;
    if (i == 0) r[k][0] = -h, r[k][1] = y - h, r[k][2] = h, r[k][3] = y + c + h, k++;
    if (j == 0) r[k][0] = x - h, r[k][1] = -h, r[k][2] = x + c + h, r[k][3] = h, k++;
    return k;
}

int maze_rects(const maze_t *m, float (*rects)[4], int max)
{
    int k = 0;
    float r[4][4];
    for (int j = 0; j < m->n; j++)
        for (int i = 0; i < m->n; i++) {
            int got = cell_rects(m, i, j, r);
            for (int q = 0; q < got && k < max; q++) memcpy(rects[k++], r[q], sizeof r[q]);
        }
    return k;
}

/* push the ball out of the walls around its cell and take the speed into them away (a little comes back) */
static void collide(maze_t *m)
{
    int ci = (int)floorf(m->x / m->c), cj = (int)floorf(m->y / m->c);
    for (int pass = 0; pass < 2; pass++) /* a corner is two walls: the second pass settles what the first pushed into */
        for (int j = cj - 1; j <= cj + 1; j++)
            for (int i = ci - 1; i <= ci + 1; i++) {
                if (i < 0 || j < 0 || i >= m->n || j >= m->n) continue;
                float r[4][4];
                int k = cell_rects(m, i, j, r);
                for (int q = 0; q < k; q++) {
                    float qx = fmaxf(r[q][0], fminf(m->x, r[q][2])), qy = fmaxf(r[q][1], fminf(m->y, r[q][3]));
                    float dx = m->x - qx, dy = m->y - qy, d2 = dx * dx + dy * dy, nx, ny, pen;
                    if (d2 >= m->r * m->r) continue;
                    if (d2 > 1e-8f) {
                        float d = sqrtf(d2);
                        nx = dx / d, ny = dy / d, pen = m->r - d;
                    } else { /* the centre inside the wall: out the shortest way */
                        float l = m->x - r[q][0], rt = r[q][2] - m->x, u = m->y - r[q][1], dn = r[q][3] - m->y;
                        float best = fminf(fminf(l, rt), fminf(u, dn));
                        nx = best == l ? -1 : best == rt ? 1 : 0, ny = nx ? 0 : best == u ? -1 : 1;
                        pen = best + m->r;
                    }
                    m->x += nx * pen, m->y += ny * pen;
                    float vn = m->vx * nx + m->vy * ny;
                    if (vn < 0) m->vx -= (1 + BOUNCE) * vn * nx, m->vy -= (1 + BOUNCE) * vn * ny;
                }
            }
}

void maze_step(maze_t *m, float ax, float ay, float dt)
{
    m->vx += ax * dt, m->vy += ay * dt;
    float k = expf(-FRICTION * dt), vmax = m->c * 12;
    m->vx *= k, m->vy *= k;
    float v = sqrtf(m->vx * m->vx + m->vy * m->vy);
    if (v > vmax) m->vx *= vmax / v, m->vy *= vmax / v;
    /* small steps: never more than a third of the radius at a time, so no wall is ever jumped */
    int steps = (int)ceilf(v * dt / (m->r / 3)) + 1;
    if (steps > 64) steps = 64;
    for (int s = 0; s < steps; s++) {
        m->x += m->vx * dt / steps, m->y += m->vy * dt / steps;
        collide(m);
    }
}

bool maze_done(const maze_t *m)
{
    float g = (m->n - 0.5f) * m->c, dx = m->x - g, dy = m->y - g;
    return dx * dx + dy * dy < m->c * m->c * 0.1f;
}
