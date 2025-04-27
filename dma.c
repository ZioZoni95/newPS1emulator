#include "dma.h"
#include <stdio.h> // For fprintf, stderr

// Helper function to get channel control register value
// REMOVED 'static'
uint32_t channel_get_control(DmaChannel* ch) {
    uint32_t r = 0;
    r |= (uint32_t)ch->direction;      // Bit 0
    r |= ((uint32_t)ch->step << 1);    // Bit 1
    // r |= ((uint32_t)ch->chopping << 8); // Bit 8 - Not implemented yet
    r |= ((uint32_t)ch->sync << 9);    // Bits 9-10
    // r |= ((uint32_t)ch->chopping_dma_sz << 16); // Bits 16-18 - Not implemented
    // r |= ((uint32_t)ch->chopping_cpu_sz << 20); // Bits 20-22 - Not implemented
    r |= ((uint32_t)ch->enable << 24); // Bit 24
    r |= ((uint32_t)ch->trigger << 28);// Bit 28
    // r |= ((uint32_t)ch->chcr_unknown_rw << 29); // Bits 29-30 - Not implemented
    return r;
}

// Helper function to set channel control register value
// REMOVED 'static'
void channel_set_control(DmaChannel* ch, uint32_t value) {
    ch->direction = (value & 1) ? FROM_RAM : TO_RAM;
    ch->step = ((value >> 1) & 1) ? DECREMENT : INCREMENT;
    // ch->chopping = (value >> 8) & 1; // Not implemented
    switch ((value >> 9) & 3) {
        case 0: ch->sync = MANUAL; break;
        case 1: ch->sync = REQUEST; break;
        case 2: ch->sync = LINKED_LIST; break;
        default:
            fprintf(stderr, "Warning: Invalid DMA Sync mode %d written to CHCR\n", (value >> 9) & 3);
            break;
    }
    // ch->chopping_dma_sz = (value >> 16) & 7; // Not implemented
    // ch->chopping_cpu_sz = (value >> 20) & 7; // Not implemented
    ch->enable = (value >> 24) & 1;
    ch->trigger = (value >> 28) & 1;
    // ch->chcr_unknown_rw = (value >> 29) & 3; // Not implemented
}

// Checks if a channel should start transferring based on its state.
bool dma_channel_is_active(DmaChannel* ch) {
    if (!ch->enable) {
        return false;
    }
    if (ch->sync == MANUAL) {
        return ch->trigger;
    }
    return true;
}

// Marks a channel as finished after a transfer.
void dma_channel_done(DmaChannel* ch) {
    ch->enable = false;
    ch->trigger = false;
    // TODO: Handle setting/clearing interrupt flags in DICR here later
}


// Initializes the DMA state to reset values.
void dma_init(Dma* dma) {
    // DPCR reset value
    dma->control = 0x07654321;

    // Initialize DICR fields
    dma->force_irq = false;
    dma->channel_irq_enable = 0;
    dma->master_irq_enable = false;
    dma->channel_irq_flags = 0;
    dma->master_irq_flag = false;
    dma->dicr_unknown_rw = 0;

    // Initialize all 7 channels to default values
    for (int i = 0; i < 7; ++i) {
        dma->channels[i].enable = false;
        dma->channels[i].direction = TO_RAM;
        dma->channels[i].step = INCREMENT;
        dma->channels[i].sync = MANUAL;
        dma->channels[i].trigger = false;
        dma->channels[i].base_addr = 0;
        dma->channels[i].block_size = 0;
        dma->channels[i].block_count = 0;
    }

    printf("DMA Initialized. DPCR=0x%08x, Channels initialized.\n", dma->control);
}

// Reads a 32-bit value from a DMA register address (relative offset).
uint32_t dma_read(Dma* dma, uint32_t offset) {
    uint32_t channel_index = (offset >> 4) & 0x7;
    uint32_t register_offset = offset & 0xF;

    if (channel_index < 7) { // Channel Register Access
        DmaChannel* ch = &dma->channels[channel_index];
        switch (register_offset) {
            case 0x0: // MADR
                return ch->base_addr;
            case 0x4: // BCR
                return ((uint32_t)ch->block_count << 16) | (uint32_t)ch->block_size;
            case 0x8: // CHCR
                return channel_get_control(ch);
            default:
                fprintf(stderr, "Warning: Unhandled DMA Channel read at offset 0x%x (Channel %d, Reg %x)\n", offset, channel_index, register_offset);
                return 0;
        }
    } else { // Main DMA Register Access
        switch (offset) {
            case 0x70: // DPCR
                return dma->control;
            case 0x74: // DICR
                {
                    uint32_t dicr = 0;
                    dicr |= (uint32_t)dma->dicr_unknown_rw & 0x3F;
                    dicr |= ((uint32_t)dma->force_irq << 15);
                    dicr |= ((uint32_t)dma->channel_irq_enable << 16);
                    dicr |= ((uint32_t)dma->master_irq_enable << 23);
                    dicr |= ((uint32_t)dma->channel_irq_flags << 24);
                    bool master_flag = dma->force_irq || (dma->master_irq_enable && ((dma->channel_irq_flags & dma->channel_irq_enable) != 0));
                    dicr |= ((uint32_t)master_flag << 31);
                    return dicr;
                }
            default:
                fprintf(stderr, "Error: Unhandled DMA Main register read at offset 0x%x\n", offset);
                return 0;
        }
    }
}

// Writes a 32-bit value to a DMA register address (relative offset).
// Returns true if the write made a channel active, false otherwise.
bool dma_write(Dma* dma, uint32_t offset, uint32_t value) {
    uint32_t channel_index = (offset >> 4) & 0x7;
    uint32_t register_offset = offset & 0xF;
    bool channel_became_active = false;

    if (channel_index < 7) { // Channel Register Access
        DmaChannel* ch = &dma->channels[channel_index];
        switch (register_offset) {
            case 0x0: // MADR
                ch->base_addr = value & 0x00FFFFFF;
                break;
            case 0x4: // BCR
                ch->block_size = (uint16_t)(value & 0xFFFF);
                ch->block_count = (uint16_t)(value >> 16);
                break;
            case 0x8: // CHCR
                channel_set_control(ch, value);
                // Check if this write activated the channel NOW
                channel_became_active = dma_channel_is_active(ch);
                break;
            default:
                fprintf(stderr, "Warning: Unhandled DMA Channel write at offset 0x%x = 0x%08x (Channel %d, Reg %x)\n", offset, value, channel_index, register_offset);
                break;
        }
    } else { // Main DMA Register Access
         switch (offset) {
            case 0x70: // DPCR
                dma->control = value;
                break;
            case 0x74: // DICR
                dma->dicr_unknown_rw = (uint8_t)(value & 0x3F);
                dma->force_irq = (value >> 15) & 1;
                dma->channel_irq_enable = (uint8_t)((value >> 16) & 0x7F);
                dma->master_irq_enable = (value >> 23) & 1;
                uint8_t ack_flags = (uint8_t)((value >> 24) & 0x7F);
                dma->channel_irq_flags &= ~ack_flags;
                break;
            default:
                fprintf(stderr, "Error: Unhandled DMA Main register write at offset 0x%x = 0x%08x\n", offset, value);
                break;
        }
    }
    return channel_became_active;
}