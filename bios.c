#include "bios.h"       // Include the corresponding header file
#include <stdio.h>      // For file operations (fopen, fread, fclose, perror, fprintf)

// Loads the BIOS ROM content from a file specified by 'path' into the Bios struct.
// Based on Guide Section 2.7 Loading the BIOS [cite: 117]
int bios_load(Bios* bios, const char* path) {
    // Attempt to open the specified file in binary read mode ("rb").
    FILE *file = fopen(path, "rb");
    // Check if the file was opened successfully.
    if (!file) {
        // If fopen failed, print an error message (e.g., "No such file or directory").
        perror("Error opening BIOS file");
        return 0; // Indicate failure.
    }

    // Read data from the file directly into the 'data' buffer within the Bios struct.
    // - bios->data: destination buffer
    // - 1: size of each element to read (1 byte)
    // - BIOS_SIZE: number of elements to read (total size)
    // - file: the file stream to read from
    // fread returns the number of elements successfully read.
    size_t bytes_read = fread(bios->data, 1, BIOS_SIZE, file);

    // Close the file stream now that we are done with it.
    fclose(file);

    // Check if the number of bytes read matches the expected BIOS size.
    if (bytes_read != BIOS_SIZE) {
        // If not, print an error message indicating the mismatch.
        fprintf(stderr, "Error reading BIOS file: Read %zu bytes, expected %d\n",
                bytes_read, BIOS_SIZE);
        return 0; // Indicate failure.
    }

    // Optional: Verify the BIOS checksum against known values (Guide Table 3) [cite: 115]
    // Add MD5 or SHA1 checksum calculation and comparison logic here if desired.

    // Print a success message including the path and size.
    printf("BIOS loaded successfully from %s (%d bytes)\n", path, BIOS_SIZE);
    // Return 1 to indicate success.
    return 1;
}

// Reads a 32-bit value from the loaded BIOS data at a specific 'offset'.
// Handles little-endian conversion. [cite: 124]
// Based on Guide Section 2.7 load32 example [cite: 121]
uint32_t bios_load32(Bios* bios, uint32_t offset) {
    // Basic bounds check: Ensure reading 4 bytes starting at 'offset' stays within the BIOS_SIZE.
    if (offset > BIOS_SIZE - 4) { // Check if offset + 3 would exceed bounds
         fprintf(stderr, "BIOS read out of bounds: offset 0x%x\n", offset);
         // A real emulator might trigger an exception here. For now, return 0.
         return 0; // Placeholder error value
    }

    // Read the 4 individual bytes from the data buffer at the calculated offset.
    uint32_t b0 = bios->data[offset + 0]; // Least significant byte
    uint32_t b1 = bios->data[offset + 1];
    uint32_t b2 = bios->data[offset + 2];
    uint32_t b3 = bios->data[offset + 3]; // Most significant byte

    // Combine the bytes into a 32-bit value, respecting little-endian order.
    // b0 is the lowest byte, b3 is the highest. Shift and OR them together. [cite: 124]
    return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}