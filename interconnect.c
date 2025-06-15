#include "interconnect.h" // Includes associated header and headers for components (gpu.h, dma.h etc.)
#include <stdio.h>
#include <stdbool.h>

// Forward declaration for the internal DMA transfer function
static void interconnect_perform_dma(Interconnect* inter, uint32_t channel_index);

// --- Memory Region Masking ---
// Array mapping the top 3 bits of a virtual address to a mask
// used to convert KUSEG/KSEG0/KSEG1 addresses to physical addresses.
// KSEG2 addresses are not masked. Based on Guide Section 2.38[cite: 512].
const uint32_t REGION_MASK[8] = {
    0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, // KUSEG (0x00000000 - 0x7FFFFFFF) - No mask
    0x7fffffff,                                     // KSEG0 (0x80000000 - 0x9FFFFFFF) - Mask top bit
    0x1fffffff,                                     // KSEG1 (0xA0000000 - 0xBFFFFFFF) - Mask top 3 bits
    0xffffffff, 0xffffffff                          // KSEG2 (0xC0000000 - 0xFFFFFFFF) - No mask
};

/**
 * @brief Maps a CPU virtual address to a physical address by masking region bits.
 * KSEG2 addresses are returned unchanged.
 * @param addr The 32-bit virtual address.
 * @return The 32-bit physical address.
 */
uint32_t mask_region(uint32_t addr) {
    // Use top 3 bits to index into the mask array
    size_t index = (addr >> 29) & 0x7;
    return addr & REGION_MASK[index];
}


// --- Initialization ---
/**
 * @brief Initializes the Interconnect struct.
 * Assigns component pointers, initializes embedded structs (GPU, DMA),
 * and resets interrupt controller state.
 * @param inter Pointer to the Interconnect struct.
 * @param bios Pointer to the loaded Bios struct.
 * @param ram Pointer to the initialized Ram struct.
 */
void interconnect_init(Interconnect* inter, Bios* bios, Ram* ram) {
    inter->bios = bios;
    inter->ram = ram;
    dma_init(&inter->dma); // Initialize DMA controller state
    gpu_init(&inter->gpu); // Initialize GPU state (now contains Renderer)


    cdrom_init(&inter->cdrom,inter);

    // Initialize Interrupt Controller state
    inter->irq_status = 0; // No pending interrupts
    inter->irq_mask = 0;   // All interrupts masked
    
    // Initialize Timer state <<< ADD THIS CALL
    timers_init(&inter->timers_state, inter);
    
    printf("Interconnect Initialized (BIOS, RAM, DMA, GPU, CDROM, IRQ states set).\n");
}


// --- Peripheral Interrupt Request ---
/**
 * @brief Allows peripherals to signal an interrupt request.
 * Sets the corresponding bit in the I_STAT register (irq_status).
 * @param inter Pointer to the Interconnect instance.
 * @param irq_line The interrupt line number (0-10).
 */
void interconnect_request_irq(Interconnect* inter, uint32_t irq_line) {
    if (irq_line <= IRQ_PIO) { // Check if line is valid (0-10)
        uint16_t old_stat = inter->irq_status;
        inter->irq_status |= (1 << irq_line); // Set the corresponding bit
        if (old_stat != inter->irq_status) {
            // Optional: Print only when status actually changes
            printf("IRQ Requested: Line %u. I_STAT is now 0x%04x\n", irq_line, inter->irq_status);
        }
    } else {
        fprintf(stderr, "Warning: Invalid IRQ line %u requested by peripheral.\n", irq_line);
    }
}


// --- Load Operations ---

/**
 * @brief Handles 32-bit memory reads from the CPU.
 * @param inter The Interconnect instance.
 * @param address Virtual address to read from.
 * @return The 32-bit value read.
 */
