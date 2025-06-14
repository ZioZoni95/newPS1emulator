// --- cdrom.c ---
#include "cdrom.h"
#include "interconnect.h" // For interrupt definitions/requests IRQ_CDROM
#include <stdio.h>
#include <string.h> // For memset
#include <stdlib.h> // For exit if needed

// --- Sector Structure Constants ---
#define CD_RAW_SECTOR_SIZE 2352 // Common size for Mode 2 sectors in .bin files
#define CD_USER_DATA_SIZE 2048  // Standard data payload size (Mode 1 or Mode 2 Form 1)
#define CD_MODE2_FORM1_HEADER_SIZE 24 // Offset to user data (Sync+Header+SubHeader+EDC/ECC)
// Using 2340 as the size when raw-ish is requested (Mode 2 Form 2 + Subheader?)
// Offset 12 skips Sync. This part needs verification against hardware/game needs.
#define CD_MODE_RAWISH_SIZE 2340
#define CD_MODE_RAWISH_OFFSET 12
#define CDROM_READ_DELAY_CYCLES 50000
#define CDROM_GETID_DELAY_CYCLES 10000


// --- CDROM Commands (Partial List) ---
#define CDC_GETSTAT     0x01
#define CDC_SETLOC      0x02
#define CDC_READN       0x06
#define CDC_STOP        0x08 // <<< Added
#define CDC_PAUSE       0x09
#define CDC_INIT        0x0A
#define CDC_SETMODE     0x0E
#define CDC_SEEKL       0x15
#define CDC_TEST        0x19
#define CDC_GETID       0x1A

// --- Forward Declarations for Command Handlers ---
static void cdrom_handle_command(Cdrom* cdrom, uint8_t command);
static void cmd_get_stat(Cdrom* cdrom);
static void cmd_init(Cdrom* cdrom);
static void cmd_get_id(Cdrom* cdrom);
static void cmd_set_loc(Cdrom* cdrom);
static void cmd_read_n(Cdrom* cdrom);
static void cmd_pause(Cdrom* cdrom);
static void cmd_seek_l(Cdrom* cdrom);
static void cmd_test(Cdrom* cdrom);
static void cmd_set_mode(Cdrom* cdrom);
static void cmd_stop(Cdrom* cdrom); // <<< Added

// --- Internal Helper Function Declarations ---
static void fifo_init(Fifo8* fifo);
static bool fifo_push(Fifo8* fifo, uint8_t value);
static uint8_t fifo_pop(Fifo8* fifo);
static void fifo_clear(Fifo8* fifo);
static bool fifo_is_empty(Fifo8* fifo);
static bool fifo_is_full(Fifo8* fifo);
static uint8_t bcd_to_int(uint8_t bcd);
static void update_status_register(Cdrom* cdrom);
static void trigger_interrupt(Cdrom* cdrom, uint8_t int_code);


// --- FIFO Helpers ---
// ... (fifo_init, fifo_push, fifo_pop, fifo_clear, fifo_is_empty, fifo_is_full - unchanged) ...
static void fifo_init(Fifo8* fifo) {
    fifo->count = 0;
    fifo->read_ptr = 0;
    memset(fifo->data, 0, FIFO_SIZE);
}
static bool fifo_push(Fifo8* fifo, uint8_t value) {
    if (fifo->count >= FIFO_SIZE) {
        fprintf(stderr, "CDROM Warning: FIFO push overflow (Count=%d, Size=%d)!\n", fifo->count, FIFO_SIZE);
        return false;
    }
    uint8_t write_ptr = (fifo->read_ptr + fifo->count) % FIFO_SIZE;
    fifo->data[write_ptr] = value;
    fifo->count++;
    return true;
}
static uint8_t fifo_pop(Fifo8* fifo) {
    if (fifo->count == 0) {
        return 0;
    }
    uint8_t value = fifo->data[fifo->read_ptr];
    fifo->read_ptr = (fifo->read_ptr + 1) % FIFO_SIZE;
    fifo->count--;
    return value;
}
static void fifo_clear(Fifo8* fifo) {
    fifo->count = 0;
    fifo->read_ptr = 0;
}
static bool fifo_is_empty(Fifo8* fifo) {
    return fifo->count == 0;
}
static bool fifo_is_full(Fifo8* fifo) {
    return fifo->count >= FIFO_SIZE;
}


