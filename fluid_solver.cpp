#include "fluid_solver.h"

#include <immintrin.h>

#include <cmath>

#define IX(i, j, k) ((i) + (M + 2) * (j) + (M + 2) * (N + 2) * (k))
#define SWAP(x0, x)      \
    {                    \
        float *tmp = x0; \
        x0 = x;          \
        x = tmp;         \
    }
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define LINEARSOLVERTIMES 20

// Add sources (density or velocity)
void add_source(const int M, const int N, const int O, float *x, const float *s,
                const float dt) {
    int size = (M + 2) * (N + 2) * (O + 2);
    for (int i = 0; i < size; i++) {
        x[i] += dt * s[i];
    }
}

// Dei inline a esta funcao pq estamos a dar 13400 calls no ciclo da lin_solve e
// a encher a stack de lixo
// Set boundary conditions
inline void set_bnd(const int M, const int N, const int O, const int b,
                    float *x) {
    int i, j;


    // Set boundary on faces
    for (j = 1; j <= M; j++) {
        for (i = 1; i <= N; i++) {
            x[IX(i, j, 0)] = b == 3 ? -x[IX(i, j, 1)] : x[IX(i, j, 1)];
            x[IX(i, j, O + 1)] = b == 3 ? -x[IX(i, j, O)] : x[IX(i, j, O)];
        }
    }
    for (j = 1; j <= N; j++) {
        for (i = 1; i <= O; i++) {
            x[IX(0, i, j)] = b == 1 ? -x[IX(1, i, j)] : x[IX(1, i, j)];
            x[IX(M + 1, i, j)] = b == 1 ? -x[IX(M, i, j)] : x[IX(M, i, j)];
        }
    }
    for (j = 1; j <= M; j++) {
        for (i = 1; i <= O; i++) {
            x[IX(i, 0, j)] = b == 2 ? -x[IX(i, 1, j)] : x[IX(i, 1, j)];
            x[IX(i, N + 1, j)] = b == 2 ? -x[IX(i, N, j)] : x[IX(i, N, j)];
        }
    }

    // Set corners
    x[IX(0, 0, 0)] = 0.33f * (x[IX(1, 0, 0)] + x[IX(0, 1, 0)] + x[IX(0, 0, 1)]);
    x[IX(M + 1, 0, 0)] =
        0.33f * (x[IX(M, 0, 0)] + x[IX(M + 1, 1, 0)] + x[IX(M + 1, 0, 1)]);
    x[IX(0, N + 1, 0)] =
        0.33f * (x[IX(1, N + 1, 0)] + x[IX(0, N, 0)] + x[IX(0, N + 1, 1)]);
    x[IX(M + 1, N + 1, 0)] = 0.33f * (x[IX(M, N + 1, 0)] + x[IX(M + 1, N, 0)] +
                                      x[IX(M + 1, N + 1, 1)]);
}


// Linear solve for implicit methods (diffusion)
void lin_solve(const int M, const int N, const int O, const int b, float *x,
               const float *x0, const float a, const float c) {
    const int fat_cycles = O >> 3;
    const int m2 = M + 2;
    const int m2_n2 = m2 * (N + 2);
    const __m256 a_vec = _mm256_set1_ps(a);
    const __m256 c_vec = _mm256_set1_ps(c);

    for (int l = 0; l < LINEARSOLVERTIMES; l++) {
        for (int k = 1; k <= M; k++) {
            for (int j = 1; j <= N; j++) {
                int index = IX(1, j, k);
                int i = 0;
                for (; i < fat_cycles; i++) {
                    __m256 sum, other;
                    int off_index;

                    off_index = index - 1;
                    sum = _mm256_loadu_ps(&x[off_index]);

                    off_index = index + 1;
                    other = _mm256_loadu_ps(&x[off_index]);
                    sum = _mm256_add_ps(sum, other);

                    off_index = index - m2;
                    other = _mm256_loadu_ps(&x[off_index]);
                    sum = _mm256_add_ps(sum, other);

                    off_index = index + m2;
                    other = _mm256_loadu_ps(&x[off_index]);
                    sum = _mm256_add_ps(sum, other);

                    off_index = index - m2_n2;
                    other = _mm256_loadu_ps(&x[off_index]);
                    sum = _mm256_add_ps(sum, other);

                    off_index = index + m2_n2;
                    other = _mm256_loadu_ps(&x[off_index]);
                    sum = _mm256_add_ps(sum, other);

                    sum = _mm256_mul_ps(sum, a_vec);

                    other = _mm256_loadu_ps(&x0[index]);
                    sum = _mm256_add_ps(sum, other);

                    sum = _mm256_div_ps(sum, c_vec);

                    _mm256_storeu_ps(&x[index], sum);

                    index += 8;
                }
                for (i <<= 3; i <= O; i++) {
                    float r =
                        (x[index - 1] + x[index + 1] + x[index - (M + 2)] +
                         x[index + (M + 2)] + x[index - (M + 2) * (N + 2)] +
                         x[index + (M + 2) * (N + 2)]);
                    x[index] = (x0[index] + (a * r)) / c;
                    ++index;
                }
            }
        }
        set_bnd(M, N, O, b, x);
    }
}


// Diffusion step (uses implicit method)
inline void diffuse(const int M, const int N, const int O, const int b,
                    float *x, float *x0, const float diff, const float dt) {
    const int max = MAX(MAX(M, N), O);
    const float a = dt * diff * max * max;
    lin_solve(M, N, O, b, x, x0, a, 1 + 6 * a);
}

