/**
 * cdrom.c
 * Implementation of the PlayStation CD-ROM Drive emulation.
 * Handles register access, command processing, disc reading (eventually),
 * and interrupt generation.
 */
#include "cdrom.h"
#include "interconnect.h" // For interrupt definitions/requests IRQ_CDROM
#include <stdio.h>
#include <string.h> // For memset
#include <stdlib.h> // For exit if needed
// #include <math.h>   // No longer needed here if floor() isn't used directly

// --- Forward Declarations for Command Handlers ---
static void cdrom_handle_command(Cdrom* cdrom, uint8_t command);
static void cmd_get_stat(Cdrom* cdrom);
static void cmd_init(Cdrom* cdrom);
static void cmd_get_id(Cdrom* cdrom);
static void cmd_set_loc(Cdrom* cdrom);
static void cmd_read_n(Cdrom* cdrom);
// TODO: Add prototypes for other commands (Pause, SeekL, Test etc.)

// --- FIFO Helpers (Basic Implementation) ---
// NOTE: A robust FIFO needs proper head/tail pointers, wrapping, and size checks.
// This is simplified for now, assuming low usage doesn't hit edge cases badly.

/**
 * @brief Initializes a simple 8-bit FIFO.
 * @param fifo Pointer to the Fifo8 structure.
 */
static void fifo_init(Fifo8* fifo) {
    fifo->count = 0;
    fifo->read_ptr = 0; // Points to the next byte to read
    memset(fifo->data, 0, FIFO_SIZE);
}

/**
 * @brief Pushes a byte onto the FIFO. Returns false if full.
 * @param fifo Pointer to the Fifo8 structure.
 * @param value The byte to push.
 * @return True if successful, false if FIFO was full.
 */
static bool fifo_push(Fifo8* fifo, uint8_t value) {
    if (fifo->count >= FIFO_SIZE) {
        fprintf(stderr, "CDROM Warning: FIFO push overflow (Count=%d, Size=%d)!\n", fifo->count, FIFO_SIZE);
        return false; // FIFO full
    }
    // Calculate write pointer based on read pointer and count
    uint8_t write_ptr = (fifo->read_ptr + fifo->count) % FIFO_SIZE;
    fifo->data[write_ptr] = value;
    fifo->count++;
    return true;
}

/**
 * @brief Pops a byte from the FIFO. Returns 0 if empty.
 * @param fifo Pointer to the Fifo8 structure.
 * @return The popped byte, or 0 if the FIFO was empty.
 */
static uint8_t fifo_pop(Fifo8* fifo) {
    if (fifo->count == 0) {
        // fprintf(stderr, "CDROM Warning: FIFO pop underflow!\n");
        return 0; // FIFO empty
    }
    uint8_t value = fifo->data[fifo->read_ptr];
    fifo->read_ptr = (fifo->read_ptr + 1) % FIFO_SIZE; // Advance read pointer, wrap around
    fifo->count--;
    return value;
}

/**
 * @brief Clears all data from the FIFO.
 * @param fifo Pointer to the Fifo8 structure.
 */
static void fifo_clear(Fifo8* fifo) {
    fifo->count = 0;
    fifo->read_ptr = 0;
    // No need to clear the data buffer itself
}

/**
 * @brief Peeks at the next byte to be popped without removing it.
 * @param fifo Pointer to the Fifo8 structure.
 * @return The next byte, or 0 if the FIFO is empty.
 */
// static uint8_t fifo_peek(Fifo8* fifo) { // Currently unused
//     if (fifo->count == 0) {
//         return 0;
//     }
//     return fifo->data[fifo->read_ptr];
// }

/**
 * @brief Checks if the FIFO is empty.
 * @param fifo Pointer to the Fifo8 structure.
 * @return True if empty, false otherwise.
 */
static bool fifo_is_empty(Fifo8* fifo) {
    return fifo->count == 0;
}

/**
 * @brief Checks if the FIFO is full.
 * @param fifo Pointer to the Fifo8 structure.
 * @return True if full, false otherwise.
 */
static bool fifo_is_full(Fifo8* fifo) {
    return fifo->count >= FIFO_SIZE;
}


// --- BCD Conversion Helper ---
/** @brief Converts a Binary Coded Decimal (BCD) byte to integer. */
static uint8_t bcd_to_int(uint8_t bcd) {
    // Check for invalid BCD digits (faster than calculation?)
    if ((bcd & 0x0F) > 9 || (bcd >> 4) > 9) {
         fprintf(stderr, "CDROM Warning: Invalid BCD value 0x%02x\n", bcd);
         // Return a default or handle error appropriately
         return 0; // Or maybe return value as is? Let's return 0.
    }
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}
// static uint8_t int_to_bcd(uint8_t integer) { // Needed later for GetLocL response?
//     return (((integer / 10) % 10) << 4) | (integer % 10);
// }