// --- BCD Conversion Helper ---
static uint8_t bcd_to_int(uint8_t bcd) {
    if (((bcd & 0x0F) > 9) || ((bcd >> 4) > 9)) {
         fprintf(stderr, "CDROM Warning: Invalid BCD value 0x%02x encountered in conversion.\n", bcd);
         return 0;
    }
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

// --- Internal Helper Functions ---

// Status Register Bits
#define STAT_INDEX0     (1 << 0)
#define STAT_INDEX1     (1 << 1)
#define STAT_MOTORON    (1 << 1) // Placeholder
#define STAT_PRMEMPT    (1 << 2)
#define STAT_PRMWRDY    (1 << 3)
#define STAT_RSLRDY     (1 << 4) // Response FIFO Not Empty
#define STAT_ERROR      (1 << 4) // Error flag overlaps RSLRDY? Check Nocash carefully. Let's use bit 4 for RSLRDY and rely on INT5 + error codes.
#define STAT_DTEN       (1 << 5) // Data FIFO Not Empty
#define STAT_BUSY       (1 << 6)
#define STAT_PLAYING    (1 << 7) // Placeholder

static void update_status_register(Cdrom* cdrom) {
    // Preserve bits that aren't dynamically determined here
    uint8_t preserved_bits = cdrom->status & (STAT_BUSY | STAT_PLAYING | STAT_MOTORON); // Keep Busy, Playing, MotorOn
    // Nocash: Error state (0x11/0x19...) might be separate/sticky until cleared? Let's assume error clears on new cmd for now.

    cdrom->status = (cdrom->index & 0x03) | preserved_bits;

    if (fifo_is_empty(&cdrom->param_fifo)) cdrom->status |= STAT_PRMEMPT;
    if (!fifo_is_full(&cdrom->param_fifo)) cdrom->status |= STAT_PRMWRDY;
    if (!fifo_is_empty(&cdrom->response_fifo)) cdrom->status |= STAT_RSLRDY; // Response available
    if (cdrom->data_buffer_count > cdrom->data_buffer_read_ptr) cdrom->status |= STAT_DTEN; // Data available
}

static void trigger_interrupt(Cdrom* cdrom, uint8_t int_code) {
    if (int_code >= 1 && int_code <= 5) {
        uint8_t flag_bit = 1 << (int_code - 1);
        cdrom->interrupt_flags |= flag_bit;
        if (cdrom->interrupt_enable & flag_bit) {
            interconnect_request_irq(cdrom->inter, IRQ_CDROM);
        }
    } else if (int_code == 0) { // Test commands sometimes use INT0
        interconnect_request_irq(cdrom->inter, IRQ_CDROM);
    }
    // Note: Error status bit setting should happen in the command handler based on the error code.
}

// --- Command Handlers ---

static void cmd_get_stat(Cdrom* cdrom) {
    cdrom->current_state = CD_STATE_IDLE;
    update_status_register(cdrom);
    fifo_clear(&cdrom->response_fifo);
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 3); // INT3: Response Ready
}

static void cmd_init(Cdrom* cdrom) {
    printf("~ CDROM CMD: Init (0x0A)\n");
    cdrom->current_state = CD_STATE_CMD_EXEC;
    cdrom->status |= STAT_BUSY;

    cdrom->interrupt_enable = 0;
    cdrom->interrupt_flags = 0;
    fifo_clear(&cdrom->param_fifo);
    fifo_clear(&cdrom->response_fifo);
    cdrom->target_lba = 0;
    cdrom->double_speed = false;
    cdrom->sector_size_is_2340 = false;
    cdrom->data_buffer_count = 0;
    cdrom->data_buffer_read_ptr = 0;
    cdrom->status = (cdrom->index & 0x03) | STAT_PRMEMPT | STAT_PRMWRDY | STAT_BUSY;

    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 3);

    // TODO: Add realistic Init delay
    update_status_register(cdrom);
    cdrom->status &= ~STAT_BUSY;
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 2);
    cdrom->current_state = CD_STATE_IDLE;
}

static void cmd_get_id(Cdrom* cdrom) {
    printf("~ CDROM CMD: GetID (0x1A)\n");
    cdrom->status |= STAT_BUSY;


    fifo_clear(&cdrom->response_fifo);
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 3);

    cdrom->current_state = CD_STATE_GETID_PENDING;
    cdrom->read_delay_timer = CDROM_GETID_DELAY_CYCLES;
}

