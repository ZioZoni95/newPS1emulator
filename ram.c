#include "ram.h"
#include <stdio.h>  // For fprintf, stderr (optional error checking)
#include <string.h> // For memset

// Initializes the RAM memory, filling it with a recognizable pattern.
// Based on Guide Section 2.34 [cite: 460]
void ram_init(Ram* ram) {
    // Fill RAM with a "garbage" value (0xCA) to simulate uninitialized state
    // and potentially help catch reads from uninitialized memory.
    memset(ram->data, 0xCA, RAM_SIZE);
    printf("RAM Initialized (%d bytes, filled with 0xCA).\n", RAM_SIZE);
}

// Helper for bounds checking
static inline int is_out_of_bounds(uint32_t offset, uint32_t access_size) {
    // Check if offset + (access_size - 1) exceeds the last valid index (RAM_SIZE - 1)
    return offset > RAM_SIZE - access_size;
}


// Reads a 32-bit value from RAM (Little-Endian)
// Based on Guide Section 2.34 load32 [cite: 461]
uint32_t ram_load32(Ram* ram, uint32_t offset) {
    if (is_out_of_bounds(offset, 4)) {
        fprintf(stderr, "RAM Load32 out of bounds: offset 0x%x\n", offset);
        return 0; // Or handle error appropriately
    }
    uint32_t b0 = ram->data[offset + 0];
    uint32_t b1 = ram->data[offset + 1];
    uint32_t b2 = ram->data[offset + 2];
    uint32_t b3 = ram->data[offset + 3];
    return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

// Writes a 32-bit value to RAM (Little-Endian)
// Based on Guide Section 2.34 store32 [cite: 462]
void ram_store32(Ram* ram, uint32_t offset, uint32_t value) {
     if (is_out_of_bounds(offset, 4)) {
        fprintf(stderr, "RAM Store32 out of bounds: offset 0x%x\n", offset);
        return; // Or handle error appropriately
    }
    ram->data[offset + 0] = (uint8_t)(value & 0xFF);
    ram->data[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
    ram->data[offset + 2] = (uint8_t)((value >> 16) & 0xFF);
    ram->data[offset + 3] = (uint8_t)((value >> 24) & 0xFF);
}

// Reads a 16-bit value from RAM (Little-Endian)
// Based on Guide Section 2.80 store16 (adapted for load) / 2.82 LHU [cite: 1011, 1045]
uint16_t ram_load16(Ram* ram, uint32_t offset) {
     if (is_out_of_bounds(offset, 2)) {
        fprintf(stderr, "RAM Load16 out of bounds: offset 0x%x\n", offset);
        return 0;
    }
    uint16_t b0 = ram->data[offset + 0];
    uint16_t b1 = ram->data[offset + 1];
    return b0 | (b1 << 8);
}

// Writes a 16-bit value to RAM (Little-Endian)
// Based on Guide Section 2.80 store16 [cite: 1011]
void ram_store16(Ram* ram, uint32_t offset, uint16_t value) {
    if (is_out_of_bounds(offset, 2)) {
        fprintf(stderr, "RAM Store16 out of bounds: offset 0x%x\n", offset);
        return;
    }
    ram->data[offset + 0] = (uint8_t)(value & 0xFF);
    ram->data[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
}

// Reads an 8-bit value from RAM
// Based on Guide Section 2.49 load8 [cite: 593]
uint8_t ram_load8(Ram* ram, uint32_t offset) {
    if (is_out_of_bounds(offset, 1)) {
        fprintf(stderr, "RAM Load8 out of bounds: offset 0x%x\n", offset);
        return 0;
    }
    return ram->data[offset];
}

// Writes an 8-bit value to RAM
// Based on Guide Section 2.49 store8 [cite: 591]
void ram_store8(Ram* ram, uint32_t offset, uint8_t value) {
    if (is_out_of_bounds(offset, 1)) {
        fprintf(stderr, "RAM Store8 out of bounds: offset 0x%x\n", offset);
        return;
    }
    ram->data[offset] = value;
}