// --- Internal Helper Functions ---

/** @brief Update dynamic status bits based on current state and FIFO status */
static void update_status_register(Cdrom* cdrom) {
    // Preserve Index bits (0-1) and Command Busy (bit 6)
    // Let's assume command handlers explicitly set/clear busy bit 6.
    uint8_t current_busy_bit = cdrom->status & (1 << 6);
    cdrom->status = (cdrom->index & 0x03) | current_busy_bit;

    // Bit 2: Parameter FIFO Empty (1 = Empty)
    if (fifo_is_empty(&cdrom->param_fifo)) cdrom->status |= (1 << 2);
    // Bit 3: Parameter FIFO Write Ready (1 = Not Full) - Note: Nocash says PRMWRDY (Parameter Write Ready) is Bit 3
    if (!fifo_is_full(&cdrom->param_fifo)) cdrom->status |= (1 << 3);
    // Bit 4: Response FIFO Ready (1 = Not Empty) - Note: Nocash says RSLRDY (Response Read Ready) is Bit 4
    if (!fifo_is_empty(&cdrom->response_fifo)) cdrom->status |= (1 << 4);
    // Bit 5: Data FIFO Ready (1 = Not Empty) - Note: Nocash says DTEN (Data Transfer Enable?) is Bit 5
    // TODO: Check actual data buffer state
    // if (!data_buffer_is_empty()) cdrom->status |= (1 << 5);
    // Bit 7: Data transfer busy (ADPCM related?) - Assume 0 for now
    // cdrom->status |= (0 << 7);
}

/**
 * @brief Triggers a CD-ROM interrupt (IRQ 2) if enabled.
 * @param cdrom Pointer to the Cdrom state.
 * @param int_code The interrupt code (1-5, sometimes 0 for test).
 */
static void trigger_interrupt(Cdrom* cdrom, uint8_t int_code) {
    // Interrupt codes map to flags/enable bits 0-4 (INT1-5)
    // INT1(DataReady)=bit0, INT2(Complete)=bit1, INT3(ResponseReady)=bit2, INT4(DataEnd?)=bit3, INT5(Error)=bit4
    if (int_code >= 1 && int_code <= 5) {
        uint8_t flag_bit = 1 << (int_code - 1);
        // Set the internal pending flag bit
        cdrom->interrupt_flags |= flag_bit;
        // printf("~CDROM Set IRQ Flag for INT%d (Flags=0x%02x)\n", int_code, cdrom->interrupt_flags);

        // Check if this specific interrupt is enabled via the enable mask (bits 0-4)
        if (cdrom->interrupt_enable & flag_bit) {
            // printf("~CDROM Requesting IRQ 2 (INT%d enabled)\n", int_code);
            interconnect_request_irq(cdrom->inter, IRQ_CDROM); // Request IRQ 2
        } else {
            // printf("~CDROM INT%d occurred but masked (Flags=0x%x, Enable=0x%x)\n", int_code, cdrom->interrupt_flags, cdrom->interrupt_enable);
        }
    } else if (int_code == 0) {
        // INT 0 is often used in TEST command responses but doesn't set flags? Check Nocash.
        printf("~CDROM Triggered INT0 (Test?)\n");
        interconnect_request_irq(cdrom->inter, IRQ_CDROM);
    }
    else {
        fprintf(stderr, "CDROM Error: Tried to trigger invalid INT code %d\n", int_code);
    }
}


// --- Command Handlers ---

/** @brief Command 0x01: GetStat. Returns drive status. */
static void cmd_get_stat(Cdrom* cdrom) {
    // printf("~ CDROM CMD: GetStat (0x01)\n");
    cdrom->current_state = CD_STATE_CMD_EXEC;
    update_status_register(cdrom); // Update status bits before reporting
    fifo_clear(&cdrom->response_fifo);
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 3); // INT3: Response Ready
    cdrom->current_state = CD_STATE_IDLE; // GetStat is quick
}

