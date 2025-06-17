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
#include <SDL2/SDL.h>
#define GLEW_STATIC
#include <GL/glew.h>

// --- Emulator Core Components ---
#include "cpu.h"
#include "interconnect.h"
#include "bios.h"
#include "ram.h"
#include "renderer.h"
#include "cdrom.h"

int main(int argc, char *argv[]) {
    // --- File Logging Setup ---
    FILE *log_file = freopen("emulator_log.txt", "w", stdout);
    if (log_file == NULL) {
        perror("Failed to open log file for stdout");
        return 1;
    }
    freopen("emulator_log.txt", "a", stderr);
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);
    printf("--- Log Started ---\n");

    // --- Configuration ---
    const char* bios_path = (argc > 1) ? argv[1] : "roms/SCPH1001.BIN";
    // --- MODIFICATION: More accurate cycles per frame calculation ---
    // The PSX CPU runs at 33,868,800 Hz.
    // For a 60 FPS target (NTSC), we run this many cycles per frame.
    const uint32_t cycles_per_frame = 33868800 / 60;

    printf("--- ZoniStation One Emulator ---\n");
    printf("Attempting to load BIOS from: %s\n", bios_path);

    // --- SDL & OpenGL Initialization ---
    // (No changes needed in this section, your SDL/GLEW setup is correct)
    printf("Initializing SDL Video...\n");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    printf("Creating SDL Window (1024x512, OpenGL)...\n");
    SDL_Window* window = SDL_CreateWindow("ZoniStation One", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1024, 512, SDL_WINDOW_OPENGL);
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

    // We now use dynamic allocation for the main state objects
    // to keep the stack clean.
    Bios* bios_data = malloc(sizeof(Bios));
    Ram* ram_memory = malloc(sizeof(Ram));
    Interconnect* interconnect_state = malloc(sizeof(Interconnect));
    Cpu* cpu_state = malloc(sizeof(Cpu));

    if (!bios_data || !ram_memory || !interconnect_state || !cpu_state) {
        fprintf(stderr, "Failed to allocate memory for core components.\n");
        return 1; // Exit if allocation fails
    }

    printf("  Initializing RAM...\n");
    ram_init(ram_memory);

    printf("  Loading BIOS...\n");
    if (!bios_load(bios_data, bios_path)) {
        return 1; // Cleanup is handled later
    }
    
    printf("  Initializing Interconnect...\n");
    interconnect_init(interconnect_state, bios_data, ram_memory);

    printf("  Initializing Renderer...\n");
    if (!renderer_init(&interconnect_state->gpu.renderer)) {
        fprintf(stderr, "Failed to initialize renderer!\n");
        return 1; // Cleanup is handled later
    }
    
    // Load a game disc (optional)
    if (!cdrom_load_disc(&interconnect_state->cdrom, "games/Crash Bandicoot.bin")) {
        printf("Warning: Could not load game disc. Running BIOS only.\n");
    }

    printf("  Initializing CPU...\n");
    cpu_init(cpu_state, interconnect_state);

    printf("All Emulator Components Initialized.\n");

    // --- Main Emulation Loop ---
    printf("Starting Emulation Loop...\n");
    bool should_quit = false;
    SDL_Event event;

    while (!should_quit) {
        // --- Handle Input/Window Events ---
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                should_quit = true;
            } else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    should_quit = true;
                }
            }
        }

        // --- Run Emulation for One Frame ---
        
        // Loop through cycles, but also step components at a finer grain.
        // This improves timing accuracy for interrupts and peripherals.
        // We will step timers every ~256 cycles for better resolution.
        for (uint32_t cycles_done = 0; cycles_done < cycles_per_frame; ) {
            // Define a number of cycles for this "step"
            uint32_t cycles_to_run = 256; 

            // Execute a small batch of CPU instructions
            for(int i = 0; i < cycles_to_run; i++) {
                 cpu_run_next_instruction(cpu_state);
            }

            // --- MODIFICATION: Step timers more frequently ---
            // After running a small batch of CPU cycles, we step the timers.
            // This ensures timer interrupts are more accurately timed relative to the CPU.
            timers_step(&interconnect_state->timers_state, cycles_to_run);

            cycles_done += cycles_to_run;
        }

        // --- MODIFICATION: Step the CD-ROM drive once per frame ---
        // This is for longer-term actions, like the delay in the Init command.
        cdrom_step(&interconnect_state->cdrom, cycles_per_frame);

        // --- Render and Display Frame ---
        // --- PROPOSED MODIFICATION START ---
        // This is the sequence that connects everything together.

        // 1. UPLOAD VRAM TO TEXTURE:
        //    Upload the current state of our emulated VRAM to the OpenGL texture object.
        //    This makes the VRAM content available to our shader.
        glBindTexture(GL_TEXTURE_2D, interconnect_state->gpu.renderer.vram_texture_id);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1024, 512, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, interconnect_state->gpu.vram.data);
        glBindTexture(GL_TEXTURE_2D, 0); // Unbind to be safe
        check_gl_error("After VRAM Texture Upload");

        // 2. DRAW THE RENDERER'S BUFFER: (THIS IS THE MISSING CALL)
        //    Now, tell the renderer to draw everything that was buffered during this frame's CPU execution.
        //    This calls renderer_draw(), which uploads the vertex data and calls glDrawArrays.
        renderer_display(&interconnect_state->gpu.renderer);
        
        // 3. SWAP THE WINDOW:
        //    Finally, swap the back buffer (which we just drew on) to the front to display the rendered frame.
        SDL_GL_SwapWindow(window);
        check_gl_error("After SwapWindow");
        // --- PROPOSED MODIFICATION END ---
    }

    // --- Cleanup ---
    printf("Emulation loop finished. Cleaning up...\n");

    renderer_destroy(&interconnect_state->gpu.renderer);
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    printf("SDL Quit.\n");
    
    // --- MODIFICATION: Free allocated memory ---
    free(cpu_state);
    free(interconnect_state);
    free(ram_memory);
    free(bios_data);

    printf("--- ZoniStation One Emulator Finished ---\n");
    fclose(log_file);
    return 0;
}