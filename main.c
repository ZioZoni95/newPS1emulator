#include <stdio.h>
#include <stdlib.h>
#include "cpu.h"
#include "interconnect.h"
#include "bios.h"

int main(int argc, char *argv[]) {
    const char* bios_path = (argc > 1) ? argv[1] : "roms/SCPH1001.BIN";
    printf("myPS1 Emulator Starting...\n");
    if (argc <= 1) { /* ... Usage message ... */ }

    Bios bios_data;
    Interconnect interconnect_state;
    Cpu cpu_state;

    if (!bios_load(&bios_data, bios_path)) { return 1; }
    interconnect_init(&interconnect_state, &bios_data);
    cpu_init(&cpu_state, &interconnect_state);

    printf("Starting emulation loop... (Will run until unhandled instruction or error)\n");
    while(1) {
        // Optional: printf("PC=0x%08x\n", cpu_state.pc);
        cpu_run_next_instruction(&cpu_state);
    }
    return 0; // Unreachable
}