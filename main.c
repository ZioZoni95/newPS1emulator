/**
 * main.c
 * Entry point for the ZoniStation One Emulator.
 * Initializes all subsystems (SDL, OpenGL, Core Components), runs the main
 * emulation loop, and handles cleanup.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

// --- Graphics/Windowing Includes ---
// Uses SDL2 for window creation, event handling, and OpenGL context management.
// Uses GLEW for loading modern OpenGL extensions.
#include <SDL2/SDL.h>
#define GLEW_STATIC
#include <GL/glew.h>

// --- Emulator Core Components ---
#include "cpu.h"
#include "interconnect.h"
#include "bios.h"
#include "ram.h"
#include "renderer.h"
#include "cdrom.h" // <<< UPDATED: Added include for the CD-ROM component




int main(int argc, char *argv[]) {
    // --- File Logging Setup ---
    // Redirect stdout and stderr to a log file for easier debugging.
    FILE *log_file = freopen("emulator_log.txt", "w", stdout);
    if (log_file == NULL) {
        perror("Failed to open log file for stdout");
        return 1;
    }
    // Append stderr to the same file.
    freopen("emulator_log.txt", "a", stderr);
    setbuf(stdout, NULL); // Disable buffering to see logs in real-time.
    setbuf(stderr, NULL);
    printf("--- Log Started ---\n");

    // --- Configuration ---
    // Allow setting the BIOS path via command-line argument.
    const char* bios_path = (argc > 1) ? argv[1] : "roms/SCPH1001.BIN";
    // Define a number of CPU cycles to run per frame. This helps pace the emulation.
    // This value might need tuning for performance vs. accuracy.
    const uint32_t cycles_per_frame = 33868800 / 60; // PSX CPU speed / NTSC refresh rate

    printf("--- ZoniStation One Emulator ---\n");
    printf("Attempting to load BIOS from: %s\n", bios_path);

    // --- SDL & OpenGL Initialization ---
    printf("Initializing SDL Video...\n");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError());
        return 1;
    }

    // Set OpenGL context attributes for a modern OpenGL 3.3 Core Profile.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);

    printf("Creating SDL Window (1024x512, OpenGL)...\n");
    SDL_Window* window = SDL_CreateWindow(
        "ZoniStation One",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1024, 512, // Native PSX VRAM resolution
        SDL_WINDOW_OPENGL
    );
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow Error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    printf("Creating OpenGL Context...\n");
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (!gl_context) {
        fprintf(stderr, "SDL_GL_CreateContext Error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Initialize GLEW *after* the OpenGL context is created.
    printf("Initializing GLEW...\n");
    glewExperimental = GL_TRUE;
    GLenum glewError = glewInit();
    if (glewError != GLEW_OK) {
        fprintf(stderr, "Error initializing GLEW! %s\n", glewGetErrorString(glewError));
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    printf("GLEW Initialized. OpenGL Version: %s\n", glGetString(GL_VERSION));
    check_gl_error("After GLEW Init");

    // --- Emulator Component Initialization ---
    printf("Initializing Emulator Components...\n");

    Bios bios_data;
    Ram ram_memory;
    Interconnect interconnect_state;
    Cpu cpu_state;

    // 1. Initialize RAM
    printf("  Initializing RAM...\n");
    ram_init(&ram_memory);

    // 2. Load BIOS
    printf("  Loading BIOS...\n");
    if (!bios_load(&bios_data, bios_path)) {
        // Cleanup on failure
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // 3. Initialize Interconnect (connects all hardware components)
    printf("  Initializing Interconnect...\n");
    interconnect_init(&interconnect_state, &bios_data, &ram_memory);

    // 4. Initialize the Renderer (using the instance inside the GPU)
    printf("  Initializing Renderer...\n");
    if (!renderer_init(&interconnect_state.gpu.renderer)) {
        fprintf(stderr, "Failed to initialize renderer!\n");
        // Cleanup on failure
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    
    // 5. Load a game disc into the CD-ROM drive
    // NOTE: Replace "path/to/your/game.bin" with an actual game image.
    // If no disc is loaded, the emulator will just run the BIOS.
    if (!cdrom_load_disc(&interconnect_state.cdrom, "games/Crash Bandicoot.bin")) {
        printf("Warning: Could not load game disc. Running BIOS only.\n");
    }


    // 6. Initialize CPU (pass it the fully connected interconnect)
    printf("  Initializing CPU...\n");
    cpu_init(&cpu_state, &interconnect_state);

    printf("All Emulator Components Initialized.\n");

    // --- Main Emulation Loop ---
    printf("Starting Emulation Loop...\n");
    bool should_quit = false;
    SDL_Event event;
    uint64_t total_cycles = 0;

    while (!should_quit) {
        // --- Handle Input/Window Events ---
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                printf("SDL_QUIT event received.\n");
                should_quit = true;
            } else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    printf("Escape key pressed. Quitting.\n");
                    should_quit = true;
                }
            }
        }

        // --- Run Emulation for One Frame ---
        
        // Execute a frame's worth of CPU cycles.
        // cpu_run_cycles is a hypothetical function. If you have cpu_run_next_instruction,
        // you would loop that call `cycles_per_frame` times.
        for (uint32_t i = 0; i < cycles_per_frame; ++i) {
             cpu_run_next_instruction(&cpu_state);
        }

        // <<< UPDATED: Step the CD-ROM scheduler >>>
        // This is critical for handling timed CD-ROM commands. It must be called
        // regularly, passing the number of CPU cycles that have just run.
        cdrom_step(&interconnect_state.cdrom, cycles_per_frame);

        total_cycles += cycles_per_frame;

        // --- Render and Display Frame ---
        // The GPU emulation sends drawing commands to the renderer during CPU execution.
        // Here, we just need to swap the buffers to show the result on screen.
        SDL_GL_SwapWindow(window);
        check_gl_error("After SwapWindow");
    }

    // --- Cleanup ---
    printf("Emulation loop finished. Cleaning up...\n");

    renderer_destroy(&interconnect_state.gpu.renderer);
    printf("Destroying SDL GL Context and Window...\n");
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    printf("SDL Quit.\n");

    printf("--- ZoniStation One Emulator Finished ---\n");
    fclose(log_file); // Close the log file
    return 0;
}