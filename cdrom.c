// --- cdrom.c ---
#include "cdrom.h"
#include "interconnect.h" // For interrupt definitions/requests IRQ_CDROM
#include <stdio.h>
#include <string.h> // For memset
#include <stdlib.h> // For exit if needed

// --- Sector Structure Constants ---
#define CD_RAW_SECTOR_SIZE 2352
#define CD_USER_DATA_SIZE 2048
#define CD_MODE2_FORM1_HEADER_SIZE 24
#define CD_MODE_RAWISH_SIZE 2340
#define CD_MODE_RAWISH_OFFSET 12

// --- CDROM Commands (from your header) ---
#define CDC_GETSTAT     0x01
#define CDC_SETLOC      0x02
#define CDC_READN       0x06
#define CDC_STOP        0x08
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
static void cmd_stop(Cdrom* cdrom);

// --- NEW: Forward declarations for completion handlers ---
static void cmd_init_complete(Cdrom* cdrom);
static void cmd_get_id_complete(Cdrom* cdrom);
static void cmd_pause_complete(Cdrom* cdrom);
static void cmd_read_n_complete(Cdrom* cdrom);
static void cmd_set_loc_complete(Cdrom* cdrom);

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
static void cdrom_schedule_event(Cdrom* cdrom, uint32_t cycles, void (*handler)(Cdrom*));


// --- FIFO Helpers (Your implementation is great, no changes needed) ---
static void fifo_init(Fifo8* fifo) { memset(fifo, 0, sizeof(Fifo8)); }
static bool fifo_push(Fifo8* fifo, uint8_t value) {
    if (fifo->count >= FIFO_SIZE) return false;
    uint8_t write_ptr = (fifo->read_ptr + fifo->count) % FIFO_SIZE;
    fifo->data[write_ptr] = value;
    fifo->count++;
    return true;
}
static uint8_t fifo_pop(Fifo8* fifo) {
    if (fifo->count == 0) return 0;
    uint8_t value = fifo->data[fifo->read_ptr];
    fifo->read_ptr = (fifo->read_ptr + 1) % FIFO_SIZE;
    fifo->count--;
    return value;
}
static void fifo_clear(Fifo8* fifo) { fifo->count = 0; fifo->read_ptr = 0; }
static bool fifo_is_empty(Fifo8* fifo) { return fifo->count == 0; }
static bool fifo_is_full(Fifo8* fifo) { return fifo->count >= FIFO_SIZE; }
static uint8_t bcd_to_int(uint8_t bcd) { return ((bcd >> 4) * 10) + (bcd & 0x0F); }

// --- Status Register Bits (from nocash specs) ---
#define STAT_PRMEMPT    (1 << 2) // Parameter FIFO empty
#define STAT_PRMWRDY    (1 << 3) // Parameter FIFO not full
#define STAT_RSLRDY     (1 << 4) // Response FIFO Not Empty
#define STAT_DTEN       (1 << 5) // Data FIFO Not Empty
#define STAT_BUSY       (1 << 6)
#define STAT_MOTORON    (1 << 7) // Motor is on

// --- Internal Helper Functions ---

static void cdrom_schedule_event(Cdrom* cdrom, uint32_t cycles, void (*handler)(Cdrom*)) {
    cdrom->cycles_until_event = cycles;
    cdrom->pending_completion_handler = handler;
}

static void update_status_register(Cdrom* cdrom) {
    uint8_t preserved_bits = cdrom->status & (STAT_BUSY | STAT_MOTORON);
    cdrom->status = (cdrom->index & 0x03) | preserved_bits;

    if (fifo_is_empty(&cdrom->param_fifo)) cdrom->status |= STAT_PRMEMPT;
    if (!fifo_is_full(&cdrom->param_fifo)) cdrom->status |= STAT_PRMWRDY;
    if (!fifo_is_empty(&cdrom->response_fifo)) cdrom->status |= STAT_RSLRDY;
    if (cdrom->data_buffer_count > cdrom->data_buffer_read_ptr) cdrom->status |= STAT_DTEN;
}

static void trigger_interrupt(Cdrom* cdrom, uint8_t int_code) {
    if (int_code > 0 && int_code < 8) {
        uint8_t flag_bit = 1 << (int_code - 1);
        cdrom->interrupt_flags |= flag_bit;
        if (cdrom->interrupt_enable & flag_bit) {
            interconnect_request_irq(cdrom->inter, IRQ_CDROM);
        }
    }
}

// --- Command Handlers (This is where the main logic is filled in) ---