static void cmd_set_loc(Cdrom* cdrom) {
    cdrom->current_state = CD_STATE_CMD_EXEC;
    cdrom->status |= STAT_BUSY;

    if (cdrom->param_fifo.count < 3) {
        fprintf(stderr, "CDROM Error: SetLoc requires 3 parameters, got %d\n", cdrom->param_fifo.count);
        fifo_clear(&cdrom->response_fifo);
        update_status_register(cdrom);
        uint8_t error_status = (cdrom->status & 0x03) | 0x14; // Error status (STAT_ERROR maybe?) + Busy
        fifo_push(&cdrom->response_fifo, error_status);
        fifo_push(&cdrom->response_fifo, 0x40); // Error Code: Wrong number of parameters
        trigger_interrupt(cdrom, 5);
        cdrom->current_state = CD_STATE_ERROR;
        cdrom->status = error_status & ~STAT_BUSY;
        return;
    }
    uint8_t m_bcd = fifo_pop(&cdrom->param_fifo);
    uint8_t s_bcd = fifo_pop(&cdrom->param_fifo);
    uint8_t f_bcd = fifo_pop(&cdrom->param_fifo);
    uint8_t m = bcd_to_int(m_bcd);
    uint8_t s = bcd_to_int(s_bcd);
    uint8_t f = bcd_to_int(f_bcd);

    cdrom->target_lba = ((uint32_t)m * 60 + (uint32_t)s) * 75 + (uint32_t)f;
    if (cdrom->target_lba >= 150) cdrom->target_lba -= 150; else cdrom->target_lba = 0;

    // First Response (INT3)
    update_status_register(cdrom);
    fifo_clear(&cdrom->response_fifo);
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 3);

    // Second Response (INT2) - after delay
    // TODO: Add realistic seek delay
    update_status_register(cdrom);
    cdrom->status &= ~STAT_BUSY;
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 2);
    cdrom->current_state = CD_STATE_IDLE;
}

// <<< UPDATED FUNCTION >>>
/** @brief Command 0x06: ReadN. Reads normal sectors starting at target LBA. */
static void cmd_read_n(Cdrom* cdrom) {
     printf("~ CDROM CMD: ReadN (0x06) from LBA %u\n", cdrom->target_lba);

    // 1. Check Disc Presence
    if (!cdrom->disc_present || !cdrom->disc_file) {
         fprintf(stderr, "CDROM ReadN Error: No disc loaded.\n");
         uint8_t error_status = (cdrom->index & 0x03) | 0x10; // Status for No Disc Error
         fifo_clear(&cdrom->response_fifo);
         fifo_push(&cdrom->response_fifo, error_status);
         fifo_push(&cdrom->response_fifo, 0x80); // Error Code: No Disc
         trigger_interrupt(cdrom, 5);
         cdrom->current_state = CD_STATE_ERROR;
         cdrom->status = error_status;
        return;
    }

    // 2. Acknowledge Command & Set State
    cdrom->current_state = CD_STATE_READING;
    cdrom->status |= STAT_BUSY;
    cdrom->read_delay_timer = CDROM_READ_DELAY_CYCLES;

    fifo_clear(&cdrom->response_fifo);
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->status); // Push initial status (Busy)
    trigger_interrupt(cdrom, 3); // INT3: Response Ready

    // --- Read Raw Sector ---
    // TODO: Add timing delays for seek
    long long offset = (long long)cdrom->target_lba * CD_RAW_SECTOR_SIZE;
    uint8_t raw_sector_buffer[CD_RAW_SECTOR_SIZE]; // Local buffer for raw read

    if (fseek(cdrom->disc_file, (long)offset, SEEK_SET) != 0) {
        perror("CDROM ReadN Error: fseek failed");
        fifo_clear(&cdrom->response_fifo);
        uint8_t error_status = (cdrom->index & 0x03) | 0x14 | STAT_BUSY; // Seek error status?
        fifo_push(&cdrom->response_fifo, error_status);
        fifo_push(&cdrom->response_fifo, 0x40); // Error Code: Seek Error?
        trigger_interrupt(cdrom, 5);
        cdrom->current_state = CD_STATE_ERROR;
        cdrom->status = error_status & ~STAT_BUSY;
        return;
    }

    // TODO: Add timing delays for read
    size_t bytes_read = fread(raw_sector_buffer, 1, CD_RAW_SECTOR_SIZE, cdrom->disc_file);

    if (bytes_read != CD_RAW_SECTOR_SIZE) {
        fprintf(stderr, "CDROM ReadN Error: fread failed or read incomplete (%zu bytes).\n", bytes_read);
        fifo_clear(&cdrom->response_fifo);
        uint8_t error_status = (cdrom->index & 0x03) | 0x11 | STAT_BUSY; // Read error status?
        fifo_push(&cdrom->response_fifo, error_status);
        fifo_push(&cdrom->response_fifo, feof(cdrom->disc_file) ? 0x80 : 0x20); // Error: End of Disc or Drive Error?
        trigger_interrupt(cdrom, 5);
        cdrom->current_state = CD_STATE_ERROR;
        cdrom->status = error_status & ~STAT_BUSY;
        return;
    }

    // --- Process Sector Based on Mode ---
    uint32_t bytes_to_copy = 0;
    uint32_t copy_offset_in_raw = 0;

    if (cdrom->sector_size_is_2340) {
        // Mode requests "raw-ish" sector data (SetMode bit 5 = 1)
        bytes_to_copy = CD_MODE_RAWISH_SIZE;        // 2340 bytes
        copy_offset_in_raw = CD_MODE_RAWISH_OFFSET; // Offset 12?
        // printf("  CDROM ReadN: Mode requests ~raw data (%u bytes from offset %u)\n", bytes_to_copy, copy_offset_in_raw);
    } else {
        // Mode requests standard 2048 byte user data (SetMode bit 5 = 0)
        bytes_to_copy = CD_USER_DATA_SIZE;
        copy_offset_in_raw = CD_MODE2_FORM1_HEADER_SIZE; // Offset 24
        // printf("  CDROM ReadN: Mode requests user data (%u bytes from offset %u)\n", bytes_to_copy, copy_offset_in_raw);
    }

    // 6. Data Transfer (Polled Setup)
    cdrom->data_buffer_count = 0;
    cdrom->data_buffer_read_ptr = 0;
    if (copy_offset_in_raw + bytes_to_copy <= CD_RAW_SECTOR_SIZE && bytes_to_copy <= sizeof(cdrom->data_buffer)) {
        memcpy(cdrom->data_buffer, raw_sector_buffer + copy_offset_in_raw, bytes_to_copy);
        cdrom->data_buffer_count = bytes_to_copy;
    } else {
         fprintf(stderr, "CDROM ReadN Error: Calculated copy [%u + %u] exceeds buffers.\n", copy_offset_in_raw, bytes_to_copy);
         // Handle internal error
         fifo_clear(&cdrom->response_fifo);
         uint8_t error_status = (cdrom->index & 0x03) | 0x11 | STAT_BUSY; // Generic error status
         fifo_push(&cdrom->response_fifo, error_status);
         fifo_push(&cdrom->response_fifo, 0x20); // Generic Drive Error?
         trigger_interrupt(cdrom, 5);
         cdrom->current_state = CD_STATE_ERROR;
         cdrom->status = error_status & ~STAT_BUSY;
         return;
    }
    // --- END MODIFIED SECTION ---

    // 7. Signal Data Ready
    trigger_interrupt(cdrom, 1); // INT1: Data Ready

    // --- IMMEDIATE COMPLETION (TIMING INACCURATE) ---
    // 8. Complete Command (Prematurely)
    update_status_register(cdrom);
    cdrom->status &= ~STAT_BUSY;
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 2); // INT2: Command Complete

    // 9. Update State (Prematurely)
    cdrom->target_lba++;
    cdrom->current_state = CD_STATE_IDLE;
    // --- End Immediate Completion ---
}
// <<< END UPDATED FUNCTION >>>

