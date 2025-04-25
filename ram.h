#ifndef RAM_H
#define RAM_H

#include <stdint.h> // For uint8_t, uint16_t, uint32_t

// Define the size of the PlayStation's main RAM (2 Megabytes)
#define RAM_SIZE (2 * 1024 * 1024)

// Structure to hold the RAM data
typedef struct {
    uint8_t data[RAM_SIZE]; // Buffer for the 2MB RAM content
} Ram;

// Initializes the RAM memory (e.g., fills with a default pattern).
void ram_init(Ram* ram);

// Reads a 32-bit value from RAM at the specified offset (little-endian).
uint32_t ram_load32(Ram* ram, uint32_t offset);

// Writes a 32-bit value to RAM at the specified offset (little-endian).
void ram_store32(Ram* ram, uint32_t offset, uint32_t value);

// Reads a 16-bit value from RAM at the specified offset (little-endian).
uint16_t ram_load16(Ram* ram, uint32_t offset);

// Writes a 16-bit value to RAM at the specified offset (little-endian).
void ram_store16(Ram* ram, uint32_t offset, uint16_t value);

// Reads an 8-bit value from RAM at the specified offset.
uint8_t ram_load8(Ram* ram, uint32_t offset);

// Writes an 8-bit value to RAM at the specified offset.
void ram_store8(Ram* ram, uint32_t offset, uint8_t value);


#endif // RAM_H