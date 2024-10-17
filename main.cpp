#include <mm_malloc.h>

#include <cstring>
#include <iostream>
#include <vector>

#include "EventManager.h"
#include "fluid_solver.h"

#define SIZE 42

#define IX(i, j, k) ((i) + (M + 2) * (j) + (M + 2) * (N + 2) * (k))

// Globals for the grid size
static int M = SIZE;
static int N = SIZE;
static int O = SIZE;
static float dt = 0.1f;       // Time delta
static float diff = 0.0001f;  // Diffusion constant
static float visc = 0.0001f;  // Viscosity constant

// Fluid simulation arrays
static float *u, *v, *w, *u_prev, *v_prev, *w_prev;
static float *dens, *dens_prev;

// Function to allocate simulation data
int allocate_data() {
    // TODO: Este size ]e calculado em N sitios diferentes. Pode bem ser global
    int size = (M + 2) * (N + 2) * (O + 2);
    u = (float *)_mm_malloc(size * sizeof(float), 32);
    v = (float *)_mm_malloc(size * sizeof(float), 32);
    w = (float *)_mm_malloc(size * sizeof(float), 32);
    u_prev = (float *)_mm_malloc(size * sizeof(float), 32);
    v_prev = (float *)_mm_malloc(size * sizeof(float), 32);
    w_prev = (float *)_mm_malloc(size * sizeof(float), 32);
    dens = (float *)_mm_malloc(size * sizeof(float), 32);
    dens_prev = (float *)_mm_malloc(size * sizeof(float), 32);

    if (!u || !v || !w || !u_prev || !v_prev || !w_prev || !dens ||
        !dens_prev) {
        std::cerr << "Cannot allocate memory" << std::endl;
        return 0;
    }
    return 1;
}

// Function to clear the data (set all to zero)
void clear_data() {
    int size = (M + 2) * (N + 2) * (O + 2);
    memset(u, 0, size);
    memset(v, 0, size);
    memset(w, 0, size);
    memset(u_prev, 0, size);
    memset(v_prev, 0, size);
    memset(w_prev, 0, size);
    memset(dens, 0, size);
    memset(dens_prev, 0, size);
}

// Free allocated memory
void free_data() {
    _mm_free(u);
    _mm_free(v);
    _mm_free(w);
    _mm_free(u_prev);
    _mm_free(v_prev);
    _mm_free(w_prev);
    _mm_free(dens);
    _mm_free(dens_prev);
}

// Apply events (source or force) for the current timestep
void apply_events(const std::vector<Event> &events) {
    // TODO: IX(i, j, k) calculado sempre, pode estar fora do if e numa var.
    // i, j, k constantes... estamos a calcula-los todos os ciclos.
    for (const auto &event : events) {
        if (event.type == ADD_SOURCE) {
            // Apply density source at the center of the grid
            int i = M / 2, j = N / 2, k = O / 2;
            dens[IX(i, j, k)] = event.density;
        } else if (event.type == APPLY_FORCE) {
            // Apply forces based on the event's vector (fx, fy, fz)
            int i = M / 2, j = N / 2, k = O / 2;
            u[IX(i, j, k)] = event.force.x;
            v[IX(i, j, k)] = event.force.y;
            w[IX(i, j, k)] = event.force.z;
        }
    }
}

// Function to sum the total density
float sum_density() {
    float total_density = 0.0f;
    int size = (M + 2) * (N + 2) * (O + 2);
    // TODO: SIMD
    for (int i = 0; i < size; i++) {
        total_density += dens[i];
    }
    return total_density;
}

// Simulation loop
void simulate(EventManager &eventManager, int timesteps) {
    for (int t = 0; t < timesteps; t++) {
        // Get the events for the current timestep
        std::vector<Event> events = eventManager.get_events_at_timestamp(t);

        // Apply events to the simulation
        apply_events(events);

        // Perform the simulation steps
        vel_step(M, N, O, u, v, w, u_prev, v_prev, w_prev, visc, dt);
        dens_step(M, N, O, dens, dens_prev, u, v, w, diff, dt);
    }
}

int main() {
    // Initialize EventManager
    EventManager eventManager;
    eventManager.read_events("events.txt");

    // Get the total number of timesteps from the event file
    int timesteps = eventManager.get_total_timesteps();

    // Allocate and clear data
    if (!allocate_data()) return -1;
    clear_data();

    // Run simulation with events
    simulate(eventManager, timesteps);

    // Print total density at the end of simulation
    float total_density = sum_density();
    std::cout << "Total density after " << timesteps
              << " timesteps: " << total_density << std::endl;

    // Free memory
    free_data();

    return 0;
}