static void cmd_pause(Cdrom* cdrom) {
    printf("~ CDROM CMD: Pause (0x09)\n");
    cdrom->current_state = CD_STATE_CMD_EXEC;
    cdrom->status |= STAT_BUSY;

    // First Response (INT3)
    update_status_register(cdrom);
    fifo_clear(&cdrom->response_fifo);
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 3);

    // Internal Pause Action
    cdrom->status &= ~STAT_PLAYING;
    cdrom->data_buffer_count = 0; // Pause likely invalidates data buffer?
    cdrom->data_buffer_read_ptr = 0;

    // Second Response (INT2)
    // TODO: Add delay?
    update_status_register(cdrom);
    cdrom->status &= ~STAT_BUSY;
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 2);
    cdrom->current_state = CD_STATE_IDLE;
}

static void cmd_seek_l(Cdrom* cdrom) {
    printf("~ CDROM CMD: SeekL (0x15) to LBA %u\n", cdrom->target_lba);
    cdrom->current_state = CD_STATE_CMD_EXEC;
    cdrom->status |= STAT_BUSY;

    // First Response (INT3)
    update_status_register(cdrom);
    fifo_clear(&cdrom->response_fifo);
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 3);

    // Second Response (INT2) - after delay
    // TODO: Add realistic seek delay
    update_status_register(cdrom);
    cdrom->status &= ~STAT_BUSY;
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 2);
    cdrom->current_state = CD_STATE_IDLE;
}