/** @brief Command 0x0A: Init. Resets controller. */
static void cmd_init(Cdrom* cdrom) {
    printf("~ CDROM CMD: Init (0x0A)\n");
    cdrom->current_state = CD_STATE_CMD_EXEC; // Mark as busy during Init

    // Reset controller state
    // cdrom->index = 0; // Index is NOT reset by Init
    cdrom->interrupt_enable = 0;
    cdrom->interrupt_flags = 0; // Clear any pending flags
    fifo_clear(&cdrom->param_fifo);
    fifo_clear(&cdrom->response_fifo);
    cdrom->target_lba = 0;
    // Reset status register (keep index)
    cdrom->status = (cdrom->index & 0x03);
    cdrom->status |= (1 << 2); // Param FIFO Empty
    cdrom->status |= (1 << 5); // Data FIFO Empty? Needs check
    // TODO: Update based on actual disc presence after internal checks?
    cdrom->status |= (1 << 4); // Set Response FIFO not empty (will push shortly)

    // Push first response (status after reset)
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 3); // INT3 for first response

    // TODO: Schedule INT2 (command complete) after ~1ms delay, pushing status again.
    // For now, immediately push second response and trigger INT2. THIS IS WRONG TIMING!
    // printf("  CDROM Init: First response sent. Pushing second immediately (WRONG TIMING).\n");
    update_status_register(cdrom); // Update status again (mainly FIFO flags)
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 2); // INT2: Command Complete
    cdrom->current_state = CD_STATE_IDLE; // Back to idle
}

/** @brief Command 0x1A: GetID. Returns disc status/license info. */
static void cmd_get_id(Cdrom* cdrom) {
    printf("~ CDROM CMD: GetID (0x1A)\n");
    cdrom->current_state = CD_STATE_CMD_EXEC;
    fifo_clear(&cdrom->response_fifo);
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->status); // 1st response: Status
    trigger_interrupt(cdrom, 3); // INT3: Response ready

    // TODO: Schedule second response (INT2) after delay. Send immediately for now.
    uint8_t resp[8];
    if (!cdrom->disc_present) {
        printf("  CDROM GetID: Responding No Disc (Error INT5)\n");
        // Nocash: INT5(error), response[0]=STAT_ERR, response[1]=0x80 (cannot read)
        cdrom->status = (cdrom->status & 0x03) | 0x11; // Set error bit, keep index
        fifo_push(&cdrom->response_fifo, cdrom->status); // Send error status?
        fifo_push(&cdrom->response_fifo, 0x80); // Error Code: Cannot read sector/No disc
        trigger_interrupt(cdrom, 5); // Trigger INT5 (Error)
        cdrom->current_state = CD_STATE_ERROR;
    } else {
        printf("  CDROM GetID: Responding Licensed Disc (SCEA)\n");
        // Response for Licensed Game Disc
        update_status_register(cdrom);
        resp[0] = cdrom->status; // Current status
        // TODO: Set MotorOn bit in status if simulating that
        resp[1] = 0x02; // Status: Licensed Game Disc (0x00=Audio, 0x10=Data, etc.)
        resp[2] = 0x00; // Disc Type: 0x00=CD-ROM (Mode1/2)
        resp[3] = 0x00; // System Area: Always 0x00 for PSX discs?
        // Licensed string section depends on region (SCEA, SCEE, SCEI)
        resp[4] = 'S'; resp[5] = 'C'; resp[6] = 'E'; resp[7] = 'A'; // NA/Asia
        // resp[7] = 'E'; // Europe
        // resp[7] = 'I'; // Japan

        for(int i=0; i<8; ++i) fifo_push(&cdrom->response_fifo, resp[i]);
        trigger_interrupt(cdrom, 2); // Trigger INT2 (Command Complete)
        cdrom->current_state = CD_STATE_IDLE;
    }
    // printf("  CDROM GetID: Second response requires timing.\n");
}

