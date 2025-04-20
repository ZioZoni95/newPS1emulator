#ifndef INTERCONNECT_H // Include guard
#define INTERCONNECT_H

#include <stdint.h>       // For uint32_t
#include "bios.h"         // Needs the definition of the Bios struct

// --- Memory Map Definitions (Physical Addresses) ---
// These define the start addresses and sizes of known memory regions.
// Based on Guide Section 2.5 and 2.8 [cite: 84, 152]
#define BIOS_START 0x1fc00000 // Physical start address of the 512KB BIOS ROM [cite: 516]
#define BIOS_SIZE  (512 * 1024)
#define BIOS_END   (BIOS_START + BIOS_SIZE - 1)

// --- Add other physical memory map definitions here ---
// Example: RAM (Guide Section 2.34 / Fig 1) [cite: 515]
// #define RAM_START 0x00000000
// #define RAM_SIZE  (2 * 1024 * 1024) // 2MB Main RAM
// #define RAM_END   (RAM_START + RAM_SIZE - 1)

// Example: Hardware Registers (Guide Section 2.5 / Fig 1) [cite: 84]
// #define HW_REGS_START 0x1f801000
// #define HW_REGS_SIZE  8192 // 8KB (approx, exact size depends on highest reg)
// #define HW_REGS_END   (HW_REGS_START + HW_REGS_SIZE - 1) // Approximate end


// Structure representing the system bus interconnect.
// It holds pointers to all memory-mapped components (BIOS, RAM, GPU, etc.)
// and routes memory accesses to the correct component. [cite: 147]
typedef struct {
    Bios* bios;         // Pointer to the loaded BIOS data structure. [cite: 147]
    // Ram* ram;        // Pointer to the RAM structure (to be added later).
    // Gpu* gpu;        // Pointer to the GPU structure (to be added later).
    // Dma* dma;        // Pointer to the DMA structure (to be added later).
    // ... other peripherals ...
} Interconnect;

// Initializes the Interconnect structure, linking it to the provided components.
void interconnect_init(Interconnect* inter, Bios* bios /*, Ram* ram, ... */);

// Reads a 32-bit word from the specified memory address.
// It determines which component owns the address and calls its load function.
// Based on Guide Section 2.8 / 2.9 [cite: 160] / Refactored in Ch 6
uint32_t interconnect_load32(Interconnect* inter, uint32_t address);

// Writes a 32-bit word to the specified memory address.
// It determines which component owns the address and calls its store function.
// Based on Guide Section 2.16 [cite: 244] / Refactored in Ch 6
void interconnect_store32(Interconnect* inter, uint32_t address, uint32_t value);

// Add interconnect_load16, load8, store16, store8 later

// Helper function to map CPU virtual addresses (KUSEG/KSEG0/KSEG1) to physical addresses
// used for peripheral mapping. (Guide Section 2.38) [cite: 509]
uint32_t mask_region(uint32_t addr);


#endif // INTERCONNECT_H