static void cmd_test(Cdrom* cdrom) {
    cdrom->current_state = CD_STATE_CMD_EXEC;
    cdrom->status |= STAT_BUSY;

    if (fifo_is_empty(&cdrom->param_fifo)) {
        fprintf(stderr, "CDROM Error: Test (0x19) requires a parameter.\n");
        fifo_clear(&cdrom->response_fifo);
        update_status_register(cdrom);
        uint8_t error_status = (cdrom->index & 0x03) | 0x14 | STAT_BUSY; // Error status
        fifo_push(&cdrom->response_fifo, error_status);
        fifo_push(&cdrom->response_fifo, 0x40); // Error Code: Wrong number of parameters
        trigger_interrupt(cdrom, 5);
        cdrom->current_state = CD_STATE_ERROR;
        cdrom->status = error_status & ~STAT_BUSY;
        return;
    }
    uint8_t sub_command = fifo_pop(&cdrom->param_fifo);
    printf("~ CDROM CMD: Test (0x19), Subcommand: 0x%02x\n", sub_command);

    // First Response (INT3 - Status)
    update_status_register(cdrom);
    fifo_clear(&cdrom->response_fifo);
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 3);

    // Second Response (INT2/INT5 - Result/Completion)
    // TODO: Add realistic delay
//edited
    switch (sub_command) {
        case 0x20: { // Get BIOS Date / Version ID
            printf("  CDROM Test(0x20): Get BIOS Date/Version\n");
            uint8_t resp[4] = { 0x97, 0x01, 0x10, 0xC2 }; // Placeholder BCD YY, MM, DD, Version
            update_status_register(cdrom);
            cdrom->status &= ~STAT_BUSY;
            fifo_push(&cdrom->response_fifo, cdrom->status);
            for (int i = 0; i < 4; ++i) fifo_push(&cdrom->response_fifo, resp[i]);
            trigger_interrupt(cdrom, 2);
            cdrom->current_state = CD_STATE_IDLE;
            break;
        }
        default: {
            fprintf(stderr, "CDROM Test Warning: Unhandled subcommand 0x%02x\n", sub_command);
            update_status_register(cdrom);
            uint8_t error_status = (cdrom->index & 0x03) | 0x14; // Error status
            fifo_push(&cdrom->response_fifo, error_status);
            fifo_push(&cdrom->response_fifo, 0x20); // Error Code: Invalid Command
            trigger_interrupt(cdrom, 5);
            cdrom->current_state = CD_STATE_ERROR;
            cdrom->status = error_status & ~STAT_BUSY;
            break;
        }
    }
     if (cdrom->current_state != CD_STATE_ERROR) {
         cdrom->status &= ~STAT_BUSY;
     }
}

static void cmd_set_mode(Cdrom* cdrom) {
    cdrom->current_state = CD_STATE_CMD_EXEC;
    cdrom->status |= STAT_BUSY;

    if (fifo_is_empty(&cdrom->param_fifo)) {
        fprintf(stderr, "CDROM Error: SetMode (0x0E) requires a parameter.\n");
        fifo_clear(&cdrom->response_fifo);
        update_status_register(cdrom);
        uint8_t error_status = (cdrom->status & 0x03) | 0x14 | STAT_BUSY;
        fifo_push(&cdrom->response_fifo, error_status);
        fifo_push(&cdrom->response_fifo, 0x40);
        trigger_interrupt(cdrom, 5);
        cdrom->current_state = CD_STATE_ERROR;
        cdrom->status = error_status & ~STAT_BUSY;
        return;
    }

    uint8_t mode_byte = fifo_pop(&cdrom->param_fifo);
    printf("~ CDROM CMD: SetMode (0x0E) with ModeByte = 0x%02x\n", mode_byte);

    cdrom->double_speed        = (mode_byte & 0x80) != 0;
    cdrom->sector_size_is_2340 = (mode_byte & 0x20) != 0;
    // TODO: Store and use other mode bits

    printf("  CDROM SetMode: Speed=%s, SectorSize=%s\n",
        cdrom->double_speed ? "Double" : "Normal",
        cdrom->sector_size_is_2340 ? "2340/Raw" : "2048/Data");

    // First Response (INT3)
    update_status_register(cdrom);
    fifo_clear(&cdrom->response_fifo);
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 3);

    // Second Response (INT2)
    // TODO: Add delay?
    update_status_register(cdrom);
    cdrom->status &= ~STAT_BUSY;
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 2);
    cdrom->current_state = CD_STATE_IDLE;
}