/** @brief Command 0x02: SetLoc. Sets target LBA for reading. */
static void cmd_set_loc(Cdrom* cdrom) {
    // printf("~ CDROM CMD: SetLoc (0x02)\n");
     cdrom->current_state = CD_STATE_CMD_EXEC;
     if (cdrom->param_fifo.count < 3) {
         fprintf(stderr, "CDROM Error: SetLoc requires 3 parameters, got %d\n", cdrom->param_fifo.count);
         fifo_clear(&cdrom->response_fifo);
         update_status_register(cdrom); fifo_push(&cdrom->response_fifo, cdrom->status | 0x11); // Set error bit?
         fifo_push(&cdrom->response_fifo, 0x40); // Wrong number of parameters error code
         trigger_interrupt(cdrom, 5);
         cdrom->current_state = CD_STATE_ERROR;
         return;
     }
     // Read M:S:F parameters in BCD from parameter FIFO
     uint8_t m_bcd = fifo_pop(&cdrom->param_fifo);
     uint8_t s_bcd = fifo_pop(&cdrom->param_fifo);
     uint8_t f_bcd = fifo_pop(&cdrom->param_fifo);
     uint8_t m = bcd_to_int(m_bcd);
     uint8_t s = bcd_to_int(s_bcd);
     uint8_t f = bcd_to_int(f_bcd);

     // Convert M:S:F to LBA (Logical Block Address)
     // LBA = (Minute * 60 + Second) * 75 + Frame - 150 (offset for lead-in)
     cdrom->target_lba = (m * 60 + s) * 75 + f - 150;
     // printf("  CDROM SetLoc: M=%u S=%u F=%u -> Target LBA=%u (0x%x)\n", m, s, f, cdrom->target_lba, cdrom->target_lba);

    // Push status response and trigger INT3
    update_status_register(cdrom);
    fifo_clear(&cdrom->response_fifo);
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 3); // INT3: Response Ready

    // TODO: Set drive state to Seeking? Simulate seek time?
    // TODO: Trigger INT2 (Command Complete) after seek delay.
    // For now, assume seek is instantaneous and complete
    // printf("  CDROM SetLoc: Command complete requires timing.\n");
    trigger_interrupt(cdrom, 2); // Placeholder: Send Complete interrupt immediately
    cdrom->current_state = CD_STATE_IDLE;
}

/** @brief Command 0x06: ReadN. Reads normal sectors starting at target LBA. */
static void cmd_read_n(Cdrom* cdrom) {
    printf("~ CDROM CMD: ReadN (0x06)\n");
    cdrom->current_state = CD_STATE_READING; // Set Reading state
    update_status_register(cdrom);
    cdrom->status |= (1 << 6); // Set Command Busy bit

    // Push first response (status) and trigger INT3
    fifo_clear(&cdrom->response_fifo);
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 3); // INT3: Response Ready

    // --- Placeholder / Immediate Completion (WRONG TIMING / NO DATA) ---
    // TODO: Implement actual asynchronous sector reading from cdrom->disc_file
    //       at cdrom->target_lba into an internal sector buffer.
    // TODO: Increment target_lba after read.
    // TODO: Transfer data via Data FIFO / DMA Channel 3. Handle Mode1/Mode2 sectors.
    // TODO: Handle errors (e.g., end of disc, file read error).
    if (!cdrom->disc_present) {
        printf("  CDROM ReadN: No Disc error response.\n");
        fifo_push(&cdrom->response_fifo, cdrom->status | 0x11); // Error status code?
        fifo_push(&cdrom->response_fifo, 0x80); // Error: Cannot read sector? (Check error codes)
        trigger_interrupt(cdrom, 5); // INT5: Error
        cdrom->current_state = CD_STATE_ERROR;
    } else {
        // Simulate instant read success for now
        printf("  CDROM ReadN: Simulating instant success from LBA %u (NO DATA READ).\n", cdrom->target_lba);
        update_status_register(cdrom);
        fifo_push(&cdrom->response_fifo, cdrom->status); // Push completion status
        // Need INT1(DataReady) *during* transfer, then INT2(Complete) at end.
        trigger_interrupt(cdrom, 1); // INT1: Data Ready (prematurely)
        trigger_interrupt(cdrom, 2); // INT2: Command Complete (prematurely)
        cdrom->current_state = CD_STATE_IDLE; // Go back to idle (wrong state handling)
    }
     cdrom->status &= ~(1 << 6); // Clear busy bit (prematurely)
    // --- End Placeholder ---
}

/** @brief Main command dispatcher */
static void cdrom_handle_command(Cdrom* cdrom, uint8_t command) {
    // TODO: Check if already busy? Queue command? PSX seems to overwrite/ignore.
    cdrom->pending_command = command;
    cdrom->current_state = CD_STATE_CMD_EXEC; // Mark as busy

    switch (command) {
        case CDC_GETSTAT: cmd_get_stat(cdrom); break;
        case CDC_INIT:    cmd_init(cdrom); break;
        case CDC_GETID:   cmd_get_id(cdrom); break;
        case CDC_SETLOC:  cmd_set_loc(cdrom); break;
        case CDC_READN:   cmd_read_n(cdrom); break;
        // TODO: Add cases for other commands (Pause, SeekL, Test, etc.)

        default:
            fprintf(stderr, "CDROM Error: Unhandled command 0x%02x\n", command);
            // Push standard error response (INT5)
            fifo_clear(&cdrom->response_fifo);
            update_status_register(cdrom); fifo_push(&cdrom->response_fifo, cdrom->status | (1 << 6) | 0x11); // Set busy & error status code
            fifo_push(&cdrom->response_fifo, 0x80); // Invalid command error code
            trigger_interrupt(cdrom, 5); // INT5: Error interrupt
            cdrom->current_state = CD_STATE_ERROR;
            break;
    }
    // Clear parameters AFTER handler potentially uses them
    fifo_clear(&cdrom->param_fifo);
    // Note: Busy flag should be cleared by command handlers when they truly complete
}


