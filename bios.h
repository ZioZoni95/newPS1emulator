#ifndef BIOS_H       // Include guard
#define BIOS_H

#include <stdint.h>       // For uint8_t, uint32_t
#include <stddef.h>       // For size_t type

// Define the standard size of a PlayStation BIOS ROM. [cite: 114]
#define BIOS_SIZE (512 * 1024) // 512KB

// Structure to hold the BIOS data in memory.
typedef struct {
    // A buffer large enough to hold the entire BIOS content.
    uint8_t data[BIOS_SIZE];
} Bios;

// Loads the BIOS ROM content from a file specified by 'path' into the Bios struct.
// Returns 1 on success, 0 on failure (e.g., file not found, wrong size).
// Based on Guide Section 2.7 Loading the BIOS [cite: 117]
int bios_load(Bios* bios, const char* path);

// Reads a 32-bit value from the loaded BIOS data at a specific 'offset'.
// Handles little-endian conversion required by the MIPS architecture.
// Based on Guide Section 2.7 load32 example [cite: 121]
uint32_t bios_load32(Bios* bios, uint32_t offset);

// Add bios_load8, bios_load16 later if needed or use generic load

#endif // BIOS_H