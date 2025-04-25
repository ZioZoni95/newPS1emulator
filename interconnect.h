#ifndef INTERCONNECT_H // Include guard: Prevents defining things multiple times
#define INTERCONNECT_H

#include <stdint.h>       // Standard header for fixed-width integer types like uint32_t
#include "bios.h"         // Needs the definition of the Bios struct for the pointer below
#include "ram.h"          // <-- Added include for Ram struct definition

/* --- Memory Map Definitions (Physical Addresses) ---
 * These definitions represent the physical address ranges for various
 * components mapped into the PlayStation's memory space.
 * The interconnect uses these physical addresses after mapping
 * the CPU's virtual addresses (KUSEG/KSEG0/KSEG1).
 * References primarily based on the guide's memory map tables and sections.
 */

// Main RAM (2 Megabytes) (Guide §2.5, §2.34) [cite: 84, 459]
#define RAM_START 0x00000000
#define RAM_SIZE  (2 * 1024 * 1024)
#define RAM_END   (RAM_START + RAM_SIZE - 1)

// BIOS ROM (512 Kilobytes) (Guide §2.5, §2.6) [cite: 84, 102]
#define BIOS_START 0x1fc00000 // Physical start address
#define BIOS_SIZE  (512 * 1024)
#define BIOS_END   (BIOS_START + BIOS_SIZE - 1)

// Memory Control Region (Guide §2.16.2, §2.53, §2.21) [cite: 258, 623, 304]
// Contains Expansion Base Address registers, RAM_SIZE register, Interrupt Control, etc.
#define MEM_CONTROL_START 0x1f801000
// Guide mentions registers up to 0x1f801020 (RAM_SIZE), 0x1f801070/74 (IRQ).
// Let's use a slightly larger range for safety, covering known registers.
// Nocash indicates IO ports up to 0x1f801074+. A size of 0x80 covers this.
#define MEM_CONTROL_SIZE  0x80 // Size covering known registers up to IRQ mask+4
#define MEM_CONTROL_END   (MEM_CONTROL_START + MEM_CONTROL_SIZE - 1)
// Specific registers within this range:
#define EXPANSION_1_BASE_ADDR 0x1f801000 // [cite: 258]
#define EXPANSION_2_BASE_ADDR 0x1f801004 // [cite: 258]
#define RAM_SIZE_ADDR         0x1f801060 // [cite: 304]
#define IRQ_STATUS_ADDR       0x1f801070 // [cite: 628]
#define IRQ_MASK_ADDR         0x1f801074 // [cite: 623]

// Scratchpad (1 Kilobyte Data Cache RAM) (Guide §2.5, §2.38) [cite: 84, 501]
#define SCRATCHPAD_START 0x1f800000
#define SCRATCHPAD_SIZE  1024
#define SCRATCHPAD_END   (SCRATCHPAD_START + SCRATCHPAD_SIZE - 1)

// Hardware Registers (General I/O Ports, Timers, SPU, PIO, SIO etc.) (Guide §2.5) [cite: 84]
// This is a broad range containing many different peripherals.
#define HW_REGS_START 0x1f801000 // Note: Overlaps MEM_CONTROL, SPU, TIMERS etc.
#define HW_REGS_END   0x1f802FFF // Approximate end covering known standard registers

// SPU (Sound Processing Unit) Register Range (Guide §2.40) [cite: 529]
#define SPU_START 0x1f801C00
#define SPU_SIZE  640 // From Nocash specs
#define SPU_END   (SPU_START + SPU_SIZE - 1)

// Expansion Region 1 (Parallel Port on early models) (Guide §2.5, §2.48) [cite: 84, 571]
#define EXPANSION_1_START 0x1f000000
#define EXPANSION_1_SIZE  (8 * 1024 * 1024) // 8MB, though often unused/smaller devices
#define EXPANSION_1_END   (EXPANSION_1_START + EXPANSION_1_SIZE - 1)

// Expansion Region 2 (Used for Development Hardware/Debugging) (Guide §2.44) [cite: 547]
#define EXPANSION_2_START 0x1f802000
#define EXPANSION_2_SIZE  66 // From guide
#define EXPANSION_2_END   (EXPANSION_2_START + EXPANSION_2_SIZE - 1)

// Timer Registers (Guide §2.70, §2.91) [cite: 803, 1180]
#define TIMERS_START 0x1f801100
#define TIMERS_SIZE  0x30 // Covers Timers 0, 1, 2
#define TIMERS_END   (TIMERS_START + TIMERS_SIZE - 1)

// DMA Registers (Guide §2.81, Section 3) [cite: 1018, 1479]
#define DMA_START 0x1f801080
#define DMA_SIZE  0x80 // Covers common registers DPCR, DICR, and 7 channels
#define DMA_END   (DMA_START + DMA_SIZE - 1)

