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

// --- Define Memory Control Region ---
// Based on Guide Section 2.16.2 [cite: 258]
#define MEM_CONTROL_START 0x1f801000
// Guide §2.16.2 refers to registers at 0x1f801000, 0x1f801004, and implies others up to offset 36 (0x24) might exist.
// The range 0x1f801000 to 0x1f801023 covers offsets 0 to 35 (inclusive), total 36 bytes.
#define MEM_CONTROL_SIZE  36
#define MEM_CONTROL_END   (MEM_CONTROL_START + MEM_CONTROL_SIZE - 1)

// Specific addresses within MEM_CONTROL that the guide mentions handling
#define EXPANSION_1_BASE_ADDR 0x1f801000 // Guide §2.16.2 [cite: 258, 261]
#define EXPANSION_2_BASE_ADDR 0x1f801004 // Guide §2.16.2 [cite: 258, 262]

// Memory store function for 32 bits
void interconnect_store32(Interconnect* inter, uint32_t address, uint32_t value) {
    // Check alignment
     if (address % 4 != 0) {
        fprintf(stderr, "Unaligned store32 address: 0x%08x\n", address);
        // TODO: Trigger Address Error Store exception
        return;
    }

    // Note: For KSEG2 addresses like CACHE_CONTROL, mask_region should pass them through unchanged.
    // We'll check the physical_addr which will be the same as the original address in this case.
    uint32_t physical_addr = mask_region(address);

    // --- Route the write to the correct component ---

    // 1. Check BIOS range (Read-Only)
    if (physical_addr >= BIOS_START && physical_addr <= BIOS_END) {
        fprintf(stderr, "Write attempt to BIOS ROM at address: 0x%08x = 0x%08x (Mapped from 0x%08x)\n",
                physical_addr, value, address);
        return; // Ignore write
    }

    // 2. Check Memory Control range (Guide §2.16.2)
    if (physical_addr >= MEM_CONTROL_START && physical_addr <= MEM_CONTROL_END) {
        uint32_t offset = physical_addr - MEM_CONTROL_START;
        printf("~ Write to MEM_CONTROL region: Offset 0x%x = 0x%08x\n", offset, value);
        switch (physical_addr) {
            case EXPANSION_1_BASE_ADDR: // Check Expansion 1 Base
                if (value != 0x1f000000) { fprintf(stderr, "Warning: Bad Expansion 1 base address write: 0x%08x\n", value); }
                else { printf("  (Expansion 1 Base Address set correctly)\n"); }
                break;
            case EXPANSION_2_BASE_ADDR: // Check Expansion 2 Base
                 if (value != 0x1f802000) { fprintf(stderr, "Warning: Bad Expansion 2 base address write: 0x%08x\n", value); }
                 else { printf("  (Expansion 2 Base Address set correctly)\n"); }
                break;
            default: // Ignore other MEM_CONTROL writes
                printf("  (Ignoring write to other MEM_CONTROL register at offset 0x%x)\n", offset);
                break;
        }
        return; // Handled (or ignored) Memory Control write
    }

    // 3. Check RAM_SIZE register write (Guide §2.21)
    if (physical_addr == RAM_SIZE_ADDR) {
        printf("~ Write to RAM_SIZE register (0x%08x): Value 0x%08x (Ignoring)\n",
               physical_addr, value);
        return; // Explicitly ignore the write
    }

    // 4. Check CACHE_CONTROL register write (Guide §2.26) <-- ADD THIS BLOCK
    if (physical_addr == CACHE_CONTROL_ADDR) {
        // The guide suggests ignoring writes to this register for now as cache is not implemented.
        printf("~ Write to CACHE_CONTROL register (0x%08x): Value 0x%08x (Ignoring)\n",
               physical_addr, value);
        return; // Explicitly ignore the write
    }

    // 5. Check RAM Range (To be added)
    // if (physical_addr >= RAM_START && physical_addr <= RAM_END) { ... ram_store32 ... return; }

    // 6. Check Hardware Register Ranges (SPU, GPU, DMA, Timers, etc.) (To be added)
    // ...


    // --- Fallback for Unhandled Writes ---
    fprintf(stderr, "Unhandled physical memory write at address: 0x%08x = 0x%08x (Mapped from 0x%08x)\n",
            physical_addr, value, address);
}