// --- Core Public Functions ---

/** @brief Initializes the CD-ROM drive state. */
void cdrom_init(Cdrom* cdrom, struct Interconnect* inter) {
    printf("Initializing CD-ROM...\n");
    memset(cdrom, 0, sizeof(Cdrom));
    cdrom->inter = inter;
    // Initial status: Param FIFO Empty, Response FIFO Empty, Data FIFO Empty, Idle, No Disc?
    cdrom->status = (1 << 2) | (1 << 4) | (1 << 5); // PRMEMPT | RSLRDY=0 | DTEN=0
    cdrom->disc_present = false;
    cdrom->current_state = CD_STATE_IDLE;
    fifo_init(&cdrom->param_fifo);
    fifo_init(&cdrom->response_fifo);
    printf("  CDROM Initial Status: 0x%02x\n", cdrom->status);
}

/** @brief Attempts to load a disc image file. */
bool cdrom_load_disc(Cdrom* cdrom, const char* bin_filename) {
    if (cdrom->disc_file) { fclose(cdrom->disc_file); cdrom->disc_file = NULL; }
    cdrom->disc_present = false;
    cdrom->is_cd_da = false;

    printf("CDROM: Attempting to load disc image '%s'\n", bin_filename);
    cdrom->disc_file = fopen(bin_filename, "rb");
    if (!cdrom->disc_file) {
        perror("CDROM Error: Failed to open disc image");
        cdrom->status = (1 << 2) | (1 << 4) | (1 << 5); // Ensure No Disc status bits?
        cdrom->current_state = CD_STATE_IDLE; // Remain idle
        return false;
    }

    printf("CDROM: Disc image loaded successfully.\n");
    cdrom->disc_present = true;
    // TODO: Parse associated CUE file if available to get track layout/types
    cdrom->current_state = CD_STATE_IDLE;
    // TODO: Update status to reflect disc presence (MotorOn? Clear NoDisc bit?)
    return true;
}


/** @brief Reads an 8-bit value from a CD-ROM controller register. */
uint8_t cdrom_read_register(Cdrom* cdrom, uint32_t addr) {
    uint8_t offset = addr & 0x3;
    uint8_t result = 0;
    uint8_t reg_index = cdrom->index; // Use the last written index

    // Reading Status register (offset 0) is independent of the index register value
    if (offset == CDREG_INDEX) {
        update_status_register(cdrom); // Update dynamic bits
        result = cdrom->status;
        // printf("~CDROM Read Status (1800h) = 0x%02x\n", result);
        return result;
    }

    // Reads for other offsets depend on the selected index
    switch (offset) {
        case CDREG_RESPONSE: // Response FIFO (1801h) - Requires Index 1
            if (reg_index == 1) {
                result = fifo_pop(&cdrom->response_fifo);
                // Reading last byte from response FIFO clears INT3 flag
                if (fifo_is_empty(&cdrom->response_fifo)) {
                    cdrom->interrupt_flags &= ~(1 << 2); // Clear INT3 flag
                }
            } else {
                 fprintf(stderr, "CDROM Read Error: Read from 1801h with Index %d != 1\n", reg_index);
                 result = 0xFF; // Return junk? Or last value?
            }
            // printf("~CDROM Read Response FIFO (1801h.%d) -> 0x%02x\n", reg_index, result);
            break;
        case CDREG_DATA: // Data FIFO (1802h) - Requires Index 2
             if (reg_index == 2) {
                 // TODO: Read from actual sector data buffer
                 // printf("~CDROM Read Data FIFO (1802h.2): UNIMPLEMENTED\n");
                 result = 0; // Return dummy data
             } else {
                 fprintf(stderr, "CDROM Read Error: Read from 1802h with Index %d != 2\n", reg_index);
                 result = 0xFF;
             }
            break;
        case CDREG_IRQ_EN_FLAG: // Interrupt Enable/Flags (1803h) - Requires Index 1
            if (reg_index == 1) {
                // Read Interrupt Enable & Flags (1803h.1)
                // Lower 5 bits = Enable Mask (RW), Upper 3 bits = Flags (R/Ack)
                // Flags assumed to be bits 7:5 = INT5, INT4, INT3 ? Check docs.
                result = (cdrom->interrupt_enable & 0x1F) | ((cdrom->interrupt_flags << 5) & 0xE0); // Map flags 0-2 to bits 5-7? Needs check.
                // printf("~CDROM Read IntEn/Flags (1803h.1) = 0x%02x (En=0x%x, Fl=0x%x)\n",
                //        result, cdrom->interrupt_enable, cdrom->interrupt_flags);
            } else {
                 fprintf(stderr, "CDROM Read Error: Read from 1803h with index %d != 1\n", reg_index);
                 result = 0xFF;
             }
            break;
    }
    return result;
}