uint32_t interconnect_load32(Interconnect* inter, uint32_t address) {
    // Check for 32-bit alignment (Word access)
    if (address % 4 != 0) {
        // TODO: This should trigger an Address Error Load exception in the CPU.
        fprintf(stderr, "Unaligned load32 address: 0x%08x\n", address);
        // For now, just return a garbage value, but an exception is correct.
        return 0xBADBAD32; // Placeholder for unaligned access
    }

    uint32_t physical_addr = mask_region(address);

    // --- Hardware Register Checks (Specific Addresses First) ---

// --- Check Timer Range --- <<< ADD THIS BLOCK
    if (physical_addr >= TIMERS_START && physical_addr <= TIMERS_END) {
        uint32_t timer_base_offset = physical_addr - TIMERS_START;
        int timer_index = timer_base_offset / 0x10; // Each timer block is 0x10 bytes wide
        uint32_t register_offset = physical_addr & 0xF; // Offset within the timer block (0, 4, 8)

        // printf("~ Read32 from Timer %d Offset 0x%x\n", timer_index, register_offset);
        return timer_read32(&inter->timers_state, timer_index, register_offset);
    }
    
    // Interrupt Controller Registers
    if (physical_addr == IRQ_STATUS_ADDR) { // 0x1f801070 (I_STAT)
        printf("~ Read32 from IRQ_STATUS (0x1f801070): Returning 0x%04x\n", inter->irq_status);
        return (uint32_t)inter->irq_status;
    }
    if (physical_addr == IRQ_MASK_ADDR) { // 0x1f801074 (I_MASK)
        printf("~ Read32 from IRQ_MASK (0x1f801074): Returning 0x%04x\n", inter->irq_mask);
        return (uint32_t)inter->irq_mask;
    }

    // GPU Registers
    if (physical_addr == GPU_GPUREAD_ADDR) { // 0x1f801810 (Read = GPUREAD)
        // Reading GPUREAD should return data from VRAM transfers or command responses
        printf("~ Read32 from GPUREAD (0x1f801810)\n");
        return gpu_read_data(&inter->gpu);
    }
    if (physical_addr == GPU_GPUSTAT_ADDR) { // 0x1f801814 (Read = GPUSTAT)
        // Reading GPUSTAT returns the GPU status flags
        // printf("~ Read32 from GPUSTAT (0x1f801814)\n"); // Often read, can be noisy
        return gpu_read_status(&inter->gpu);
    }

    // Timer Registers (Example: Read Timer 1 Counter)
    if (physical_addr == 0x1f801110) { // Timer 1 Counter Value (T1_COUNT)
         // TODO: Implement actual Timer read logic
         printf("~ Read32 from Timer 1 Counter (0x1f801110): Returning 0 (Placeholder)\n");
         return 0;
    }
    // Add reads for other Timer counters/modes/targets if needed


    // --- Region Checks (Broader Ranges) ---

    // DMA Region (0x1f801080 - 0x1f8010FF)
    if (physical_addr >= DMA_START && physical_addr <= DMA_END) {
        uint32_t offset = physical_addr - DMA_START;
        printf("~ Read32 from DMA region: Addr=0x%08x Offset=0x%x\n", physical_addr, offset);
        return dma_read(&inter->dma, offset); // Delegate to DMA module
    }

    // BIOS Region (0x1fc00000 - 0x1fc7ffff)
    if (physical_addr >= BIOS_START && physical_addr <= BIOS_END) {
        uint32_t offset = physical_addr - BIOS_START;
        // printf("~ Read32 from BIOS region: Addr=0x%08x Offset=0x%x\n", physical_addr, offset); // Can be noisy
        return bios_load32(inter->bios, offset); // Delegate to BIOS module
    }

    // Main RAM Region (0x00000000 - 0x001fffff)
    if (physical_addr <= RAM_END) {
        // printf("~ Read32 from RAM region: Addr=0x%08x\n", physical_addr); // Can be very noisy
        return ram_load32(inter->ram, physical_addr); // Delegate to RAM module
    }

    // Timer Region (General Check - 0x1f801100 - 0x1f80112F)
    if (physical_addr >= TIMERS_START && physical_addr <= TIMERS_END) {
        fprintf(stderr, "Warning: Unhandled Timer read32 at 0x%08x\n", physical_addr);
        return 0; // Return 0 for unhandled timer reads
    }

    // SPU Region (0x1f801C00 - 0x1f801E7F)
    if (physical_addr >= SPU_START && physical_addr <= SPU_END) {
         // printf("~ Read32 from SPU region: Address 0x%08x (Ignoring, returning 0)\n", physical_addr); // Often noisy
         return 0; // SPU not implemented yet
    }

    // Expansion 1 Region (0x1f000000 - 0x1f7fffff)
    if (physical_addr >= EXPANSION_1_START && physical_addr <= EXPANSION_1_END) {
         printf("~ Read32 from Expansion 1 region: Address 0x%08x (Returning 0xFFFFFFFF)\n", physical_addr);
         return 0xFFFFFFFF; // Expansion 1 returns all Fs when empty
    }


    // --- Fallback for Unhandled Addresses ---
    fprintf(stderr, "Unhandled physical memory read32 at address: 0x%08x (Mapped from 0x%08x)\n",
            physical_addr, address);
    return 0; // Or a more distinct "garbage" value like 0xDEADBEEF
}

/**
 * @brief Handles 16-bit memory reads from the CPU.
 * @param inter The Interconnect instance.
 * @param address Virtual address to read from.
 * @return The 16-bit value read.
 */