// Memory store function for 16 bits (Halfword)
// Based on Section 2.39
void interconnect_store16(Interconnect* inter, uint32_t address, uint16_t value) {
    // Check alignment (must be multiple of 2)
     if (address % 2 != 0) {
        fprintf(stderr, "Unaligned store16 address: 0x%08x\n", address);
        // TODO: Trigger Address Error Store exception
        return;
    }

    uint32_t physical_addr = mask_region(address);

    // --- Route the write to the correct component ---

    // Check BIOS range (Read-Only)
    if (physical_addr >= BIOS_START && physical_addr <= BIOS_END) {
        fprintf(stderr, "Write attempt to BIOS ROM at address: 0x%08x = 0x%04x (16-bit)\n",
                physical_addr, value);
        return; // Ignore write
    }

    // Check Memory Control range
    // Note: Most hardware registers expect 32-bit writes. Need to verify
    // if any support 16-bit writes specifically. For now, treat as unhandled/log.
    if (physical_addr >= MEM_CONTROL_START && physical_addr <= MEM_CONTROL_END) {
         uint32_t offset = physical_addr - MEM_CONTROL_START;
         printf("~ Write16 to MEM_CONTROL region: Offset 0x%x = 0x%04x (Ignoring)\n", offset, value);
         // Generally ignore 16-bit writes here unless a specific register needs it.
         return;
    }

    // Check RAM_SIZE register address
    if (physical_addr == RAM_SIZE_ADDR) {
        printf("~ Write16 to RAM_SIZE register (0x%08x): Value 0x%04x (Ignoring)\n",
               physical_addr, value);
        return; // Explicitly ignore the write
    }

    // Check CACHE_CONTROL register address
    if (physical_addr == CACHE_CONTROL_ADDR) {
        printf("~ Write16 to CACHE_CONTROL register (0x%08x): Value 0x%04x (Ignoring)\n",
               physical_addr, value);
        return; // Explicitly ignore the write
    }

    // Check SPU register range (Example - Guide §2.40 mentions writes here)
    // #define SPU_START 0x1f801c00
    // #define SPU_END   0x1f801e7f // Approximate end
    // if (physical_addr >= SPU_START && physical_addr <= SPU_END) {
    //      printf("~ Write16 to SPU region: Address 0x%08x = 0x%04x (Not Implemented)\n", physical_addr, value);
    //      // TODO: Forward to SPU module's write16 function
    //      return;
    // }


    // --- Add RAM Range Check Here ---
    // if (physical_addr >= RAM_START && physical_addr <= RAM_END) {
    //    uint32_t offset = physical_addr - RAM_START;
    //    ram_store16(inter->ram, offset, value); // Call RAM write function
    //    // ram_store16 should handle little-endian byte order:
    //    // ram_buffer[offset] = value & 0xFF;
    //    // ram_buffer[offset+1] = (value >> 8) & 0xFF;
    //    return;
    // }

    // --- Fallback for Unhandled Writes ---
    fprintf(stderr, "Unhandled physical memory write16 at address: 0x%08x = 0x%04x (Mapped from 0x%08x)\n",
            physical_addr, value, address);
}
// --- Store 8-bit ---
void interconnect_store8(Interconnect* inter, uint32_t address, uint8_t value) {
    // No alignment check needed
    uint32_t physical_addr = mask_region(address);

    if (physical_addr >= BIOS_START && physical_addr <= BIOS_END) {
        fprintf(stderr, "Write8 attempt to BIOS ROM at address: 0x%08x = 0x%02x\n", physical_addr, value);
        return;
    }
    // Check Expansion 2 region (Guide §2.44)
    if (physical_addr >= EXPANSION_2_START && physical_addr <= EXPANSION_2_END) {
         printf("~ Write8 to Expansion 2 region: Address 0x%08x = 0x%02x (Ignoring)\n", physical_addr, value);
         return;
    }
    if (physical_addr >= MEM_CONTROL_START && physical_addr <= MEM_CONTROL_END) {
         uint32_t offset = physical_addr - MEM_CONTROL_START;
         printf("~ Write8 to MEM_CONTROL region: Offset 0x%x = 0x%02x (Ignoring)\n", offset, value);
         return;
    }
    if (physical_addr == RAM_SIZE_ADDR) {
        printf("~ Write8 to RAM_SIZE register (0x%08x): Value 0x%02x (Ignoring)\n", physical_addr, value);
        return;
    }
    if (physical_addr == CACHE_CONTROL_ADDR) {
        printf("~ Write8 to CACHE_CONTROL register (0x%08x): Value 0x%02x (Ignoring)\n", physical_addr, value);
        return;
    }

    // TODO: Add RAM store8
    // TODO: Add HW register store8 (e.g., SPU, maybe others?)

    fprintf(stderr, "Unhandled physical memory write8 at address: 0x%08x = 0x%02x (Mapped from 0x%08x)\n",
            physical_addr, value, address);
}