static void cmd_get_stat(Cdrom* cdrom) {
    printf("~ CDROM CMD: GetStat (0x01)\n");    
    // <<< MODIFIED >>>
    fifo_clear(&cdrom->response_fifo);
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 3); // INT3: Response Ready
}

// <<< MODIFIED: Implemented two-stage Init >>>
static void cmd_init(Cdrom* cdrom) {
    printf("~ CDROM CMD: Init (0x0A) - Step 1\n");
    cdrom->current_state = CD_STATE_CMD_EXEC;
    cdrom->status |= STAT_BUSY | STAT_MOTORON;

    // First response is immediate (acknowledges command)
    update_status_register(cdrom);
    fifo_clear(&cdrom->response_fifo);
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 3);

    // Schedule the second part (actual init and completion response)
    cdrom_schedule_event(cdrom, 300000, cmd_init_complete);
}

static void cmd_init_complete(Cdrom* cdrom) {
    printf("  CDROM Init - Step 2 (Completion)\n");
    cdrom->status &= ~STAT_BUSY;

    // Reset internal state
    cdrom->interrupt_enable = 0;
    cdrom->interrupt_flags = 0;
    fifo_clear(&cdrom->param_fifo);
    cdrom->double_speed = false;
    cdrom->sector_size_is_2340 = false;
    cdrom->current_state = CD_STATE_IDLE;
    
    // Second response (signals command is finished)
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 2); // INT2: Command Complete
}

// <<< MODIFIED: Implemented two-stage GetID >>>
static void cmd_get_id(Cdrom* cdrom) {
    printf("~ CDROM CMD: GetID (0x1A) - Step 1\n");
    cdrom->current_state = CD_STATE_CMD_EXEC;
    cdrom->status |= STAT_BUSY;

    // First response is immediate (acknowledges command)
    update_status_register(cdrom);
    fifo_clear(&cdrom->response_fifo);
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 3);
    
    // Schedule the result
    cdrom_schedule_event(cdrom, 100000, cmd_get_id_complete);
}

static void cmd_get_id_complete(Cdrom* cdrom) {
    printf("  CDROM GetID - Step 2 (Completion)\n");
    cdrom->status &= ~STAT_BUSY;

    if (!cdrom->disc_present) {
        // No Disc Error response
        uint8_t error_status = (cdrom->status & ~STAT_RSLRDY) | 0x10; // Nocash says STAT=10h for No Disc Error
        fifo_push(&cdrom->response_fifo, error_status);
        fifo_push(&cdrom->response_fifo, 0x80); // Error Code: No Disc
        for(int i = 0; i < 6; ++i) fifo_push(&cdrom->response_fifo, 0);
        trigger_interrupt(cdrom, 5); // INT5: Error
    } else {
        // Standard Licensed Disc response (SCEA)
        update_status_register(cdrom);
        fifo_push(&cdrom->response_fifo, cdrom->status);
        fifo_push(&cdrom->response_fifo, 0x02); // Status: Licensed
        fifo_push(&cdrom->response_fifo, 0x00); // Disc Type: CD-ROM
        fifo_push(&cdrom->response_fifo, 0x00);
        fifo_push(&cdrom->response_fifo, 'S');
        fifo_push(&cdrom->response_fifo, 'C');
        fifo_push(&cdrom->response_fifo, 'E');
        fifo_push(&cdrom->response_fifo, 'A');
        trigger_interrupt(cdrom, 2); // INT2: Command Complete
    }
    cdrom->current_state = CD_STATE_IDLE;
}

// <<< MODIFIED: Implemented Test(0x20) >>>
static void cmd_test(Cdrom* cdrom) {
    uint8_t sub_command = fifo_pop(&cdrom->param_fifo);
    printf("~ CDROM CMD: Test (0x19), Sub: 0x%02x\n", sub_command);
    
    fifo_clear(&cdrom->response_fifo);
    
    // Test commands are weird, they often seem to respond with INT3 and then the result
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 3);

    switch (sub_command) {
        case 0x20: // Get BIOS Date/Version
            printf("  CDROM Test(0x20): Get BIOS Date\n");
            fifo_push(&cdrom->response_fifo, 0x94); // Year
            fifo_push(&cdrom->response_fifo, 0x12); // Month
            fifo_push(&cdrom->response_fifo, 0x20); // Day
            fifo_push(&cdrom->response_fifo, 0xC2); // Version (from SCPH1001)
            break;
        default:
             printf("  CDROM Test: Unhandled sub 0x%02x\n", sub_command);
             fifo_push(&cdrom->response_fifo, 0x00); // Placeholder
             break;
    }
    // Unlike other commands, TEST seems to complete immediately.
    // We don't use the scheduler.
}