uint16_t interconnect_load16(Interconnect* inter, uint32_t address) {
     // Check for 16-bit alignment (Halfword access)
     if (address % 2 != 0) {
        // TODO: Trigger Address Error Load exception
        fprintf(stderr, "Unaligned load16 address: 0x%08x\n", address);
        return 0xBADB; // Placeholder
    }
    uint32_t physical_addr = mask_region(address);

// --- Check Timer Range --- <<< ADD THIS BLOCK
    if (physical_addr >= TIMERS_START && physical_addr <= TIMERS_END) {
        uint32_t timer_base_offset = physical_addr - TIMERS_START;
        int timer_index = timer_base_offset / 0x10;
        uint32_t register_offset = physical_addr & 0xF;

        // printf("~ Read16 from Timer %d Offset 0x%x\n", timer_index, register_offset);
        return timer_read16(&inter->timers_state, timer_index, register_offset);
    }

    // Interrupt Controller Registers
    if (physical_addr == IRQ_STATUS_ADDR) { // 0x1f801070 (I_STAT)
        printf("~ Read16 from IRQ_STATUS (0x1f801070): Returning 0x%04x\n", inter->irq_status);
        return inter->irq_status;
    }
     if (physical_addr == IRQ_MASK_ADDR) { // 0x1f801074 (I_MASK)
        printf("~ Read16 from IRQ_MASK (0x1f801074): Returning 0x%04x\n", inter->irq_mask);
        return inter->irq_mask;
     }

    // SPU Region (Reads usually return 0 or specific status)
    if (physical_addr >= SPU_START && physical_addr <= SPU_END) {
         // printf("~ Read16 from SPU region: Address 0x%08x (Ignoring, returning 0)\n", physical_addr); // Often noisy
         return 0; // SPU reads often ignored initially
    }

    // Timer Region (Check specific registers if needed)
     if (physical_addr >= TIMERS_START && physical_addr <= TIMERS_END) {
        // Handle specific 16-bit Timer reads (Counter, Mode, Target)
        // Example:
        // if (physical_addr == 0x1F801100) return timer0_get_count();
        fprintf(stderr, "Warning: Unhandled Timer read16 at 0x%08x\n", physical_addr);
        return 0;
    }

    // Main RAM Region
    if (physical_addr <= RAM_END) {
        // printf("~ Read16 from RAM region: Addr=0x%08x\n", physical_addr); // Can be noisy
        return ram_load16(inter->ram, physical_addr);
    }

    // BIOS Region (Unlikely, but check)
     if (physical_addr >= BIOS_START && physical_addr <= BIOS_END) {
        fprintf(stderr, "Warning: Unhandled 16-bit read from BIOS at 0x%08x\n", physical_addr);
        // BIOS is typically read 32 bits at a time for instructions
        // Reading 16 bits might happen but isn't common.
        // We could implement bios_load16 if needed.
        return 0;
    }

    // GPU Region (Unlikely 16-bit reads)
    if (physical_addr >= GPU_START && physical_addr <= GPU_END) {
         fprintf(stderr, "Warning: Unhandled GPU read16 at 0x%08x\n", physical_addr);
         return 0;
    }

    // DMA Region (Unlikely 16-bit reads)
     if (physical_addr >= DMA_START && physical_addr <= DMA_END) {
        fprintf(stderr, "Warning: Unhandled DMA read16 at 0x%08x\n", physical_addr);
        return 0;
    }

    // Expansion 1 Region
    if (physical_addr >= EXPANSION_1_START && physical_addr <= EXPANSION_1_END) {
         printf("~ Read16 from Expansion 1 region: Address 0x%08x (Returning 0xFFFF)\n", physical_addr);
         return 0xFFFF; // Expansion 1 returns all Fs when empty
    }


    fprintf(stderr, "Unhandled physical memory read16 at address: 0x%08x (Mapped from 0x%08x)\n", physical_addr, address);
    return 0;
}

/**
 * @brief Handles 8-bit memory reads from the CPU.
 * @param inter The Interconnect instance.
 * @param address Virtual address to read from.
 * @return The 8-bit value read.
 */