// <<< NEW FUNCTION >>>
/** @brief Command 0x08: Stop. Stops CD-DA playback or reading. */
static void cmd_stop(Cdrom* cdrom) {
    printf("~ CDROM CMD: Stop (0x08)\n");
    cdrom->current_state = CD_STATE_CMD_EXEC;
    cdrom->status |= STAT_BUSY;

    // First Response (INT3)
    update_status_register(cdrom);
    fifo_clear(&cdrom->response_fifo);
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 3);

    // Internal Stop Action
    // TODO: Halt async ReadN/CDDA if simulating.
    cdrom->status &= ~STAT_PLAYING; // Ensure playing bit is clear
    cdrom->data_buffer_count = 0;   // Invalidate data buffer
    cdrom->data_buffer_read_ptr = 0;
    // TODO: Stop motor? Clear STAT_MOTORON?

    // Second Response (INT2)
    // TODO: Add small delay?
    update_status_register(cdrom);
    cdrom->status &= ~STAT_BUSY;
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 2);
    cdrom->current_state = CD_STATE_IDLE;
}
// <<< END NEW FUNCTION >>>

/** @brief Main command dispatcher */
static void cdrom_handle_command(Cdrom* cdrom, uint8_t command) {
    cdrom->pending_command = command;
    // Parameter FIFO should be cleared AFTER the handler uses it, if it needs to.
    // Let's clear it here for commands known not to use params, or let handlers clear it.
    bool uses_params = (command == CDC_SETLOC || command == CDC_SETMODE || command == CDC_TEST); // Add others as needed

    switch (command) {
        case CDC_GETSTAT: cmd_get_stat(cdrom); break;
        case CDC_SETLOC:  cmd_set_loc(cdrom); break; // Consumes params inside
        case CDC_READN:   cmd_read_n(cdrom); break;
        case CDC_PAUSE:   cmd_pause(cdrom); break;
        case CDC_INIT:    cmd_init(cdrom); break; // Clears params inside
        case CDC_SETMODE: cmd_set_mode(cdrom); break; // Consumes params inside
        case CDC_STOP:    cmd_stop(cdrom); break;
        case CDC_SEEKL:   cmd_seek_l(cdrom); break;
        case CDC_TEST:    cmd_test(cdrom); break; // Consumes params inside
        case CDC_GETID:   cmd_get_id(cdrom); break;
        // TODO: Add cases for other commands

        default:
            fprintf(stderr, "CDROM Error: Unhandled command 0x%02x\n", command);
            fifo_clear(&cdrom->response_fifo);
            update_status_register(cdrom);
            uint8_t error_status = (cdrom->status & 0x03) | 0x14 | STAT_BUSY; // Error status
            fifo_push(&cdrom->response_fifo, error_status);
            fifo_push(&cdrom->response_fifo, 0x20); // Invalid command error code
            trigger_interrupt(cdrom, 5);
            cdrom->current_state = CD_STATE_ERROR;
            cdrom->status = error_status & ~STAT_BUSY;
            break;
    }

    // Clear parameter FIFO if the handler didn't consume them
    if (!uses_params && command != CDC_INIT) {
         if (!fifo_is_empty(&cdrom->param_fifo)) {
              // Optional: Log warning about unused parameters for this command
              // fprintf(stderr, "CDROM Warning: Unused parameters left in FIFO after command 0x%02x\n", command);
              fifo_clear(&cdrom->param_fifo);
         }
    }
}


// --- Core Public Functions ---

void cdrom_init(Cdrom* cdrom, struct Interconnect* inter) {
    printf("Initializing CD-ROM...\n");
    memset(cdrom, 0, sizeof(Cdrom));
    cdrom->inter = inter;
    cdrom->status = STAT_PRMEMPT | STAT_PRMWRDY;
    cdrom->disc_present = false;
    cdrom->disc_file = NULL;
    cdrom->current_state = CD_STATE_IDLE;
    cdrom->double_speed = false;
    cdrom->sector_size_is_2340 = false;
    fifo_init(&cdrom->param_fifo);
    fifo_init(&cdrom->response_fifo);
    memset(cdrom->data_buffer, 0, sizeof(cdrom->data_buffer)); // Use sizeof
    cdrom->data_buffer_count = 0;
    cdrom->data_buffer_read_ptr = 0;
    printf("  CDROM Initial Status: 0x%02x\n", cdrom->status);
}

/**
 * @brief Attempts to load a disc image file and its filesystem info.
 * @param cdrom Pointer to the Cdrom state structure.
 * @param bin_filename Path to the .bin or .iso file.
 * @return True if successful, false otherwise.
 */
