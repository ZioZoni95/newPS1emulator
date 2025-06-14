#ifndef INTERCONNECT_H // Include guard
#define INTERCONNECT_H

#include <stdint.h>       // For uint32_t, uint16_t etc.
#include <stdbool.h>      // For bool type

// Include headers for components accessed via the interconnect
#include "bios.h"
#include "ram.h"
#include "dma.h"
#include "gpu.h"
#include "timers.h"
#include "cdrom.h"


/* --- Memory Map Definitions (Physical Addresses) ---
 * These define the physical address ranges used by the interconnect
 * after mapping the CPU's virtual address.
 */
#define TIMERS_START 0x1f801100
#define TIMERS_SIZE  0x30 // Covers Timers 0, 1, 2
#define TIMERS_END   (TIMERS_START + TIMERS_SIZE - 1)

// Main RAM (2 Megabytes)
#define RAM_START 0x00000000
#define RAM_SIZE  (2 * 1024 * 1024)
#define RAM_END   (RAM_START + RAM_SIZE - 1)

// BIOS ROM (512 Kilobytes)
#define BIOS_START 0x1fc00000 // Physical start address
#define BIOS_SIZE  (512 * 1024)
#define BIOS_END   (BIOS_START + BIOS_SIZE - 1)

// Scratchpad (1 Kilobyte Data Cache RAM)
#define SCRATCHPAD_START 0x1f800000
#define SCRATCHPAD_SIZE  1024
#define SCRATCHPAD_END   (SCRATCHPAD_START + SCRATCHPAD_SIZE - 1)

// Memory Control Registers (Expansion Base, RAM Size)
#define MEM_CONTROL_START 0x1f801000
#define MEM_CONTROL_SIZE  0x80 // Covers known registers up to IRQ mask+4
#define MEM_CONTROL_END   (MEM_CONTROL_START + MEM_CONTROL_SIZE - 1)
#define EXPANSION_1_BASE_ADDR 0x1f801000
#define EXPANSION_2_BASE_ADDR 0x1f801004
#define RAM_SIZE_ADDR         0x1f801060

// Interrupt Control Registers (I_STAT, I_MASK)
#define IRQ_REGS_START        0x1f801070
#define IRQ_STATUS_ADDR       0x1f801070 // Read: Pending IRQs / Write: Acknowledge IRQs
#define IRQ_MASK_ADDR         0x1f801074 // Read/Write: Enable/Disable IRQ lines
#define IRQ_REGS_END          (IRQ_REGS_START + 8 - 1) // Covers I_STAT and I_MASK

// DMA Registers
#define DMA_START 0x1f801080
#define DMA_SIZE  0x80 // Covers common registers and 7 channels
#define DMA_END   (DMA_START + DMA_SIZE - 1)

// Timer Registers
#define TIMERS_START 0x1f801100
#define TIMERS_SIZE  0x30 // Covers Timers 0, 1, 2
#define TIMERS_END   (TIMERS_START + TIMERS_SIZE - 1)

// SPU (Sound Processing Unit) Registers
#define SPU_START 0x1f801C00
#define SPU_SIZE  640 // From Nocash specs
#define SPU_END   (SPU_START + SPU_SIZE - 1)

// GPU Registers (GP0, GP1/GPUSTAT)
#define GPU_START 0x1f801810
#define GPU_SIZE  8 // GP0/GPUREAD (0x1810) and GP1/GPUSTAT (0x1814)
#define GPU_END   (GPU_START + GPU_SIZE - 1)
#define GPU_GP0_ADDR     0x1f801810 // Write address for GP0 commands
#define GPU_GPUREAD_ADDR 0x1f801810 // Read address for GPUREAD fifo
#define GPU_GP1_ADDR     0x1f801814 // Write address for GP1 commands
#define GPU_GPUSTAT_ADDR 0x1f801814 // Read address for GPUSTAT register

// Expansion Region 1 (Parallel Port)
#define EXPANSION_1_START 0x1f000000
#define EXPANSION_1_SIZE  (8 * 1024 * 1024)
#define EXPANSION_1_END   (EXPANSION_1_START + EXPANSION_1_SIZE - 1)

// Expansion Region 2 (Debug/Dev Hardware)
#define EXPANSION_2_START 0x1f802000
#define EXPANSION_2_SIZE  66
#define EXPANSION_2_END   (EXPANSION_2_START + EXPANSION_2_SIZE - 1)

// Cache Control Register (KSEG2)
#define CACHE_CONTROL_ADDR 0xfffe0130

/* --- Interrupt Line Definitions ---
 * Defines symbolic names for the PSX hardware interrupt request lines (0-10).
 * These correspond to bits in the I_STAT and I_MASK registers.
 */