// GPU Registers (GP0, GP1/GPUSTAT) (Guide §2.89, Section 4) [cite: 1136, 1835]
#define GPU_START 0x1f801810
#define GPU_SIZE  8 // GP0 (0x1810) and GP1/GPUSTAT (0x1814)
#define GPU_END   (GPU_START + GPU_SIZE - 1)

// Cache Control Register (KSEG2) (Guide §2.26) [cite: 366]
#define CACHE_CONTROL_ADDR 0xfffe0130


/* --- Interconnect Structure Definition ---
 * Holds pointers to all the main components of the emulated system
 * that are connected via the memory bus (BIOS, RAM, GPU, DMA, etc.).
 * The Interconnect module uses these pointers to route memory accesses
 * requested by the CPU to the appropriate component.
 */
typedef struct {
    Bios* bios; // Pointer to the loaded BIOS data structure.
    Ram* ram;   // <-- Pointer to the RAM data structure
    // Gpu* gpu;        // To be added
    // Dma* dma;        // To be added
    // ... pointers to SPU, Timers, CDROM, Controllers etc. ...
} Interconnect;

/* --- Function Declarations (Prototypes) --- */

/**
 * @brief Initializes the Interconnect structure.
 * Stores pointers to the BIOS and RAM components.
 * @param inter Pointer to the Interconnect struct to initialize.
 * @param bios Pointer to the initialized Bios struct.
 * @param ram Pointer to the initialized Ram struct.
 */
void interconnect_init(Interconnect* inter, Bios* bios, Ram* ram);

/**
 * @brief Reads a 32-bit word from the emulated system memory space.
 * Handles address mapping (virtual to physical) and routes the read
 * request to the appropriate component (BIOS, RAM, Hardware Registers).
 * Checks for alignment errors.
 * @param inter Pointer to the Interconnect instance.
 * @param address The 32-bit virtual address requested by the CPU.
 * @return The 32-bit value read from the specified address. Returns 0 on unhandled reads or alignment errors for now.
 */
uint32_t interconnect_load32(Interconnect* inter, uint32_t address);

/**
 * @brief Reads a 16-bit halfword from the emulated system memory space.
 * Handles address mapping and routes the read to the appropriate component.
 * Checks for alignment errors.
 * @param inter Pointer to the Interconnect instance.
 * @param address The 32-bit virtual address requested by the CPU.
 * @return The 16-bit value read from the specified address. Returns 0 on unhandled reads or alignment errors for now.
 */
uint16_t interconnect_load16(Interconnect* inter, uint32_t address); // <-- Added prototype

/**
 * @brief Reads an 8-bit byte from the emulated system memory space.
 * Handles address mapping and routes the read to the appropriate component.
 * @param inter Pointer to the Interconnect instance.
 * @param address The 32-bit virtual address requested by the CPU.
 * @return The 8-bit value read from the specified address. Returns 0 on unhandled reads for now.
 */
uint8_t interconnect_load8(Interconnect* inter, uint32_t address); // <-- Added prototype

/**
 * @brief Writes a 32-bit word to the emulated system memory space.
 * Handles address mapping and routes the write request to the appropriate
 * component (RAM, Hardware Registers). Writes to BIOS are ignored.
 * Checks for alignment errors.
 * @param inter Pointer to the Interconnect instance.
 * @param address The 32-bit virtual address targeted by the CPU.
 * @param value The 32-bit value to write.
 */
void interconnect_store32(Interconnect* inter, uint32_t address, uint32_t value);

/**
 * @brief Writes a 16-bit halfword to the emulated system memory space.
 * Handles address mapping and routes the write request.
 * Checks for alignment errors.
 * @param inter Pointer to the Interconnect instance.
 * @param address The 32-bit virtual address targeted by the CPU.
 * @param value The 16-bit value to write.
 */
void interconnect_store16(Interconnect* inter, uint32_t address, uint16_t value);

/**
 * @brief Writes an 8-bit byte to the emulated system memory space.
 * Handles address mapping and routes the write request.
 * @param inter Pointer to the Interconnect instance.
 * @param address The 32-bit virtual address targeted by the CPU.
 * @param value The 8-bit value to write.
 */
void interconnect_store8(Interconnect* inter, uint32_t address, uint8_t value);


/**
 * @brief Maps a CPU virtual address (KUSEG/KSEG0/KSEG1) to a physical address.
 * KSEG2 addresses are passed through unchanged.
 * Based on Guide Section 2.38.
 * @param addr The 32-bit virtual address from the CPU.
 * @return The corresponding 32-bit physical address.
 */
uint32_t mask_region(uint32_t addr);


#endif // INTERCONNECT_H