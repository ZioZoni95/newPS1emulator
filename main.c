#include <stdio.h>        // For standard I/O (printf)
#include <stdlib.h>       // For exit() (though not used currently after removing error exits)

#include "cpu.h"          // CPU definitions
#include "interconnect.h" // Interconnect definitions
#include "bios.h"         // BIOS definitions
#include "ram.h"          // <-- Include RAM header

/**
 * @brief Main entry point for the PlayStation emulator.
 *
 * Initializes the system components (BIOS, RAM, Interconnect, CPU),
 * handles command-line arguments for the BIOS path, and starts the
 * main emulation loop.
 *
 * @param argc Argument count.
 * @param argv Argument vector. Expects optional BIOS path as the first argument.
 * @return int Returns 0 on expected termination (currently unreachable), 1 on BIOS load error.
 */
int main(int argc, char *argv[]) {
    // Determine the BIOS file path: use the first command-line argument if provided,
    // otherwise default to "roms/SCPH1001.BIN".
    const char* bios_path = (argc > 1) ? argv[1] : "roms/SCPH1001.BIN";

    printf("myPS1 Emulator Starting...\n");

    // Basic usage message if no path is provided (optional).
    if (argc <= 1) {
        printf("Usage: %s [path/to/bios.bin]\n", argv[0]);
        printf("Defaulting to BIOS path: %s\n", bios_path);
    }

    // Declare the main components of the emulated system.
    Bios bios_data;
    Interconnect interconnect_state;
    Ram ram_memory; // <-- Declare RAM structure
    Cpu cpu_state;

    // --- Initialization Sequence ---

    // 1. Initialize RAM memory (e.g., fill with 0xCA pattern).
    ram_init(&ram_memory); // <-- Initialize RAM *before* loading BIOS or initializing interconnect

    // 2. Load the BIOS ROM from the specified file path.
    // If loading fails, print an error (handled inside bios_load) and exit.
    if (!bios_load(&bios_data, bios_path)) {
        fprintf(stderr, "Failed to load BIOS file.\n");
        return 1; // Exit with an error code
    }

    // 3. Initialize the Interconnect, linking it to the loaded BIOS and initialized RAM.
    interconnect_init(&interconnect_state, &bios_data, &ram_memory); // <-- Pass RAM pointer

    // 4. Initialize the CPU state, linking it to the initialized Interconnect.
    // The CPU can now access BIOS and RAM through the interconnect.
    cpu_init(&cpu_state, &interconnect_state);

    // --- Main Emulation Loop ---
    printf("Starting emulation loop... (Press Ctrl+C to exit)\n");
    // This loop continuously executes CPU instructions.
    while(1) {
        // Optional: Print the Program Counter before each instruction for debugging.
        // printf("PC=0x%08x\n", cpu_state.pc);

        // Execute a single CPU instruction cycle. This function handles:
        // - Applying pending load delay slot writes
        // - Fetching the instruction at the current PC
        // - Updating PC/next_pc for the next cycle (handles branch delay)
        // - Committing register state from the previous cycle
        // - Decoding and executing the fetched instruction
        // - Scheduling new load delays / setting branch target in next_pc if applicable
        cpu_run_next_instruction(&cpu_state);

        // Note: In a more advanced emulator, timing, event handling (like VSYNC),
        // and input polling would happen within or around this loop.
    }

    // This point is theoretically unreachable due to the infinite while(1) loop.
    // A clean exit mechanism (e.g., based on SDL events or a debugger command)
    // would be needed to reach here.
    printf("Emulation finished.\n"); // Should not print currently
    return 0;
}