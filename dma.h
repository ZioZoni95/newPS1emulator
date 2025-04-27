#ifndef DMA_H
#define DMA_H

#include <stdint.h>
#include <stdbool.h> // Include if using bool

// --- Enums for Channel Control (CHCR - Section 3.3) ---
typedef enum {
    TO_RAM = 0,    // Peripheral to RAM
    FROM_RAM = 1   // RAM to Peripheral
} DmaDirection;

typedef enum {
    INCREMENT = 0,
    DECREMENT = 1
} DmaStep;

typedef enum {
    MANUAL = 0,      // Start transfer via CHCR Trigger bit
    REQUEST = 1,     // Sync blocks to DRQ signals
    LINKED_LIST = 2  // Used for GPU command lists
} DmaSync;


// --- Structure for a single DMA Channel ---
typedef struct {
    // CHCR - Channel Control Register (Offset 0xX8)
    bool enable;                 // Bit 24: Channel Enable
    DmaDirection direction;      // Bit 0: Transfer Direction
    DmaStep step;                // Bit 1: Address Step (Inc/Dec)
    // Bit 8: Chopping Enable (Not implemented yet)
    DmaSync sync;                // Bits 9-10: Sync Mode (Manual, Request, Linked List)
    bool trigger;                // Bit 28: Manual Trigger (for Manual Sync)
    // uint8_t chopping_dma_sz; // Bits 16-18 (Not implemented)
    // uint8_t chopping_cpu_sz; // Bits 20-22 (Not implemented)
    // uint8_t chcr_unknown_rw; // Bits 29-30 (Not implemented/stored yet)

    // MADR - Base Address Register (Offset 0xX0)
    uint32_t base_addr;          // Effective address (lower 24 bits)

    // BCR - Block Control Register (Offset 0xX4)
    uint16_t block_size;         // BC/BA field (Word count or block size)
    uint16_t block_count;        // BS field (Block count for Request sync)

} DmaChannel;


// --- Main DMA State Structure ---
typedef struct {
    // DPCR - DMA Control Register (Offset 0x70)
    uint32_t control;

    // DICR - DMA Interrupt Register (Offset 0x74)
    bool force_irq;
    uint8_t channel_irq_enable;
    bool master_irq_enable;
    uint8_t channel_irq_flags;
    bool master_irq_flag;
    uint8_t dicr_unknown_rw;

    // Array of 7 DMA Channels
    DmaChannel channels[7];

} Dma;

// --- Function Prototypes ---
void dma_init(Dma* dma);
uint32_t dma_read(Dma* dma, uint32_t offset);
// Return bool to indicate if a channel became active
bool dma_write(Dma* dma, uint32_t offset, uint32_t value); // <-- Return type changed here
bool dma_channel_is_active(DmaChannel* ch);
void dma_channel_done(DmaChannel* ch);

// Helper to get channel control register value
uint32_t channel_get_control(DmaChannel* ch); // <-- Declaration added
// Helper to set channel control register value
void channel_set_control(DmaChannel* ch, uint32_t value); // <-- Declaration added


#endif // DMA_H