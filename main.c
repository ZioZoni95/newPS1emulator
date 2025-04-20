#include <stdio.h>
#include <stdlib.h> // Required for exit()
#include "cpu.h"
#include "interconnect.h"
#include "bios.h"

int main(int argc, char *argv[]) {

    // Use path from command line if provided, otherwise default to roms/ folder
    const char* bios_path = (argc > 1) ? argv[1] : "roms/SCPH1001.BIN";

    printf("myPS1 Emulator Starting...\n");
    if (argc <= 1) {
         printf("Usage: %s <path_to_bios_file>\n", argv[0]);
         printf("Attempting default path: %s\n", bios_path);
    }

    // --- Component Initialization ---
    Bios bios_data;
    Interconnect interconnect_state;
    Cpu cpu_state;

    // Load BIOS file
    if (!bios_load(&bios_data, bios_path)) {
        fprintf(stderr, "Failed to load BIOS. Make sure '%s' exists.\n", bios_path);
        return 1; // Exit if BIOS loading fails
    }

    // Initialize Interconnect
    interconnect_init(&interconnect_state, &bios_data);

    // Initialize CPU and connect Interconnect
    cpu_init(&cpu_state, &interconnect_state);

    // --- Main Emulation Loop ---
    printf("Starting emulation loop... (Will run until unhandled instruction or error)\n");

    // Use an infinite loop. The emulator will stop itself via exit()
    // when decode_and_execute encounters an unhandled instruction.
    while(1) {
        // Optional: Print PC before each instruction for debugging flow.
        // printf("PC=0x%08x\n", cpu_state.pc);

        // Execute one CPU instruction cycle.
        cpu_run_next_instruction(&cpu_state);

        // --- Add other checks later ---
        // Check for SDL events, handle timers, interrupts, debugger checks etc.
    }

    // This part will likely not be reached because exit() stops the program.
    // It's good practice to have a return here anyway.
    printf("Emulation finished (This shouldn't normally be printed).\n");
    return 0;
}