// Stubs for other commands - no changes needed yet
static void cmd_pause(Cdrom* cdrom) {
    printf("~ CDROM CMD: Pause (0x09)\n");
    cdrom->current_state = CD_STATE_CMD_EXEC;
    cdrom->status |= STAT_BUSY;
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 3);
    cdrom_schedule_event(cdrom, 150000, cmd_pause_complete);
}

static void cmd_pause_complete(Cdrom* cdrom) {
    printf("  CDROM Pause - Complete\n");
    cdrom->status &= ~STAT_BUSY;
    cdrom->current_state = CD_STATE_IDLE;
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 2);
}

// --- Main command dispatcher (no changes needed) ---
static void cdrom_handle_command(Cdrom* cdrom, uint8_t command) {
    cdrom->pending_command = command;
    switch (command) {
        case CDC_GETSTAT: cmd_get_stat(cdrom); break;
        case CDC_SETLOC:  cmd_set_loc(cdrom); break;
        case CDC_READN:   cmd_read_n(cdrom); break;
        case CDC_PAUSE:   cmd_pause(cdrom); break;
        case CDC_INIT:    cmd_init(cdrom); break;
        case CDC_SETMODE: cmd_set_mode(cdrom); break;
        case CDC_STOP:    cmd_stop(cdrom); break;
        case CDC_SEEKL:   cmd_seek_l(cdrom); break;
        case CDC_TEST:    cmd_test(cdrom); break;
        case CDC_GETID:   cmd_get_id(cdrom); break;
        default:
            fprintf(stderr, "CDROM Error: Unhandled command 0x%02x\n", command);
            break;
    }
}


// --- Core Public Functions ---

// cdrom_init: No changes needed
void cdrom_init(Cdrom* cdrom, struct Interconnect* inter) {
    printf("Initializing CD-ROM...\n");
    memset(cdrom, 0, sizeof(Cdrom));
    cdrom->inter = inter;
    cdrom->status = STAT_PRMEMPT | STAT_PRMWRDY;
    cdrom->disc_present = false;
    cdrom->disc_file = NULL;
    cdrom->current_state = CD_STATE_IDLE;
    fifo_init(&cdrom->param_fifo);
    fifo_init(&cdrom->response_fifo);
    printf("  CDROM Initial Status: 0x%02x\n", cdrom->status);
}

static void cmd_set_loc(Cdrom* cdrom) {
    printf("~ CDROM CMD: SetLoc (0x02)\n");
    if (cdrom->param_fifo.count < 3) {
        printf("  ERROR: SetLoc requires 3 parameters.\n");
        return;
    }
    uint8_t m = bcd_to_int(fifo_pop(&cdrom->param_fifo));
    uint8_t s = bcd_to_int(fifo_pop(&cdrom->param_fifo));
    uint8_t f = bcd_to_int(fifo_pop(&cdrom->param_fifo));
    cdrom->target_lba = (m * 60 * 75) + (s * 75) + f - 150;
    printf("  Set LBA to %u (M:%u S:%u F:%u)\n", cdrom->target_lba, m, s, f);

    cdrom->current_state = CD_STATE_CMD_EXEC;
    cdrom->status |= STAT_BUSY;
    cdrom_schedule_event(cdrom, 10000, cmd_set_loc_complete);
}

// ADDED completion handler
static void cmd_set_loc_complete(Cdrom* cdrom) {
    printf("~ CDROM CMD set_loc_complete)\n");

    cdrom->status &= ~STAT_BUSY;
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 3); // INT3
    cdrom->current_state = CD_STATE_IDLE;
}


// cdrom_load_disc: No changes needed to your fixed version
bool cdrom_load_disc(Cdrom* cdrom, const char* bin_filename) {
    if (cdrom->disc_file) { fclose(cdrom->disc_file); cdrom->disc_file = NULL; }
    
    printf("CDROM: Attempting to load disc image '%s'\n", bin_filename);
    cdrom->disc_file = fopen(bin_filename, "rb");
    if (!cdrom->disc_file) {
        perror("CDROM Error: Failed to open disc image");
        cdrom->disc_present = false;
        return false;
    }
    
    // Your check for directory is good, keeping it
    fgetc(cdrom->disc_file);
    if (ferror(cdrom->disc_file)) {
        perror("CDROM Error: Path is a directory or cannot be read");
        fclose(cdrom->disc_file);
        cdrom->disc_file = NULL;
        cdrom->disc_present = false;
        return false;
    }
    rewind(cdrom->disc_file);

    printf("CDROM: Disc image loaded successfully.\n");
    cdrom->disc_present = true;
    cdrom->current_state = CD_STATE_IDLE;
    return true;
}

