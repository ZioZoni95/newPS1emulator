#include "interconnect.h" // Include the corresponding header file
#include <stdio.h>        // For printing error messages (fprintf, stderr)
#include <stddef.h>       // For size_t
#include "ram.h"          // <-- Include ram.h for ram_* functions

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
void interconnect_init(Interconnect* inter, Bios* bios, Ram* ram /*, ... */) { // <-- Added Ram* param
    inter->bios = bios; // Store the pointer to the BIOS structure.
    inter->ram = ram;   // <-- Store pointer to RAM
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

    // --- Add RAM Range Check ---
    if (physical_addr >= RAM_START && physical_addr <= RAM_END) {
        // Calculate offset within RAM
        uint32_t offset = physical_addr - RAM_START;
        return ram_load32(inter->ram, offset); // <-- Call RAM load function
    }

    // --- Add Hardware Register / Scratchpad / Expansion Checks Here ---
    // Example: Check for Interrupt Mask read (return 0 for now)
    if (physical_addr == 0x1f801074) { // IRQ_MASK address
        printf("~ Read from IRQ_MASK (0x1f801074): Returning 0 (Not Implemented)\n");
        return 0;
    }
     if (physical_addr == 0x1f801070) { // IRQ_STATUS address
        printf("~ Read from IRQ_STATUS (0x1f801070): Returning 0 (Not Implemented)\n");
        return 0;
    }
    // TODO: Add more specific HW register reads as needed (GPUSTAT, DMA, Timers etc.)

    // If the address doesn't map to any known component, print an error.
    fprintf(stderr, "Unhandled physical memory read32 at address: 0x%08x (Mapped from 0x%08x)\n",
            physical_addr, address);
    // A real system might return garbage or cause a bus error. Return 0 for now.
    return 0;
}


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
       // printf("~ Write to MEM_CONTROL region: Offset 0x%x = 0x%08x\n", offset, value); // Less verbose
        switch (physical_addr) {
            case EXPANSION_1_BASE_ADDR: // Check Expansion 1 Base
                if (value != 0x1f000000) { fprintf(stderr, "Warning: Bad Expansion 1 base address write: 0x%08x\n", value); }
                // else { printf("  (Expansion 1 Base Address set correctly)\n"); } // Too verbose
                break;
            case EXPANSION_2_BASE_ADDR: // Check Expansion 2 Base
                 if (value != 0x1f802000) { fprintf(stderr, "Warning: Bad Expansion 2 base address write: 0x%08x\n", value); }
                 // else { printf("  (Expansion 2 Base Address set correctly)\n"); } // Too verbose
                break;
            default: // Ignore other MEM_CONTROL writes
                // printf("  (Ignoring write to other MEM_CONTROL register at offset 0x%x)\n", offset); // Too verbose
                break;
        }
        return; // Handled (or ignored) Memory Control write
    }

    // 3. Check RAM_SIZE register write (Guide §2.21)
    if (physical_addr == RAM_SIZE_ADDR) {
        // printf("~ Write to RAM_SIZE register (0x%08x): Value 0x%08x (Ignoring)\n", // Too verbose
        //        physical_addr, value);
        return; // Explicitly ignore the write
    }

    // 4. Check CACHE_CONTROL register write (Guide §2.26)
    if (physical_addr == CACHE_CONTROL_ADDR) {
        // The guide suggests ignoring writes to this register for now as cache is not implemented.
        // printf("~ Write to CACHE_CONTROL register (0x%08x): Value 0x%08x (Ignoring)\n", // Too verbose
        //       physical_addr, value);
        return; // Explicitly ignore the write
    }

    // --- Add RAM Range Check ---
    if (physical_addr >= RAM_START && physical_addr <= RAM_END) {
        uint32_t offset = physical_addr - RAM_START;
        ram_store32(inter->ram, offset, value); // <-- Call RAM store function
        return;
    }

    // 6. Check Hardware Register Ranges (SPU, GPU, DMA, Timers, etc.)
    // Example: Interrupt Control Registers (Guide §2.53)
     if (physical_addr >= 0x1f801070 && physical_addr <= 0x1f801077) { // IRQ_STATUS and IRQ_MASK
        uint32_t offset = physical_addr - 0x1f801070;
        printf("~ Write32 to IRQ_CONTROL region: Offset 0x%x = 0x%08x (Ignoring)\n", offset, value);
        // TODO: Implement IRQ handling later
        return;
     }
    // TODO: Add more specific HW register writes as needed (GPU, DMA, Timers etc.)


    // --- Fallback for Unhandled Writes ---
    fprintf(stderr, "Unhandled physical memory write32 at address: 0x%08x = 0x%08x (Mapped from 0x%08x)\n",
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
        fprintf(stderr, "Write16 attempt to BIOS ROM at address: 0x%08x = 0x%04x\n",
                physical_addr, value);
        return; // Ignore write
    }

    // Check Memory Control range
    if (physical_addr >= MEM_CONTROL_START && physical_addr <= MEM_CONTROL_END) {
         uint32_t offset = physical_addr - MEM_CONTROL_START;
         // printf("~ Write16 to MEM_CONTROL region: Offset 0x%x = 0x%04x (Ignoring)\n", offset, value); // Too verbose
         return;
    }

    // Check RAM_SIZE register address
    if (physical_addr == RAM_SIZE_ADDR) {
       // printf("~ Write16 to RAM_SIZE register (0x%08x): Value 0x%04x (Ignoring)\n", // Too verbose
       //        physical_addr, value);
        return; // Explicitly ignore the write
    }

    // Check CACHE_CONTROL register address
    if (physical_addr == CACHE_CONTROL_ADDR) {
        // printf("~ Write16 to CACHE_CONTROL register (0x%08x): Value 0x%04x (Ignoring)\n", // Too verbose
        //        physical_addr, value);
        return; // Explicitly ignore the write
    }

    // Check SPU register range (Guide §2.40)
    if (physical_addr >= SPU_START && physical_addr <= SPU_END) {
         printf("~ Write16 to SPU region: Address 0x%08x = 0x%04x (Ignoring)\n", physical_addr, value);
         // TODO: Forward to SPU module's write16 function
         return;
    }

    // --- Add RAM Range Check ---
    if (physical_addr >= RAM_START && physical_addr <= RAM_END) {
        uint32_t offset = physical_addr - RAM_START;
        ram_store16(inter->ram, offset, value); // <-- Call RAM store function
        return;
    }

    // Check Interrupt Control Registers (Guide §2.90)
     if (physical_addr >= 0x1f801070 && physical_addr <= 0x1f801077) { // IRQ_STATUS and IRQ_MASK
        uint32_t offset = physical_addr - 0x1f801070;
        printf("~ Write16 to IRQ_CONTROL region: Offset 0x%x = 0x%04x (Ignoring)\n", offset, value);
        // TODO: Implement IRQ handling later
        return;
     }
    // Check Timer Registers (Guide §2.70, §2.91) - Approximate Range
     if (physical_addr >= 0x1f801100 && physical_addr <= 0x1f80112F) {
         uint32_t offset = physical_addr - 0x1f801100;
         printf("~ Write16 to TIMERS region: Offset 0x%x = 0x%04x (Ignoring)\n", offset, value);
         // TODO: Implement Timer handling later
         return;
     }
    // TODO: Add other HW regs like GPU, DMA if they support 16-bit writes

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
         // printf("~ Write8 to Expansion 2 region: Address 0x%08x = 0x%02x (Ignoring)\n", physical_addr, value); // Too verbose
         return;
    }
    if (physical_addr >= MEM_CONTROL_START && physical_addr <= MEM_CONTROL_END) {
         uint32_t offset = physical_addr - MEM_CONTROL_START;
        // printf("~ Write8 to MEM_CONTROL region: Offset 0x%x = 0x%02x (Ignoring)\n", offset, value); // Too verbose
         return;
    }
    if (physical_addr == RAM_SIZE_ADDR) {
        // printf("~ Write8 to RAM_SIZE register (0x%08x): Value 0x%02x (Ignoring)\n", physical_addr, value); // Too verbose
        return;
    }
    if (physical_addr == CACHE_CONTROL_ADDR) {
       // printf("~ Write8 to CACHE_CONTROL register (0x%08x): Value 0x%02x (Ignoring)\n", physical_addr, value); // Too verbose
        return;
    }

    // --- Add RAM Range Check ---
    if (physical_addr >= RAM_START && physical_addr <= RAM_END) {
        uint32_t offset = physical_addr - RAM_START;
        ram_store8(inter->ram, offset, value); // <-- Call RAM store function
        return;
    }

    // TODO: Add HW register store8 (e.g., SPU, maybe others?)
    // Check SPU register range
    if (physical_addr >= SPU_START && physical_addr <= SPU_END) {
         printf("~ Write8 to SPU region: Address 0x%08x = 0x%02x (Ignoring)\n", physical_addr, value);
         // TODO: Forward to SPU module's write8 function if needed
         return;
    }

    fprintf(stderr, "Unhandled physical memory write8 at address: 0x%08x = 0x%02x (Mapped from 0x%08x)\n",
            physical_addr, value, address);
}

