/**
 * main.c
 * Entry point for the PlayStation Emulator.
 * Initializes components, runs the main emulation loop, and handles cleanup.
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <stdbool.h> // For bool type
 
 // --- Graphics/Windowing Includes ---
 // Uses SDL2 for window creation, event handling, and OpenGL context management.
 // Uses GLEW for loading OpenGL extensions.
 #include <SDL2/SDL.h>
 #define GLEW_STATIC // Define as static if linking GLEW statically
 #include <GL/glew.h> // Or your specific GLAD/OpenGL header
 
 // --- Emulator Core Components ---
 #include "cpu.h"
 #include "interconnect.h" // Includes gpu.h, dma.h, etc. implicitly if headers are set up right
 #include "bios.h"
 #include "ram.h"
 #include "renderer.h" // Include renderer for type definitions and init/destroy
 #include "cdrom.h"

 int main(int argc, char *argv[]) {

    // <<< ADD THIS BLOCK TO ENABLE FILE LOGGING >>>
    // Redirect stdout and stderr to a log file
    FILE *log_file = freopen("emulator_log.txt", "w", stdout);
    if (log_file == NULL) {
        perror("Failed to open log file for stdout");
        return 1; // Exit if we can't create the log file
    }
    // Also redirect stderr to the same file
    freopen("emulator_log.txt", "a", stderr);
    setbuf(stdout, NULL); // Disable buffering to see logs immediately
    setbuf(stderr, NULL);
    printf("--- Log Started ---\n");
    // <<< END OF LOGGING BLOCK >>>

    
     // --- Configuration ---
     // Use command-line argument for BIOS path, otherwise default.
     const char* bios_path = (argc > 1) ? argv[1] : "roms/SCPH1001.BIN"; // Default path
     const char *disk_path = (argc > 2) ? argv[2] : "games";
     const uint32_t cycles_per_frame = 300000; // Arbitrary number of CPU cycles per frame loop iteration
 
     printf("--- ZoniStation One Emulator ---\n");
     printf("Attempting to load BIOS from: %s\n", bios_path);
 
     // --- SDL & OpenGL Initialization ---
     printf("Initializing SDL Video...\n");
     if (SDL_Init(SDL_INIT_VIDEO) != 0) {
         fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError());
         return 1; // Exit if SDL fails
     }
 
     // Set OpenGL context attributes before creating the window
     // Requesting OpenGL 3.3 Core Profile
     SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
     SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
     SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
     // SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8); // Might be needed for some effects later
     SDL_Delay(2000); // Pause for 5000 milliseconds

     printf("Creating SDL Window (1024x512, OpenGL)...\n");
     SDL_Window* window = SDL_CreateWindow(
         "myPS1 Emulator",               // Window title
         SDL_WINDOWPOS_CENTERED,         // Initial x position
         SDL_WINDOWPOS_CENTERED,         // Initial y position
         1024,                           // Width (PSX VRAM Width)
         512,                            // Height (PSX VRAM Height)
         SDL_WINDOW_OPENGL               // Flags (Enable OpenGL)
         // | SDL_WINDOW_RESIZABLE      // Optional: Allow window resizing
     );
     if (!window) {
         fprintf(stderr, "SDL_CreateWindow Error: %s\n", SDL_GetError());
         SDL_Quit();
         return 1;
     }
     SDL_Delay(2000); // Pause for 5000 milliseconds

 
     printf("Creating OpenGL Context...\n");
     SDL_GLContext gl_context = SDL_GL_CreateContext(window);
     if (!gl_context) {
         fprintf(stderr, "SDL_GL_CreateContext Error: %s\n", SDL_GetError());
         SDL_DestroyWindow(window);
         SDL_Quit();
         return 1;
     }
     SDL_Delay(5000); // Pause for 5000 milliseconds

 
     // Initialize GLEW (or GLAD) - MUST be done *after* creating the GL context
     printf("Initializing GLEW...\n");
     glewExperimental = GL_TRUE; // Needed for core profile on some drivers
     GLenum glewError = glewInit();
     if (glewError != GLEW_OK) {
         fprintf(stderr, "Error initializing GLEW! %s\n", glewGetErrorString(glewError));
         SDL_GL_DeleteContext(gl_context);
         SDL_DestroyWindow(window);
         SDL_Quit();
         return 1;
     }
     printf("GLEW Initialized.\n");
     printf("OpenGL Version Detected: %s\n", glGetString(GL_VERSION));
     check_gl_error("After GLEW Init"); // Check for errors after init
     SDL_Delay(3000); // Pause for 5000 milliseconds

 
     // --- Emulator Component Initialization ---
     printf("Initializing Emulator Components...\n");
 
     // Declare component state variables
     Bios bios_data;
     Ram ram_memory;
     Interconnect interconnect_state; // Contains Gpu and Dma instances directly
     Cpu cpu_state;
     Cdrom cdrom_drive;
 
     // 1. Initialize RAM (Fill with default pattern)
     printf("  Initializing RAM...\n");
     ram_init(&ram_memory); //
 
     // 2. Load BIOS from file
     printf("  Loading BIOS...\n");
     if (!bios_load(&bios_data, bios_path)) { //
         // Cleanup SDL if BIOS load fails
         SDL_GL_DeleteContext(gl_context);
         SDL_DestroyWindow(window);
         SDL_Quit();
         return 1; // Exit if BIOS loading fails
     }

     //NEW: CDROM PRIMA DI INTERCONNECT
    printf("  Initializing CD-ROM drive...\n");
    cdrom_init(&cdrom_drive, &interconnect_state);
    cdrom_load_disc(&cdrom_drive, disk_path);

 
     // 3. Initialize Interconnect (Connects BIOS and RAM, also calls internal gpu_init/dma_init)
     printf("  Initializing Interconnect...\n");
     interconnect_init(&interconnect_state, &bios_data, &ram_memory, &cdrom_drive); //
 
     // 4. Initialize the Renderer *using the instance inside the Interconnect's GPU*
     //    This ensures the correct renderer state is initialized.
     printf("  Initializing Renderer (within Interconnect structure)...\n");
     if (!renderer_init(&interconnect_state.gpu.renderer)) { // <<< Use interconnect's renderer instance
         fprintf(stderr, "Failed to initialize renderer embedded in Interconnect!\n");
         // Cleanup SDL before exiting
         SDL_GL_DeleteContext(gl_context);
         SDL_DestroyWindow(window);
         SDL_Quit();
         return 1;
     }
 
     // 5. Initialize CPU (Pass the fully initialized Interconnect)
     printf("  Initializing CPU...\n");
     cpu_init(&cpu_state, &interconnect_state); //
     SDL_Delay(3000); // Pause for 5000 milliseconds

     printf("All Emulator Components Initialized.\n");
     SDL_Delay(1000); // Pause for 5000 milliseconds

 
     // --- Main Emulation Loop ---
     printf("Starting Emulation Loop...\n");
     bool should_quit = false;
     SDL_Event event;
     uint64_t total_cycles = 0; // Optional cycle counter for debugging
 
     while (!should_quit) {
 
         // --- Handle Input/Window Events ---
         while (SDL_PollEvent(&event)) {
             switch (event.type) {
                 case SDL_QUIT: // User clicked the window close button
                     printf("SDL_QUIT event received.\n");
                     should_quit = true;
                     break;
                 case SDL_KEYDOWN: // A key was pressed
                     if (event.key.keysym.sym == SDLK_ESCAPE) { // Check if Escape key
                         printf("Escape key pressed.\n");
                         should_quit = true;
                     }
                     // Add other key handlers here (e.g., pause, debug)
                     break;
                 // Handle other events like window resize if needed
             }
         }
 
         // --- Run Emulation Cycles ---
         // This runs a fixed number of CPU cycles per iteration of the main loop.
         // Proper timing (VSync, audio sync) would replace this fixed loop later.
         // printf("Running %u CPU cycles...\n", cycles_per_frame); // Very verbose!
         for (uint32_t i = 0; i < cycles_per_frame; ++i) {
             // Execute one CPU instruction cycle (fetches, decodes, executes)
             cpu_run_next_instruction(&cpu_state);
         }
         total_cycles += cycles_per_frame;

         //step per le periferiche per ogni ciclo di clock
        timers_step(&interconnect_state.timers_state, 1);
        cdrom_step(&cdrom_drive, 1);
         // printf("Cycles complete. Total: %llu\n", total_cycles); // Very verbose!
 
 
         // --- Render and Display Frame ---
         // The actual drawing commands are buffered by the GPU/Renderer interaction
         // during cpu_run_next_instruction (via GP0/DMA).
         // The 'display' call is currently triggered by the GP0(E5) hack inside gpu.c.
         // If that hack is removed, you might need an explicit draw/display call here.
         // renderer_display(&interconnect_state.gpu.renderer); // Example: Explicit frame display call
 
         // Swap the OpenGL front and back buffers to display the rendered frame.
         SDL_GL_SwapWindow(window);
         check_gl_error("After SwapWindow");
 
     } // End main loop
 
     // --- Cleanup ---
     printf("Emulation loop finished. Cleaning up...\n");
 
     // Destroy renderer resources (VBOs, VAO, shaders)
     // Ensure we destroy the *correct* renderer instance
     renderer_destroy(&interconnect_state.gpu.renderer); //
 
     // Destroy SDL OpenGL context and window
     printf("Destroying SDL GL Context and Window...\n");
     SDL_GL_DeleteContext(gl_context);
     SDL_DestroyWindow(window);
 
     // Quit SDL subsystems
     SDL_Quit();
     printf("SDL Quit.\n");
 
     printf("--- myPS1 Emulator Finished ---\n");
     return 0; // Successful exit
 }