// cdrom_read_register: No changes needed
uint8_t cdrom_read_register(Cdrom* cdrom, uint32_t addr) {
    uint8_t offset = addr & 3;
    uint8_t reg_index = cdrom->index;

    if (offset == CDREG_INDEX) {
        update_status_register(cdrom);
        return cdrom->status;
    }

    switch (offset) {
        case CDREG_RESPONSE:
            return (reg_index == 1) ? fifo_pop(&cdrom->response_fifo) : 0;
        case CDREG_DATA:
            return (reg_index == 2 && cdrom->data_buffer_read_ptr < cdrom->data_buffer_count) ? cdrom->data_buffer[cdrom->data_buffer_read_ptr++] : 0;
        case CDREG_IRQ_EN_FLAG:
            return (reg_index == 1) ? (cdrom->interrupt_enable & 0x1F) | ((cdrom->interrupt_flags & 0x7) << 5) : 0;
    }
    return 0;
}

// cdrom_write_register: No changes needed
void cdrom_write_register(Cdrom* cdrom, uint32_t addr, uint8_t value) {
    printf("CDROM Write: Index=%d, Offset=0x%x, Value=0x%02x\n", cdrom->index, addr & 3, value);
    uint8_t offset = addr & 3;
    uint8_t reg_index = cdrom->index;

    if (offset == CDREG_INDEX) {
        cdrom->index = value & 3;
        return;
    }

    switch (offset) {
        case CDREG_COMMAND:
            if (reg_index == 0) cdrom_handle_command(cdrom, value);
            break;
        case CDREG_PARAMETER:
            if (reg_index == 0) fifo_push(&cdrom->param_fifo, value);
            break;
        case CDREG_REQUEST:
            if (reg_index == 0) { // Request
                if (value & 0x80) fifo_clear(&cdrom->param_fifo);
            } else if (reg_index == 1) { // Interrupt
                cdrom->interrupt_enable = value & 0x1F;
                cdrom->interrupt_flags &= ~(value & 0x1F);
                if (value & 0x40) cdrom->interrupt_flags = 0;
            }
            break;
    }
}

static void cmd_read_n(Cdrom* cdrom) {
    printf("~ CDROM CMD: ReadN (0x06)\n");
    cdrom->current_state = CD_STATE_CMD_EXEC;
    cdrom->status |= STAT_BUSY;
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 3); // First response
    cdrom_schedule_event(cdrom, 200000, cmd_read_n_complete);
}

// ADDED completion handler
static void cmd_read_n_complete(Cdrom* cdrom) {
    printf("  CDROM ReadN - Complete\n");
    // This is a dummy read. We don't load from the file yet.
    // We just signal that data is ready.
    cdrom->status &= ~STAT_BUSY;
    cdrom->status |= STAT_DTEN; // Set Data FIFO not empty flag
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 1); // INT1: Data Ready
    cdrom->current_state = CD_STATE_READING;
}

static void cmd_seek_l(Cdrom* cdrom) {
    printf("~ CDROM CMD: SeekL (0x15) - Forwarding to SetLoc\n");
    cmd_set_loc(cdrom); // SeekL is mechanically the same as SetLoc for our purposes
}

static void cmd_set_mode(Cdrom* cdrom) {
    uint8_t mode = fifo_pop(&cdrom->param_fifo);
    printf("~ CDROM CMD: SetMode (0x0E) to 0x%02x\n", mode);
    cdrom->double_speed = (mode & 0x80) != 0;
    cdrom->is_cd_da = (mode & 0x40) != 0;
    cdrom->sector_size_is_2340 = (mode & 0x20) != 0;

    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 3); // INT3
}

static void cmd_stop(Cdrom* cdrom) {
    printf("~ CDROM CMD: Stop (0x08)\n");
    cdrom->current_state = CD_STATE_IDLE;
    cdrom->status &= ~(STAT_BUSY | STAT_MOTORON);
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 2); // INT2
}

// cdrom_step: No changes needed
void cdrom_step(Cdrom* cdrom, uint32_t cycles) {
    if (cdrom->cycles_until_event > 0) {
        if (cycles >= cdrom->cycles_until_event) {
            cdrom->cycles_until_event = 0;
            if (cdrom->pending_completion_handler) {
                cdrom->pending_completion_handler(cdrom);
                cdrom->pending_completion_handler = NULL;
            }
        } else {
            cdrom->cycles_until_event -= cycles;
        }
    }
}