uint8_t interconnect_load8(Interconnect* inter, uint32_t address) {
    uint32_t physical_addr = mask_region(address);

    // --- Check Timer Range --- <<< ADD THIS BLOCK
    if (physical_addr >= TIMERS_START && physical_addr <= TIMERS_END) {
        // 8-bit reads from timers are generally undefined or read partial registers.
        fprintf(stderr, "Warning: Unhandled 8-bit read from Timer range: 0x%08x\n", physical_addr);
        return 0; // Return 0 for safety
    }
    // CDROM Registers (Example)
    if (physical_addr >= 0x1f801800 && physical_addr <= 0x1f801803) {
        // printf("~ Read8 from CDROM Reg (0x%08x): Returning 0 (Placeholder)\n", physical_addr); // Often noisy
        // TODO: Implement proper CDROM status/response reads
        return 0; // Placeholder response
    }

    // Expansion 1 Region
     if (physical_addr >= EXPANSION_1_START && physical_addr <= EXPANSION_1_END) {
         // Expansion 1 is used for parallel port devices. Reading when empty returns 0xFF. [cite: 575]
         // printf("~ Read8 from Expansion 1 region: Address 0x%08x (Returning 0xFF)\n", physical_addr); // Noisy
         return 0xFF;
     }

    // BIOS Region
     if (physical_addr >= BIOS_START && physical_addr <= BIOS_END) {
        uint32_t offset = physical_addr - BIOS_START;
        if (offset < BIOS_SIZE) {
             // Implement bios_load8 if needed, or read directly:
             // printf("~ Read8 from BIOS: Addr=0x%08x Offset=0x%x\n", physical_addr, offset); // Noisy
             return inter->bios->data[offset];
        } else {
             fprintf(stderr, "BIOS Load8 out of bounds: offset 0x%x\n", offset);
             return 0; // Error
        }
    }

    // Main RAM Region
    if (physical_addr <= RAM_END) {
        // printf("~ Read8 from RAM: Addr=0x%08x\n", physical_addr); // Very noisy
        return ram_load8(inter->ram, physical_addr);
    }

    // Other regions (SPU, Timers, GPU, DMA, Exp2, MemCtrl) are less likely for 8-bit reads

    fprintf(stderr, "Unhandled physical memory read8 at address: 0x%08x (Mapped from 0x%08x)\n", physical_addr, address);
    return 0;
}


// --- Store Operations ---

/**
 * @brief Handles 32-bit memory writes from the CPU.
 * @param inter The Interconnect instance.
 * @param address Virtual address to write to.
 * @param value The 32-bit value to write.
 */