bool cdrom_load_disc(Cdrom* cdrom, const char* bin_filename) {
    if (cdrom->disc_file) {
        fclose(cdrom->disc_file);
        cdrom->disc_file = NULL;
    }
    cdrom->disc_present = false;
    cdrom->pvd_valid = false; // <<< ADD THIS LINE
    cdrom->is_cd_da = false;

    printf("CDROM: Attempting to load disc image '%s'\n", bin_filename);
    cdrom->disc_file = fopen(bin_filename, "rb");

    if (!cdrom->disc_file) {
        perror("CDROM Error: Failed to open disc image");
        return false;
    }

    printf("CDROM: Disc image loaded successfully.\n");
    cdrom->disc_present = true;
    cdrom->current_state = CD_STATE_IDLE;

// Attempt to read the PVD
    if (iso_read_pvd(cdrom, &cdrom->pvd)) {
        cdrom->pvd_valid = true;
        printf("CDROM: Successfully parsed ISO9660 Primary Volume Descriptor.\n");

        // --- NEW: Find SYSTEM.CNF ---
        // The root directory record is conveniently located inside the PVD itself.
        IsoDirectoryRecord* root_record = (IsoDirectoryRecord*)cdrom->pvd.root_directory_record;

        // We need a buffer to hold the record we find, because its size is variable.
        uint8_t found_record_buffer[256];
        IsoDirectoryRecord* system_cnf_record = (IsoDirectoryRecord*)found_record_buffer;

        // Search for the file. Note the ";1" is the file version for ISO9660.
        if (iso_find_file(cdrom, root_record, "SYSTEM.CNF;1", system_cnf_record)) {
             printf("CDROM: Found SYSTEM.CNF at LBA %u, size %u bytes.\n",
                    system_cnf_record->extent_location_le,
                    system_cnf_record->data_length_le);
        }
        // --- End of new section ---

    } else {
        cdrom->pvd_valid = false;
        fprintf(stderr, "CDROM Warning: Could not find a valid ISO9660 PVD. This may not be a game disc.\n");
    }

    return true;
}

uint8_t cdrom_read_register(Cdrom* cdrom, uint32_t addr) {
    uint8_t offset = addr & 0x3;
    uint8_t result = 0;
    uint8_t reg_index = cdrom->index;

    if (offset == CDREG_INDEX) { // Offset 0: Status Register
        update_status_register(cdrom);
        result = cdrom->status;
        return result;
    }

    switch (offset) {
        case CDREG_RESPONSE: // Offset 1: Response FIFO - Requires Index 1
            if (reg_index == 1) {
                result = fifo_pop(&cdrom->response_fifo);
                if (fifo_is_empty(&cdrom->response_fifo)) {
                    cdrom->interrupt_flags &= ~(1 << 2); // Clear INT3 flag
                }
            } else { result = 0xFF; } // Error
            break;

        case CDREG_DATA: // Offset 2: Data Buffer - Requires Index 2
             if (reg_index == 2) {
                 if (cdrom->data_buffer_read_ptr < cdrom->data_buffer_count) {
                     result = cdrom->data_buffer[cdrom->data_buffer_read_ptr];
                     cdrom->data_buffer_read_ptr++;
                     if (cdrom->data_buffer_read_ptr >= cdrom->data_buffer_count) {
                        // TODO: Clear INT1 flag?
                     }
                 } else { result = 0; } // Empty
             } else { result = 0xFF; } // Error
            break;

        case CDREG_IRQ_EN_FLAG: // Offset 3: Interrupt Enable/Flags - Requires Index 1
            if (reg_index == 1) {
                uint8_t flags_mapped = ((cdrom->interrupt_flags & 0x7) << 5); // Map INT1-3 -> bits 5-7?
                result = (cdrom->interrupt_enable & 0x1F) | flags_mapped;
            } else { result = 0xFF; } // Error
            break;

        default: result = 0xFF; break; // Should not happen
    }
    return result;
}

