#include <stdio.h>         // Standard I/O for printf, fprintf, stderr
#include "cpu.h"          // Include CPU definitions
#include "interconnect.h" // Include Interconnect definitions
#include "bios.h"         // Include BIOS definitions

// Main entry point of the emulator application
int main(int argc, char *argv[]) {

    // Define the default path for the BIOS file, relative to the executable.
    const char* bios_path = "roms/SCPH1001.BIN";

    // Check if a command-line argument was provided (overrides the default path).
    if (argc > 1) {
        bios_path = argv[1]; // Use the first argument as the BIOS path.
    } else {
         // If no argument, print usage instructions and the default path being used.
         printf("Usage: %s <path_to_bios_file>\n", argv[0]);
         printf("Attempting default path: %s\n", bios_path);
         // Consider exiting here if the default path is unlikely to exist.
    }

    // Print a startup message.
    printf("myPS1 Emulator Starting...\n");

    // --- Component Initialization ---
    // Declare structures to hold the state of the emulator components.
    Bios bios_data;                 // Holds the loaded BIOS data.
    Interconnect interconnect_state;// Manages memory access routing.
    Cpu cpu_state;                  // Holds the CPU state (registers, PC).
    // Ram ram_data;                // Declare RAM structure later when needed.

    // Attempt to load the BIOS file into the bios_data structure.
    if (!bios_load(&bios_data, bios_path)) {
        // If loading fails (file not found, wrong size), print an error and exit.
        fprintf(stderr, "Failed to load BIOS. Make sure '%s' exists.\n", bios_path);
        return 1; // Return a non-zero code to indicate an error.
    }

    // Initialize the Interconnect, passing pointers to the components it manages (currently just BIOS).
    interconnect_init(&interconnect_state, &bios_data /*, &ram_data */); // Add RAM later

    // Initialize the CPU state, passing a pointer to the initialized Interconnect.
    cpu_init(&cpu_state, &interconnect_state);

    // --- Main Emulation Loop ---
    // This loop simulates the continuous execution of the PlayStation CPU.
    printf("Starting emulation loop (running first 10 instructions)...\n");
    // For demonstration, we only run a fixed number of cycles. A real emulator runs indefinitely.
    for(int i = 0; i < 10; ++i) {
        // Print the current Program Counter before executing the instruction for this cycle.
        printf("\nCycle %d: PC=0x%08x\n", i + 1, cpu_state.pc);

        // Execute one CPU instruction cycle (fetch, increment PC, decode, execute).
        cpu_run_next_instruction(&cpu_state);

        // TODO: Add timing simulation here later.
        // TODO: Add event handling (input, window events) here later.
        // TODO: Add debugger checks/interaction here later.
    }

    // Print a message indicating the end of the demonstration loop.
    printf("\nEmulation loop finished (demo).\n");

    // Return 0 to indicate successful program execution.
    return 0;
}