void interconnect_store32(Interconnect* inter, uint32_t address, uint32_t value) {
    // Check alignment
    if (address % 4 != 0) {
        // TODO: Trigger Address Error Store exception
        fprintf(stderr, "Unaligned store32 address: 0x%08x = 0x%08x\n", address, value);
        return;
    }

    uint32_t physical_addr = mask_region(address);


    // --- Check Timer Range --- <<< ADD THIS BLOCK
    if (physical_addr >= TIMERS_START && physical_addr <= TIMERS_END) {
        uint32_t timer_base_offset = physical_addr - TIMERS_START;
        int timer_index = timer_base_offset / 0x10;
        uint32_t register_offset = physical_addr & 0xF;

        // printf("~ Write32 to Timer %d Offset 0x%x = 0x%08x\n", timer_index, register_offset, value);
        timer_write32(&inter->timers_state, timer_index, register_offset, value);
        return; // Handled
    }
    // --- Hardware Register Checks (Specific Addresses First) ---

    // Interrupt Controller Registers
    if (physical_addr == IRQ_STATUS_ADDR) { // 0x1f801070 (I_STAT)
        // Writing acknowledges (clears) specified interrupt flags
        uint16_t ack_mask = (uint16_t)(value & 0x7FF); // Only bits 0-10 matter
        inter->irq_status &= ~ack_mask; // Clear bits that are set in value
        printf("~ Write32 to IRQ_STATUS (Ack): Value=0x%08x -> I_STAT=0x%04x\n", value, inter->irq_status);
        return;
    }
    if (physical_addr == IRQ_MASK_ADDR) { // 0x1f801074 (I_MASK)
        // Writing sets the interrupt mask
        inter->irq_mask = (uint16_t)(value & 0x7FF); // Only bits 0-10 matter
        printf("~ Write32 to IRQ_MASK: Value=0x%08x -> I_MASK=0x%04x\n", value, inter->irq_mask);
        return;
    }

    // Cache Control (KSEG2)
    if (physical_addr == CACHE_CONTROL_ADDR) {
        printf("~ Write32 to CACHE_CONTROL register (0x%08x) = 0x%08x (Ignoring)\n", physical_addr, value);
        // Cache not implemented yet
        return;
    }

    // GPU Registers
    if (physical_addr == GPU_GP0_ADDR) { // 0x1f801810 (Write = GP0)
        // printf("~ Write32 GP0 = 0x%08x\n", value); // Can be noisy
        gpu_gp0(&inter->gpu, value); // Delegate to GPU module
        return;
    }
    if (physical_addr == GPU_GP1_ADDR) { // 0x1f801814 (Write = GP1)
        // printf("~ Write32 GP1 = 0x%08x\n", value); // Can be noisy
        gpu_gp1(&inter->gpu, value); // Delegate to GPU module
        return;
    }


    // --- Region Checks (Broader Ranges) ---

    // DMA Region
    if (physical_addr >= DMA_START && physical_addr <= DMA_END) {
        uint32_t offset = physical_addr - DMA_START;
        printf("~ Write32 to DMA region: Addr=0x%08x Offset=0x%x = 0x%08x\n", physical_addr, offset, value);
        bool channel_became_active = dma_write(&inter->dma, offset, value); // Delegate

        // If the write activated a channel control register, start the DMA transfer
        if (channel_became_active) {
             uint32_t channel_index = (offset >> 4) & 0x7;
             printf("  DMA Channel %d activated by write to offset 0x%x.\n", channel_index, offset);
             interconnect_perform_dma(inter, channel_index);
        }
        return;
    }

    // Memory Control Region (Includes IRQ regs handled above, RAM_SIZE reg)
    if (physical_addr >= MEM_CONTROL_START && physical_addr <= MEM_CONTROL_END) {
        // Handle specific MemCtrl writes, ignore others silently for now
        switch (physical_addr) {
            case EXPANSION_1_BASE_ADDR: // 0x1f801000
                if (value != 0x1f000000) fprintf(stderr, "Warning: Bad Expansion 1 base address write: 0x%08x\n", value);
                else printf("~ Write32 to EXP1_BASE_ADDR = 0x%08x\n", value);
                break;
            case EXPANSION_2_BASE_ADDR: // 0x1f801004
                 if (value != 0x1f802000) fprintf(stderr, "Warning: Bad Expansion 2 base address write: 0x%08x\n", value);
                 else printf("~ Write32 to EXP2_BASE_ADDR = 0x%08x\n", value);
                break;
            case RAM_SIZE_ADDR: // 0x1f801060
                printf("~ Write32 to RAM_SIZE register (0x1f801060) = 0x%08x (Ignoring)\n", value);
                break;
            // IRQ regs handled above
            default:
                printf("~ Write32 to Unknown MEM_CONTROL addr 0x%08x = 0x%08x (Ignoring)\n", physical_addr, value);
                break;
        }
        return;
    }

    // Timer Region
    if (physical_addr >= TIMERS_START && physical_addr <= TIMERS_END) {
         printf("~ Write32 to TIMERS region: Addr 0x%08x = 0x%08x (Ignoring)\n", physical_addr, value);
         // TODO: Implement Timer register writes
         return;
    }

    // SPU Region
    if (physical_addr >= SPU_START && physical_addr <= SPU_END) {
         // printf("~ Write32 to SPU region: Address 0x%08x = 0x%08x (Ignoring)\n", physical_addr, value); // Noisy
         return; // SPU not implemented
    }

    // Main RAM Region
    if (physical_addr <= RAM_END) {
        // printf("~ Write32 to RAM region: Addr=0x%08x = 0x%08x\n", physical_addr, value); // Very noisy
        ram_store32(inter->ram, physical_addr, value); // Delegate
        return;
    }

    // BIOS Region (Read-Only)
    if (physical_addr >= BIOS_START && physical_addr <= BIOS_END) {
        fprintf(stderr, "Error: Write attempt to BIOS ROM at address: 0x%08x = 0x%08x\n",
                physical_addr, value);
        return; // Writes to BIOS are ignored/prohibited
    }

    // Expansion Regions (Generally ignored)
    if ((physical_addr >= EXPANSION_1_START && physical_addr <= EXPANSION_1_END) ||
        (physical_addr >= EXPANSION_2_START && physical_addr <= EXPANSION_2_END)) {
        printf("~ Write32 to Expansion region: Address 0x%08x = 0x%08x (Ignoring)\n", physical_addr, value);
        return;
    }


    // --- Fallback ---
    fprintf(stderr, "Unhandled physical memory write32 at address: 0x%08x = 0x%08x (Mapped from 0x%08x)\n",
            physical_addr, value, address);
}

/**
 * @brief Handles 16-bit memory writes from the CPU.
 * @param inter The Interconnect instance.
 * @param address Virtual address to write to.
 * @param value The 16-bit value to write.
 */
