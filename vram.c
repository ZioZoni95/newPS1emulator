#include "vram.h"
#include <stdio.h>  // For fprintf, stderr (optional error checking)
#include <string.h> // For memset

/**
 * @brief Initializes the VRAM memory.
 * Fills with 0x00 as a default state.
 * @param vram Pointer to the Vram struct to initialize.
 */
void vram_init(Vram* vram) {
    // Fill VRAM with zeros initially. Unlike RAM, VRAM often starts cleared.
    memset(vram->data, 0x00, VRAM_SIZE);
    printf("VRAM Initialized (%d bytes, filled with 0x00).\n", VRAM_SIZE);
}

// Helper for bounds checking (inline for potential performance)
static inline int is_out_of_bounds(uint32_t offset, uint32_t access_size) {
    // Check if offset + (access_size - 1) exceeds the last valid index (VRAM_SIZE - 1)
    return offset > VRAM_SIZE - access_size;
}


// Reads a 32-bit value from VRAM (Little-Endian)
uint32_t vram_load32(Vram* vram, uint32_t offset) {
    if (offset % 4 != 0) {
         fprintf(stderr, "VRAM Load32 unaligned: offset 0x%x\n", offset);
        // You might handle this differently (e.g., return garbage or specific behavior)
        // but for now, we'll proceed, acknowledging it's likely unintended.
    }
     if (is_out_of_bounds(offset, 4)) {
        fprintf(stderr, "VRAM Load32 out of bounds: offset 0x%x\n", offset);
        return 0; // Or handle error appropriately
    }
    uint32_t b0 = vram->data[offset + 0];
    uint32_t b1 = vram->data[offset + 1];
    uint32_t b2 = vram->data[offset + 2];
    uint32_t b3 = vram->data[offset + 3];
    return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

// Writes a 32-bit value to VRAM (Little-Endian)
void vram_store32(Vram* vram, uint32_t offset, uint32_t value) {
     if (offset % 4 != 0) {
         fprintf(stderr, "VRAM Store32 unaligned: offset 0x%x\n", offset);
        // Proceeding, but acknowledging potential issue.
    }
     if (is_out_of_bounds(offset, 4)) {
        fprintf(stderr, "VRAM Store32 out of bounds: offset 0x%x\n", offset);
        return; // Or handle error appropriately
    }
    vram->data[offset + 0] = (uint8_t)(value & 0xFF);
    vram->data[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
    vram->data[offset + 2] = (uint8_t)((value >> 16) & 0xFF);
    vram->data[offset + 3] = (uint8_t)((value >> 24) & 0xFF);
}

// Reads a 16-bit value from VRAM (Little-Endian) - Primary access method
uint16_t vram_load16(Vram* vram, uint32_t offset) {
     if (offset % 2 != 0) {
         fprintf(stderr, "VRAM Load16 unaligned: offset 0x%x\n", offset);
        // Proceeding, but this is usually an error for pixel access.
     }
     if (is_out_of_bounds(offset, 2)) {
        fprintf(stderr, "VRAM Load16 out of bounds: offset 0x%x\n", offset);
        return 0;
    }
    uint16_t b0 = vram->data[offset + 0];
    uint16_t b1 = vram->data[offset + 1];
    return b0 | (b1 << 8);
}

// Writes a 16-bit value to VRAM (Little-Endian) - Primary access method
void vram_store16(Vram* vram, uint32_t offset, uint16_t value) {
     if (offset % 2 != 0) {
         fprintf(stderr, "VRAM Store16 unaligned: offset 0x%x\n", offset);
        // Proceeding, but this is usually an error for pixel access.
    }
    if (is_out_of_bounds(offset, 2)) {
        fprintf(stderr, "VRAM Store16 out of bounds: offset 0x%x\n", offset);
        return;
    }
    vram->data[offset + 0] = (uint8_t)(value & 0xFF);
    vram->data[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
}

// Reads an 8-bit value from VRAM
uint8_t vram_load8(Vram* vram, uint32_t offset) {
    if (is_out_of_bounds(offset, 1)) {
        fprintf(stderr, "VRAM Load8 out of bounds: offset 0x%x\n", offset);
        return 0;
    }
    return vram->data[offset];
}

// Writes an 8-bit value to VRAM
void vram_store8(Vram* vram, uint32_t offset, uint8_t value) {
    if (is_out_of_bounds(offset, 1)) {
        fprintf(stderr, "VRAM Store8 out of bounds: offset 0x%x\n", offset);
        return;
    }
    vram->data[offset] = value;
}