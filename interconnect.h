#ifndef INTERCONNECT_H // Include guard: Prevents defining things multiple times
#define INTERCONNECT_H

#include <stdint.h>       // Standard header for fixed-width integer types like uint32_t
#include "bios.h"         // Needs the definition of the Bios struct for the pointer below

// --- Memory Map Definitions (Physical Addresses) ---
// These #defines make the code more readable by giving names to important addresses or sizes.
// We use physical addresses here because the interconnect maps virtual addresses first.

// BIOS Region (Guide §2.5, §2.6)
#define BIOS_START 0x1fc00000 // Physical start address of the 512KB BIOS ROM
#define BIOS_SIZE  (512 * 1024)
#define BIOS_END   (BIOS_START + BIOS_SIZE - 1) // Calculate the end address

// Memory Control Region (Contains Expansion Base Address registers, etc.) (Guide §2.16.2)
#define MEM_CONTROL_START 0x1f801000
#define MEM_CONTROL_SIZE  36 // Covers registers mentioned up to offset 0x23
#define MEM_CONTROL_END   (MEM_CONTROL_START + MEM_CONTROL_SIZE - 1)

// RAM_SIZE Register Address (Guide §2.21)
#define RAM_SIZE_ADDR 0x1f801060 // Specific address for the RAM Size register <-- THIS WAS ADDED

#define CACHE_CONTROL_ADDR 0xfffe0130 // Address of Cache Control register (Guide §2.26) <-- ADD THIS


// --- Add other physical memory map definitions here as needed ---
// #define RAM_START 0x00000000
// #define RAM_SIZE  (2 * 1024 * 1024)
// #define RAM_END   (RAM_START + RAM_SIZE - 1)
// #define HW_REGS_START 0x1f801000 // Note: Overlaps with MEM_CONTROL partly
// etc...


// --- Interconnect Structure Definition ---
// This structure holds pointers to all the different components connected to the bus.
// The Interconnect module uses these pointers to route memory accesses.
typedef struct {
    Bios* bios;         // Pointer to the loaded BIOS data structure.
    // Ram* ram;        // Pointer to the RAM structure (will be added later).
    // Gpu* gpu;        // Pointer to the GPU structure (will be added later).
    // Dma* dma;        // Pointer to the DMA structure (will be added later).
    // ... add pointers to other peripherals (SPU, Timers, CDROM, Controllers) later ...
} Interconnect;

// --- Function Declarations (Prototypes) ---

// Initializes the Interconnect structure, linking it to the provided components.
void interconnect_init(Interconnect* inter, Bios* bios /*, Ram* ram, ... */);

// Reads a 32-bit word from the specified memory address via the Interconnect.
uint32_t interconnect_load32(Interconnect* inter, uint32_t address);

// Writes a 32-bit word to the specified memory address via the Interconnect.
void interconnect_store32(Interconnect* inter, uint32_t address, uint32_t value);

void interconnect_store16(Interconnect* inter, uint32_t address, uint16_t value); // <-- ADD THIS

// Add declarations for interconnect_load16, load8, store16, store8 later

// Helper function to map CPU virtual addresses (KUSEG/KSEG0/KSEG1) to physical addresses.
uint32_t mask_region(uint32_t addr);


#endif // INTERCONNECT_H