void interconnect_store16(Interconnect* inter, uint32_t address, uint16_t value) {
    // Check alignment
    if (address % 2 != 0) {
        // TODO: Trigger Address Error Store exception
        fprintf(stderr, "Unaligned store16 address: 0x%08x = 0x%04x\n", address, value);
        return;
    }
    uint32_t physical_addr = mask_region(address);

    // --- Check Timer Range --- <<< ADD THIS BLOCK
    if (physical_addr >= TIMERS_START && physical_addr <= TIMERS_END) {
        uint32_t timer_base_offset = physical_addr - TIMERS_START;
        int timer_index = timer_base_offset / 0x10;
        uint32_t register_offset = physical_addr & 0xF;

        // printf("~ Write16 to Timer %d Offset 0x%x = 0x%04x\n", timer_index, register_offset, value);
        timer_write16(&inter->timers_state, timer_index, register_offset, value);
        return; // Handled
     }
    // Interrupt Controller Registers
    if (physical_addr == IRQ_STATUS_ADDR) { // 0x1f801070 (I_STAT)
        uint16_t ack_mask = value & 0x7FF;
        inter->irq_status &= ~ack_mask;
        printf("~ Write16 to IRQ_STATUS (Ack): Value=0x%04x -> I_STAT=0x%04x\n", value, inter->irq_status);
        return;
    }
     if (physical_addr == IRQ_MASK_ADDR) { // 0x1f801074 (I_MASK)
        inter->irq_mask = value & 0x7FF;
        printf("~ Write16 to IRQ_MASK: Value=0x%04x -> I_MASK=0x%04x\n", value, inter->irq_mask);
        return;
     }

    // SPU Region
    if (physical_addr >= SPU_START && physical_addr <= SPU_END) {
         // printf("~ Write16 to SPU region: Address 0x%08x = 0x%04x (Ignoring)\n", physical_addr, value); // Noisy
         return; // SPU not implemented
    }

    // Timer Region
     if (physical_addr >= TIMERS_START && physical_addr <= TIMERS_END) {
         printf("~ Write16 to TIMERS region: Addr 0x%08x = 0x%04x (Ignoring)\n", physical_addr, value);
         // TODO: Implement Timer register writes (Mode, Target are 16-bit)
         return;
     }

    // Main RAM Region
    if (physical_addr <= RAM_END) {
        // printf("~ Write16 to RAM region: Addr=0x%08x = 0x%04x\n", physical_addr, value); // Noisy
        ram_store16(inter->ram, physical_addr, value); // Delegate
        return;
    }

    // Memory Control Region (General - unlikely 16-bit writes)
     if (physical_addr >= MEM_CONTROL_START && physical_addr <= MEM_CONTROL_END) {
         printf("~ Write16 to MEM_CONTROL region: Addr 0x%08x = 0x%04x (Ignoring)\n", physical_addr, value);
         return;
     }

    // BIOS Region (Read-Only)
    if (physical_addr >= BIOS_START && physical_addr <= BIOS_END) {
        fprintf(stderr, "Error: Write16 attempt to BIOS ROM at address: 0x%08x = 0x%04x\n",
                physical_addr, value);
        return;
    }

    // GPU Region (Unlikely 16-bit writes)
    if (physical_addr >= GPU_START && physical_addr <= GPU_END) {
         fprintf(stderr, "Warning: Unhandled GPU write16 at 0x%08x = 0x%04x\n", physical_addr, value);
         return;
    }

    // DMA Region (Unlikely 16-bit writes)
     if (physical_addr >= DMA_START && physical_addr <= DMA_END) {
        fprintf(stderr, "Warning: Unhandled DMA write16 at 0x%08x = 0x%04x\n", physical_addr, value);
        return;
    }

    // Expansion Regions
    if ((physical_addr >= EXPANSION_1_START && physical_addr <= EXPANSION_1_END) ||
        (physical_addr >= EXPANSION_2_START && physical_addr <= EXPANSION_2_END)) {
        printf("~ Write16 to Expansion region: Address 0x%08x = 0x%04x (Ignoring)\n", physical_addr, value);
        return;
    }

    // --- Fallback ---
    fprintf(stderr, "Unhandled physical memory write16 at address: 0x%08x = 0x%04x (Mapped from 0x%08x)\n",
            physical_addr, value, address);
}

/**
 * @brief Handles 8-bit memory writes from the CPU.
 * @param inter The Interconnect instance.
 * @param address Virtual address to write to.
 * @param value The 8-bit value to write.
 */
void interconnect_store8(Interconnect* inter, uint32_t address, uint8_t value) {
    uint32_t physical_addr = mask_region(address);

    // --- Check Timer Range --- <<< ADD THIS BLOCK
    if (physical_addr >= TIMERS_START && physical_addr <= TIMERS_END) {
        // 8-bit writes to timers are generally undefined or write partial registers.
        fprintf(stderr, "Warning: Unhandled 8-bit write to Timer range: 0x%08x = 0x%02x\n", physical_addr, value);
        // Ignoring is safest for now.
        return;
    }
    // CDROM Registers
    if (physical_addr >= 0x1f801800 && physical_addr <= 0x1f801803) {
        printf("~ Write8 to CDROM Reg (0x%08x) = 0x%02x (Ignoring)\n", physical_addr, value);
        // TODO: Implement CDROM register writes
        return;
    }

     // Expansion 2 Region
    if (physical_addr >= EXPANSION_2_START && physical_addr <= EXPANSION_2_END) {
         // printf("~ Write8 to Expansion 2 region: 0x%08x = 0x%02x (Ignoring)\n", physical_addr, value); // Noisy
         return; // Expansion 2 typically unused/debug
    }

    // SPU Region
    if (physical_addr >= SPU_START && physical_addr <= SPU_END) {
         // printf("~ Write8 to SPU region: Address 0x%08x = 0x%02x (Ignoring)\n", physical_addr, value); // Noisy
         return; // SPU not implemented
    }

     // Main RAM Region
    if (physical_addr <= RAM_END) {
        // printf("~ Write8 to RAM: Addr=0x%08x = 0x%02x\n", physical_addr, value); // Very noisy
        ram_store8(inter->ram, physical_addr, value); // Delegate
        return;
    }

    // Memory Control Region
    if (physical_addr >= MEM_CONTROL_START && physical_addr <= MEM_CONTROL_END) {
         printf("~ Write8 to MEM_CONTROL region: 0x%08x = 0x%02x (Ignoring)\n", physical_addr, value);
         return; // Unlikely target for 8-bit writes
    }

    // BIOS Region (Read-Only)
    if (physical_addr >= BIOS_START && physical_addr <= BIOS_END) {
        fprintf(stderr, "Error: Write8 attempt to BIOS ROM at address: 0x%08x = 0x%02x\n", physical_addr, value);
        return;
    }

    // Expansion 1 Region
    if (physical_addr >= EXPANSION_1_START && physical_addr <= EXPANSION_1_END) {
        printf("~ Write8 to Expansion 1 region: Address 0x%08x = 0x%02x (Ignoring)\n", physical_addr, value);
        return;
    }

    // --- Fallback ---
    fprintf(stderr, "Unhandled physical memory write8 at address: 0x%08x = 0x%02x (Mapped from 0x%08x)\n",
            physical_addr, value, address);
}