/** @brief Writes an 8-bit value to a CD-ROM controller register. */
void cdrom_write_register(Cdrom* cdrom, uint32_t addr, uint8_t value) {
    uint8_t offset = addr & 0x3;
    uint8_t reg_index = cdrom->index; // Use currently selected index BEFORE potential write to index reg

    // Allow writing Index register anytime
    if (offset == CDREG_INDEX) {
        cdrom->index = value & 0x3; // Only lower 2 bits used
        // printf("~CDROM Write Index Select (1800h.0) = %u\n", cdrom->index);
        return;
    }

    // Writes to other registers depend on the selected index
    switch (offset) {
        case CDREG_COMMAND: // Command Register (1801h) - Requires Index 0
            if (reg_index == 0) {
                // TODO: Check if Command Busy bit (STAT[6]) is set? Ignore write if busy?
                // printf("~CDROM Write Command (1801h.0) = 0x%02x\n", value);
                cdrom_handle_command(cdrom, value);
            } else {
                 fprintf(stderr, "CDROM Write Error: Write to Command Reg (1801h) with Index %d != 0\n", reg_index);
            }
            break;

        case CDREG_PARAMETER: // Parameter FIFO (1802h) - Requires Index 0
            if (reg_index == 0) {
                 if (!fifo_push(&cdrom->param_fifo, value)) {
                     // Parameter FIFO is full - should this set an error flag?
                     fprintf(stderr, "CDROM Warning: Parameter FIFO overflow on write 0x%02x!\n", value);
                 }
                 // printf("~CDROM Write Parameter FIFO (1802h.0) = 0x%02x (Count=%d)\n", value, cdrom->param_fifo.count);
            } else {
                 fprintf(stderr, "CDROM Write Error: Write to Param Reg (1802h) with Index %d != 0\n", reg_index);
            }
            break;

        case CDREG_REQUEST: // Request (1803h.0) OR Interrupt Enable/Ack (1803h.1)
             if (reg_index == 0) { // Request Register Write (1803h.0)
                // Bit 7: Reset Parameter FIFO
                if (value & 0x80) {
                    fifo_clear(&cdrom->param_fifo);
                    // printf("~CDROM Parameter FIFO Reset via Reg 1803h.0\n");
                }
                // TODO: Handle bit 5? (Request Data?)
                // printf("~CDROM Write Request Reg (1803h.0) = 0x%02x\n", value);
             } else if (reg_index == 1) { // Interrupt Enable / Ack Write (1803h.1)
                 // Lower 5 bits set the enable mask
                 cdrom->interrupt_enable = value & 0x1F;
                 // Writing 1 to flag bits (upper 3?) ACKs/clears them
                 // Nocash says writing 1 to bits 0-4 ACKs INT1-5 flags. Let's use that.
                 uint8_t ack_flags = value & 0x1F;
                 cdrom->interrupt_flags &= ~ack_flags;
                 // Nocash also mentions writing 0x40 to this address clears all flags AND enable? Check needed.
                 if (value == 0x40) {
                     cdrom->interrupt_flags = 0;
                     // cdrom->interrupt_enable = 0; // Does it clear enable too? Let's assume no for now.
                 }

                 // printf("~CDROM Write IntEn=0x%02x, Ack=0x%02x -> Flags=0x%02x\n",
                 //        cdrom->interrupt_enable, ack_flags, cdrom->interrupt_flags);
             } else {
                  fprintf(stderr, "CDROM Write Error: Write to 1803h with Index %d != 0 or 1\n", reg_index);
             }
            break;
    }
}