#define IRQ_VBLANK    0  // GPU VBlank interrupt
#define IRQ_GPU       1  // GPU interrupt (origin depends on GPU config)
#define IRQ_CDROM     2  // CD-ROM controller interrupt
#define IRQ_DMA       3  // DMA controller interrupt
#define IRQ_TIMER0    4  // Timer 0 interrupt
#define IRQ_TIMER1    5  // Timer 1 interrupt
#define IRQ_TIMER2    6  // Timer 2 interrupt
#define IRQ_CTRLMEMCARD 7 // Controller and Memory Card interrupt
#define IRQ_SIO       8  // Serial I/O interrupt (SIO0/SIO1)
#define IRQ_SPU       9  // Sound Processing Unit interrupt
#define IRQ_PIO      10 // PIO (Controller?) interrupt (Lightpen?)


/* --- Interconnect Structure Definition ---
 * Holds pointers/instances of all main system components accessed via the bus.
 * Routes memory accesses from the CPU to the correct component.
 */
typedef struct Interconnect {
    Bios* bios; // Pointer to the loaded BIOS data
    Ram* ram;   // Pointer to the main RAM data buffer
    Gpu gpu;    // GPU state (embedded directly)
    Dma dma;    // DMA controller state (embedded directly)

    Cdrom *cdrom;
    // --- Interrupt Controller State ---
    uint16_t irq_status; // I_STAT Register state (reflects pending IRQs)
    uint16_t irq_mask;   // I_MASK Register state (enables/disables IRQs)
    // --------------------------------
    Timers timers_state; // <<< ADD THIS MEMBER

    // Add pointers/state for other peripherals here later (Timers, SPU, CDROM, etc.)

} Interconnect;

/* --- Function Declarations (Prototypes) --- */

/**
 * @brief Initializes the Interconnect structure.
 * Stores pointers to BIOS/RAM, initializes embedded peripherals (GPU, DMA),
 * and resets interrupt controller state.
 * @param inter Pointer to the Interconnect struct to initialize.
 * @param bios Pointer to the initialized Bios struct.
 * @param ram Pointer to the initialized Ram struct.
 */
void interconnect_init(Interconnect* inter, Bios* bios, Ram* ram, Cdrom *cdrom);

/**
 * @brief Maps a CPU virtual address (KUSEG/KSEG0/KSEG1) to a physical address.
 * Based on Guide Section 2.38.
 * @param addr The 32-bit virtual address from the CPU.
 * @return The corresponding 32-bit physical address.
 */
uint32_t mask_region(uint32_t addr);

/**
 * @brief Reads a 32-bit word from the emulated system memory space.
 * Handles address mapping, routes the read, checks alignment.
 * @param inter Pointer to the Interconnect instance.
 * @param address The 32-bit virtual address requested by the CPU.
 * @return The 32-bit value read. Returns 0 on unhandled/error cases for now.
 */
uint32_t interconnect_load32(Interconnect* inter, uint32_t address);

/**
 * @brief Reads a 16-bit halfword from the emulated system memory space.
 * Handles address mapping, routes the read, checks alignment.
 * @param inter Pointer to the Interconnect instance.
 * @param address The 32-bit virtual address requested by the CPU.
 * @return The 16-bit value read. Returns 0 on unhandled/error cases for now.
 */
uint16_t interconnect_load16(Interconnect* inter, uint32_t address);

/**
 * @brief Reads an 8-bit byte from the emulated system memory space.
 * Handles address mapping and routes the read.
 * @param inter Pointer to the Interconnect instance.
 * @param address The 32-bit virtual address requested by the CPU.
 * @return The 8-bit value read. Returns 0 on unhandled reads for now.
 */
uint8_t interconnect_load8(Interconnect* inter, uint32_t address);

/**
 * @brief Writes a 32-bit word to the emulated system memory space.
 * Handles address mapping, routes the write, checks alignment.
 * @param inter Pointer to the Interconnect instance.
 * @param address The 32-bit virtual address targeted by the CPU.
 * @param value The 32-bit value to write.
 */
void interconnect_store32(Interconnect* inter, uint32_t address, uint32_t value);

/**
 * @brief Writes a 16-bit halfword to the emulated system memory space.
 * Handles address mapping, routes the write, checks alignment.
 * @param inter Pointer to the Interconnect instance.
 * @param address The 32-bit virtual address targeted by the CPU.
 * @param value The 16-bit value to write.
 */
void interconnect_store16(Interconnect* inter, uint32_t address, uint16_t value);

/**
 * @brief Writes an 8-bit byte to the emulated system memory space.
 * Handles address mapping and routes the write.
 * @param inter Pointer to the Interconnect instance.
 * @param address The 32-bit virtual address targeted by the CPU.
 * @param value The 8-bit value to write.
 */
void interconnect_store8(Interconnect* inter, uint32_t address, uint8_t value);

/**
 * @brief Called by peripherals to signal an interrupt request.
 * Sets the corresponding bit in the I_STAT register.
 * @param inter Pointer to the Interconnect instance.
 * @param irq_line The interrupt line number (0-10) to request.
 */
void interconnect_request_irq(Interconnect* inter, uint32_t irq_line);


#endif // INTERCONNECT_H