// --- DMA Transfer Logic ---
// (Based on Guide Section 3.7, 3.8, 3.9, 3.10)

// Helper to calculate transfer size for Block/Manual modes
static uint32_t dma_get_transfer_size_words(DmaChannel* ch) {
    if (ch->sync == LINKED_LIST) return 0; // Size is determined by list content

    uint32_t bs = (uint32_t)ch->block_size;
    // In Manual mode (Sync=0), BlockSize (BC/BA field) is the word count.
    // 0 means max size (0x10000 words).
    if (ch->sync == MANUAL) {
        return (bs == 0) ? 0x10000 : bs;
    }

    // In Request mode (Sync=1), size is BlockCount * BlockSize
    uint32_t bc = (uint32_t)ch->block_count;
    if (bs == 0 || bc == 0) {
        fprintf(stderr, "Warning: DMA Request sync with zero size/count (BS=%u, BC=%u)\n", bs, bc);
        return 0; // Invalid size for Request mode
    }
    return bs * bc;
}

/**
 * @brief Executes a DMA transfer for the specified channel.
 * Called when a channel becomes active after a register write.
 * Handles OTC, GPU Linked List, and placeholder for other block transfers.
 * @param inter The Interconnect instance.
 * @param channel_index The DMA channel number (0-6).
 */
