#include "interconnect.h" // Include the corresponding header file
#include <stdio.h>        // For printing error messages (fprintf, stderr)
#include <stddef.h>       // For size_t

// --- Memory Region Masking ---
// Array of masks used to convert KUSEG/KSEG0/KSEG1 addresses to physical addresses.
// The index into this array is determined by the top 3 bits of the CPU address.
// Based on Guide Section 2.38 [cite: 509, 512]
const uint32_t REGION_MASK[8] = {
    // KUSEG (0x00000000 - 0x7fffffff) -> Maps directly to physical (no change)
    0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
    // KSEG0 (0x80000000 - 0x9fffffff) -> Maps to physical by removing MSB (mask 0x7fffffff) [cite: 494]
    0x7fffffff,
    // KSEG1 (0xa0000000 - 0xbfffffff) -> Maps to physical by removing top 3 MSBs (mask 0x1fffffff) [cite: 496]
    // Note: Guide uses 0x1fffffff. Nocash map implies mirrors first 512MB like KSEG0. Using guide's mask.
    0x1fffffff,
    // KSEG2 (0xc0000000 - 0xffffffff) -> Not typically mapped to peripherals, passed through. [cite: 508]
    0xffffffff, 0xffffffff
};

// Applies the region mask based on the upper bits of the address.
uint32_t mask_region(uint32_t addr) {
    // Determine the region index using bits 31, 30, 29. [cite: 513]
    size_t index = (addr >> 29) & 0x7;
    // Apply the corresponding mask from the array. [cite: 514]
    return addr & REGION_MASK[index];
}


// Initialize the Interconnect structure by storing pointers to system components.
void interconnect_init(Interconnect* inter, Bios* bios /*, Ram* ram, ... */) {
    inter->bios = bios; // Store the pointer to the BIOS structure.
    // inter->ram = ram; // Store pointer to RAM when added.
    printf("Interconnect Initialized.\n");
}

// Handles 32-bit memory loads from the CPU.
// 'address' is the virtual address requested by the CPU.
uint32_t interconnect_load32(Interconnect* inter, uint32_t address) {
    // MIPS requires loads/stores to be naturally aligned. Check for 32-bit alignment (multiple of 4). [cite: 247]
    if (address % 4 != 0) {
        fprintf(stderr, "Unaligned load32 address: 0x%08x\n", address);
        // TODO: Trigger Address Error Load exception (Guide §2.78) [cite: 984]
        return 0; // Placeholder return
    }

    // Convert the CPU's virtual address to a physical address for peripheral mapping.
    uint32_t physical_addr = mask_region(address);

    // --- Route the read to the correct component based on the physical address ---

    // Check if the address falls within the BIOS physical range.
    if (physical_addr >= BIOS_START && physical_addr <= BIOS_END) {
        // Calculate the offset within the BIOS ROM.
        uint32_t offset = physical_addr - BIOS_START;
        // Call the BIOS function to read the 32-bit value. [cite: 161]
        return bios_load32(inter->bios, offset);
    }

    // --- Add RAM Range Check Here ---
    // if (physical_addr >= RAM_START && physical_addr <= RAM_END) { ... }

    // --- Add Hardware Register / Scratchpad / Expansion Checks Here ---

    // If the address doesn't map to any known component, print an error.
    fprintf(stderr, "Unhandled physical memory read at address: 0x%08x (Mapped from 0x%08x)\n",
            physical_addr, address);
    // A real system might return garbage or cause a bus error. Return 0 for now.
    return 0;
}

// Handles 32-bit memory stores from the CPU.
// 'address' is the virtual address, 'value' is the data to write.
void interconnect_store32(Interconnect* inter, uint32_t address, uint32_t value) {
    // Check for 32-bit alignment. [cite: 247]
     if (address % 4 != 0) {
        fprintf(stderr, "Unaligned store32 address: 0x%08x\n", address);
        // TODO: Trigger Address Error Store exception (Guide §2.78) [cite: 989]
        return; // Placeholder return
    }

    // Convert virtual address to physical address.
    uint32_t physical_addr = mask_region(address);

    // --- Route the write to the correct component ---

    // Check if the address falls within the BIOS range. BIOS is Read-Only Memory. [cite: 245]
    if (physical_addr >= BIOS_START && physical_addr <= BIOS_END) {
        // Print a warning/error, but ignore the write.
        fprintf(stderr, "Write attempt to BIOS ROM at address: 0x%08x = 0x%08x (Mapped from 0x%08x)\n",
                physical_addr, value, address);
        return; // Do nothing.
    }

    // --- Add RAM Range Check Here ---
    // if (physical_addr >= RAM_START && physical_addr <= RAM_END) { ... ram_store32 ... return; }

    // --- Add Hardware Register / Scratchpad / Expansion Checks Here ---
    // Example for Memory Control write handling (Guide §2.16.2) [cite: 260]
    // if (physical_addr >= MEM_CONTROL_START && ...) { handle_mem_control_write(...); return; }

    // If the address doesn't map to any known component, print an error.
    fprintf(stderr, "Unhandled physical memory write at address: 0x%08x = 0x%08x (Mapped from 0x%08x)\n",
            physical_addr, value, address);
    // For now, we just ignore writes to unknown addresses.
}