// TODO: Facil vetorizar
// Advection step (uses velocity field to move quantities)
void advect(const int M, const int N, const int O, const int b, float *d,
            const float *d0, const float *u, const float *v, const float *w,
            const float dt) {
    const float dtX = dt * M, dtY = dt * N, dtZ = dt * O;

    for (int k = 1; k <= M; k++) {
        for (int j = 1; j <= N; j++) {
            int index = IX(1, j, k);
            for (int i = 1; i <= O; i++) {
                float x = i - dtX * u[index];
                float y = j - dtY * v[index];
                float z = k - dtZ * w[index];

                // Clamp to grid boundaries

                if (x < 0.5f)
                    x = 0.5f;
                else if (x > M + 0.5f)
                    x = M + 0.5f;
                if (y < 0.5f)
                    y = 0.5f;
                else if (y > N + 0.5f)
                    y = N + 0.5f;
                if (z < 0.5f)
                    z = 0.5f;
                else if (z > O + 0.5f)
                    z = O + 0.5f;

                int i0 = (int)x, i1 = i0 + 1;
                int j0 = (int)y, j1 = j0 + 1;
                int k0 = (int)z, k1 = k0 + 1;

                float s1 = x - i0, s0 = 1 - s1;
                float t1 = y - j0, t0 = 1 - t1;
                float u1 = z - k0, u0 = 1 - u1;

                // TODO: SIMD
                d[index] = s0 * (t0 * (u0 * d0[IX(i0, j0, k0)] +
                                       u1 * d0[IX(i0, j0, k1)]) +
                                 t1 * (u0 * d0[IX(i0, j1, k0)] +
                                       u1 * d0[IX(i0, j1, k1)])) +
                           s1 * (t0 * (u0 * d0[IX(i1, j0, k0)] +
                                       u1 * d0[IX(i1, j0, k1)]) +
                                 t1 * (u0 * d0[IX(i1, j1, k0)] +
                                       u1 * d0[IX(i1, j1, k1)]));
                index++;
            }
        }
    }
    set_bnd(M, N, O, b, d);
}

// Projection step to ensure incompressibility (make the velocity field
// divergence-free)
void project(const int M, const int N, const int O, float *u, float *v,
             float *w, float *p, float *div) {
    for (int k = 1; k <= M; k++) {
        for (int j = 1; j <= N; j++) {
            for (int i = 1; i <= O; i++) {
                div[IX(i, j, k)] = -0.5f *
                                   (u[IX(i + 1, j, k)] - u[IX(i - 1, j, k)] +
                                    v[IX(i, j + 1, k)] - v[IX(i, j - 1, k)] +
                                    w[IX(i, j, k + 1)] - w[IX(i, j, k - 1)]) /
                                   MAX(M, MAX(N, O));
                p[IX(i, j, k)] = 0;
            }
        }
    }

    set_bnd(M, N, O, 0, div);
    set_bnd(M, N, O, 0, p);
    lin_solve(M, N, O, 0, p, div, 1, 6);

    // TODO:SIMD
    for (int k = 1; k <= M; k++) {
        for (int j = 1; j <= N; j++) {
            for (int i = 1; i <= O; i++) {
                u[IX(i, j, k)] -=
                    0.5f * (p[IX(i + 1, j, k)] - p[IX(i - 1, j, k)]);
                v[IX(i, j, k)] -=
                    0.5f * (p[IX(i, j + 1, k)] - p[IX(i, j - 1, k)]);
                w[IX(i, j, k)] -=
                    0.5f * (p[IX(i, j, k + 1)] - p[IX(i, j, k - 1)]);
            }
        }
    }
    set_bnd(M, N, O, 1, u);
    set_bnd(M, N, O, 2, v);
    set_bnd(M, N, O, 3, w);
}

// Step function for density
void dens_step(const int M, const int N, const int O, float *x, float *x0,
               float *u, float *v, float *w, const float diff, const float dt) {
    add_source(M, N, O, x, x0, dt);
    SWAP(x0, x);
    diffuse(M, N, O, 0, x, x0, diff, dt);
    SWAP(x0, x);
    advect(M, N, O, 0, x, x0, u, v, w, dt);
}

// Step function for velocity
void vel_step(const int M, const int N, const int O, float *u, float *v,
              float *w, float *u0, float *v0, float *w0, const float visc,
              const float dt) {
    add_source(M, N, O, u, u0, dt);
    add_source(M, N, O, v, v0, dt);
    add_source(M, N, O, w, w0, dt);
    SWAP(u0, u);
    diffuse(M, N, O, 1, u, u0, visc, dt);
    SWAP(v0, v);
    diffuse(M, N, O, 2, v, v0, visc, dt);
    SWAP(w0, w);
    diffuse(M, N, O, 3, w, w0, visc, dt);
    project(M, N, O, u, v, w, u0, v0);
    SWAP(u0, u);
    SWAP(v0, v);
    SWAP(w0, w);
    advect(M, N, O, 1, u, u0, u0, v0, w0, dt);
    advect(M, N, O, 2, v, v0, u0, v0, w0, dt);
    advect(M, N, O, 3, w, w0, u0, v0, w0, dt);
    project(M, N, O, u, v, w, u0, v0);
}