static void interconnect_perform_dma(Interconnect* inter, uint32_t channel_index) {
    if (channel_index >= 7) {
        fprintf(stderr, "Error: interconnect_perform_dma called with invalid channel index %u\n", channel_index);
        return;
    }

    printf("--- Starting DMA Transfer for Channel %d ---\n", channel_index);
    DmaChannel* ch = &inter->dma.channels[channel_index];
    DmaSync sync_mode = ch->sync;

    switch (sync_mode) {
        case LINKED_LIST:
            // Primarily used for GPU Channel 2
            if (channel_index == 2 && ch->direction == FROM_RAM) {
                uint32_t addr = ch->base_addr & 0x00FFFFFC; // Start address from MADR
                printf("DMA GPU Linked List: Starting at 0x%08x\n", addr);
                while(1) {
                    // Check address bounds before reading header
                    if (addr >= RAM_SIZE) {
                        fprintf(stderr, "DMA GPU LL Error: Header address 0x%08x out of RAM bounds.\n", addr);
                        break;
                    }
                    // Read header: size in high byte, next address in low 24 bits
                    uint32_t header = interconnect_load32(inter, addr); // Use interconnect load
                    uint32_t num_words = header >> 24;
                    uint32_t next_addr = header & 0x00FFFFFC; // Mask to word boundary
                    // printf("  LL Header @ 0x%08x: Value=0x%08x, Size=%u words, Next=0x%08x\n", addr, header, num_words, next_addr); // Debug

                    // Transfer packet words (if any)
                    if (num_words > 0) {
                         for (uint32_t i = 0; i < num_words; ++i) {
                            addr = (addr + 4) & 0x00FFFFFC; // Advance address for command word
                            if (addr >= RAM_SIZE) { // Check bounds before reading command
                                fprintf(stderr, "DMA GPU LL Error: Command address 0x%08x out of RAM bounds.\n", addr);
                                next_addr = 0xFFFFFF; // Force stop after this packet
                                break; // Exit inner loop
                            }
                            uint32_t command_word = interconnect_load32(inter, addr); // Read command
                            gpu_gp0(&inter->gpu, command_word); // Send command to GPU GP0 port
                        }
                        if (next_addr == 0xFFFFFF) break; // Break outer loop if error occurred
                    }

                    // Check for end-of-list marker (Top bit of next_addr usually, or 0xFFFFFF) [cite: 1808]
                    if ((header & 0x800000) != 0) { // Check MSB of address field as per Mednafen comment
                        printf("DMA GPU Linked List: End marker (0x800000) found in header 0x%08x.\n", header);
                        break;
                    }
                    // Check for explicit 0xFFFFFF marker (safer)
                    if (next_addr == 0xFFFFFF) {
                        printf("DMA GPU Linked List: End marker (0xFFFFFF) found.\n");
                         break;
                    }

                    // Check next address validity before proceeding
                     if (next_addr >= RAM_SIZE) {
                         fprintf(stderr, "DMA GPU LL Error: Next header address 0x%08x out of RAM bounds.\n", next_addr);
                         break;
                     }
                    // Move to the next header address
                    addr = next_addr;
                }
                printf("DMA GPU Linked List: Finished.\n");
            } else {
                 fprintf(stderr, "Error: Linked List DMA mode attempted on unsupported channel (%d) or direction (%d).\n", channel_index, ch->direction);
            }
            break;

        case MANUAL:
        case REQUEST:
            {
                uint32_t words_to_transfer = dma_get_transfer_size_words(ch);
                if (words_to_transfer == 0) {
                    printf("Warning: DMA Block/Request transfer started with zero size for channel %d.\n", channel_index);
                    break; // Nothing to do
                }

                uint32_t addr = ch->base_addr & 0x00FFFFFC; // Start address
                int32_t step = (ch->step == INCREMENT) ? 4 : -4;
                printf("DMA Block/Request: Chan=%d, Dir=%s, Sync=%s, Step=%d, Addr=0x%08x, Size=%u words\n",
                       channel_index, (ch->direction == FROM_RAM ? "FROM_RAM" : "TO_RAM"),
                       (sync_mode == MANUAL ? "MANUAL" : "REQUEST"), step, addr, words_to_transfer);

                for (uint32_t i = 0; i < words_to_transfer; ++i) {
                    // Ensure address stays within RAM bounds (mask low bits, check high bits)
                    uint32_t current_addr_masked = addr & 0x001FFFFC; // Mask address to stay within 2MB and word aligned
                    if (current_addr_masked >= RAM_SIZE) {
                         fprintf(stderr, "DMA Block Error: Address 0x%08x (masked 0x%08x) out of RAM bounds on channel %d.\n", addr, current_addr_masked, channel_index);
                         break; // Stop transfer if address goes out of bounds
                    }

                    if (ch->direction == FROM_RAM) {
                        // RAM -> Peripheral
                        uint32_t data_word = interconnect_load32(inter, current_addr_masked); // Read from RAM
                        switch (channel_index) {
                            case 2: // GPU
                                gpu_gp0(&inter->gpu, data_word); // Send data word to GP0 (for Image Load etc.)
                                break;
                            // Add cases for other peripherals (CDROM, SPU, MDEC) here
                            default:
                                printf("Warning: Unhandled DMA Block FROM_RAM transfer for channel %d, Addr=0x%08x, Data=0x%08x\n",
                                       channel_index, current_addr_masked, data_word);
                                break;
                        }
                    } else { // TO_RAM
                        // Peripheral -> RAM
                        uint32_t data_word = 0; // Default value if peripheral not handled
                        switch (channel_index) {
                            case 6: // OTC - Ordering Table Clear
                                // Value depends on position in transfer
                                data_word = (i == (words_to_transfer - 1)) // Is it the last word?
                                            ? 0x00FFFFFF                  // Yes: End marker
                                            : ((addr - 4) & 0x00FFFFFC); // No: Pointer to previous entry
                                break;
                            // Add cases for other peripherals reading TO RAM (CDROM, SPU, MDEC)
                            default:
                                printf("Warning: Unhandled DMA Block TO_RAM transfer for channel %d, Addr=0x%08x\n",
                                       channel_index, current_addr_masked);
                                break;
                        }
                        interconnect_store32(inter, current_addr_masked, data_word); // Write to RAM
                    }

                    // Advance address for next word
                    addr = (uint32_t)((int32_t)addr + step); // Apply step
                }
                 printf("DMA Block/Request: Finished transfer for channel %d.\n", channel_index);
            }
            break;

        default: // Should not happen if sync enum is correct
            fprintf(stderr, "Error: Unknown DMA Sync mode %d encountered for channel %d.\n", sync_mode, channel_index);
            break;
    }

    // Mark the channel as finished (clears enable/trigger bits)
    dma_channel_done(ch);
    printf("--- Finished DMA Transfer Processing for Channel %d ---\n", channel_index);

    // TODO: Trigger DMA interrupt here if enabled in DICR and channel IRQ was set
}