// --- Add Load16/Load8 ---

uint16_t interconnect_load16(Interconnect* inter, uint32_t address) {
     if (address % 2 != 0) {
        fprintf(stderr, "Unaligned load16 address: 0x%08x\n", address);
        // TODO: Trigger Address Error Load exception
        return 0;
    }
    uint32_t physical_addr = mask_region(address);

    // Check RAM
    if (physical_addr >= RAM_START && physical_addr <= RAM_END) {
        uint32_t offset = physical_addr - RAM_START;
        return ram_load16(inter->ram, offset);
    }

    // Check BIOS
     if (physical_addr >= BIOS_START && physical_addr <= BIOS_END) {
        uint32_t offset = physical_addr - BIOS_START;
        // Implement bios_load16 if needed, or handle differently. Let's return 0 for now.
        // This assumes 16-bit BIOS reads aren't common initially.
        fprintf(stderr, "Warning: Unhandled 16-bit read from BIOS at 0x%08x\n", physical_addr);
        return 0; // Placeholder
    }

    // Check Interrupt Control Registers (Guide §2.90)
     if (physical_addr >= 0x1f801070 && physical_addr <= 0x1f801077) { // IRQ_STATUS and IRQ_MASK
        uint32_t offset = physical_addr - 0x1f801070;
        printf("~ Read16 from IRQ_CONTROL region: Offset 0x%x (Ignoring, returning 0)\n", offset);
        // TODO: Implement IRQ handling later
        return 0;
     }

    // Check SPU Registers (Needed for LHU in Guide §2.82)
    if (physical_addr >= SPU_START && physical_addr <= SPU_END) {
         printf("~ Read16 from SPU region: Address 0x%08x (Ignoring, returning 0)\n", physical_addr);
         // TODO: Forward to SPU module's read16 function
         return 0; // Return 0 for now as per §2.82's initial approach
    }

    // TODO: Add checks for other HW regs if they support 16-bit reads (Timers?)

    fprintf(stderr, "Unhandled physical memory read16 at address: 0x%08x (Mapped from 0x%08x)\n", physical_addr, address);
    return 0;
}