void cdrom_write_register(Cdrom* cdrom, uint32_t addr, uint8_t value) {
    uint8_t offset = addr & 0x3;
    uint8_t reg_index = cdrom->index;

    if (offset == CDREG_INDEX) { // Index Select (1800h.0)
        cdrom->index = value & 0x3;
        return;
    }

    switch (offset) {
        case CDREG_COMMAND: // Command Register (1801h) - Requires Index 0
            if (reg_index == 0) cdrom_handle_command(cdrom, value);
            else fprintf(stderr, "CDROM Write Error: Write to Command Reg (1801h) with Index %d != 0\n", reg_index);
            break;

        case CDREG_PARAMETER: // Parameter FIFO (1802h) - Requires Index 0
            if (reg_index == 0) {
                 if (!fifo_push(&cdrom->param_fifo, value)) fprintf(stderr, "CDROM Warning: Parameter FIFO overflow!\n");
                 update_status_register(cdrom);
            } else fprintf(stderr, "CDROM Write Error: Write to Param Reg (1802h) with Index %d != 0\n", reg_index);
            break;

        case CDREG_REQUEST: // Request/Interrupt Reg (1803h) - Index 0 or 1
             if (reg_index == 0) { // Request Register Write (1803h.0)
                if (value & 0x80) fifo_clear(&cdrom->param_fifo);
                update_status_register(cdrom);
             } else if (reg_index == 1) { // Interrupt Enable / Ack Write (1803h.1)
                 cdrom->interrupt_enable = value & 0x1F;
                 uint8_t ack_flags = value & 0x1F;
                 cdrom->interrupt_flags &= ~ack_flags;
                 if (value == 0x40) cdrom->interrupt_flags = 0;
             } else fprintf(stderr, "CDROM Write Error: Write to 1803h with Index %d != 0 or 1\n", reg_index);
            break;
    }
}

/**
 * @brief Steps the CDROM state machine forward in time.
 */
void cdrom_step(Cdrom* cdrom, uint32_t cycles) {
    // Decrement any active timers
    if (cdrom->read_delay_timer > 0) {
        cdrom->read_delay_timer -= (cycles < cdrom->read_delay_timer) ? cycles : cdrom->read_delay_timer;
    }

    // Handle completion of a ReadN command
    if (cdrom->current_state == CD_STATE_READING && cdrom->read_delay_timer == 0) {
        printf("  CDROM ReadN: Completing read for LBA %u\n", cdrom->target_lba);
        
        long long offset = (long long)cdrom->target_lba * CD_RAW_SECTOR_SIZE;
        uint8_t raw_sector_buffer[CD_RAW_SECTOR_SIZE];

        if (fseek(cdrom->disc_file, (long)offset, SEEK_SET) != 0) { /* ... error handling ... */ return; }
        if (fread(raw_sector_buffer, 1, CD_RAW_SECTOR_SIZE, cdrom->disc_file) != CD_RAW_SECTOR_SIZE) { /* ... error handling ... */ return; }

        uint32_t bytes_to_copy = cdrom->sector_size_is_2340 ? CD_MODE_RAWISH_SIZE : CD_USER_DATA_SIZE;
        uint32_t copy_offset_in_raw = cdrom->sector_size_is_2340 ? CD_MODE_RAWISH_OFFSET : CD_MODE2_FORM1_HEADER_SIZE;
        
        memcpy(cdrom->data_buffer, raw_sector_buffer + copy_offset_in_raw, bytes_to_copy);
        cdrom->data_buffer_count = bytes_to_copy;
        cdrom->data_buffer_read_ptr = 0;

        trigger_interrupt(cdrom, 1); // INT1: Data Ready

        update_status_register(cdrom);
        cdrom->status &= ~STAT_BUSY;
        fifo_push(&cdrom->response_fifo, cdrom->status);
        trigger_interrupt(cdrom, 2); // INT2: Command Complete

        cdrom->target_lba++;
        cdrom->current_state = CD_STATE_IDLE;
    }

    // Handle completion of a GetID command
    if (cdrom->current_state == CD_STATE_GETID_PENDING && cdrom->read_delay_timer == 0) {
        cdrom->status &= ~STAT_BUSY; // Command is no longer busy

        if (!cdrom->disc_present) {
            printf("  CDROM GetID: Responding No Disc (Error INT5)\n");
            fifo_push(&cdrom->response_fifo, cdrom->status);
            fifo_push(&cdrom->response_fifo, 0x80); // Error Code: No Disc
            for (int i = 0; i < 6; ++i) fifo_push(&cdrom->response_fifo, 0);
            trigger_interrupt(cdrom, 5);
            cdrom->current_state = CD_STATE_ERROR;
        } else {
            printf("  CDROM GetID: Responding Licensed Disc (SCEA)\n");
            uint8_t resp[8];
            update_status_register(cdrom);
            resp[0] = cdrom->status;
            resp[1] = 0x02; // Status: Licensed Game Disc
            resp[2] = 0x00; // Disc Type: CD-ROM XA
            resp[3] = 0x00; // System Area
            resp[4] = 'S'; resp[5] = 'C'; resp[6] = 'E'; resp[7] = 'A';
            for (int i = 0; i < 8; ++i) fifo_push(&cdrom->response_fifo, resp[i]);
            trigger_interrupt(cdrom, 2); // INT2: Command Complete
            cdrom->current_state = CD_STATE_IDLE;
        }
    }
}