uint8_t interconnect_load8(Interconnect* inter, uint32_t address) {
    uint32_t physical_addr = mask_region(address);

    // Check RAM
    if (physical_addr >= RAM_START && physical_addr <= RAM_END) {
        uint32_t offset = physical_addr - RAM_START;
        return ram_load8(inter->ram, offset);
    }

    // Check BIOS (Guide §2.46 needs BIOS byte reads)
     if (physical_addr >= BIOS_START && physical_addr <= BIOS_END) {
        uint32_t offset = physical_addr - BIOS_START;
        // Implement bios_load8 - it's just reading a byte
        if (offset < BIOS_SIZE) {
             return inter->bios->data[offset];
        } else {
             fprintf(stderr, "BIOS Load8 out of bounds: offset 0x%x\n", offset);
             return 0;
        }
    }

    // Check Expansion 1 (Guide §2.48)
     if (physical_addr >= EXPANSION_1_START && physical_addr <= EXPANSION_1_END) {
         printf("~ Read8 from Expansion 1 region: Address 0x%08x (Returning 0xFF - No Expansion)\n", physical_addr);
         return 0xFF; // Return 0xFF as per guide when no expansion is present
     }

    // TODO: Add checks for other HW regs if they support 8-bit reads

    fprintf(stderr, "Unhandled physical memory read8 at address: 0x%08x (Mapped from 0x%08x)\n", physical_addr, address);
    return 0; // Default fallback
}