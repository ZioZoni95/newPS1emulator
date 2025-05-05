/**
 * cdrom.h
 * Header file for the PlayStation CD-ROM Drive emulation.
 */
#ifndef CDROM_H
#define CDROM_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h> // For FILE* type

// Forward declaration
struct Interconnect;

// --- CDROM Register Indices ---
// How the 4 physical 8-bit registers (1F801800h-1F801803h) map based on Index register (1800h) value
// Offset refers to the physical address LSB (0-3)
#define CDREG_INDEX       0 // Offset 0: Write Index Select; Read Status Register
#define CDREG_COMMAND     1 // Offset 1: Write Command Register (Requires Index 0)
#define CDREG_RESPONSE    1 // Offset 1: Read Response FIFO (Requires Index 1)
#define CDREG_PARAMETER   2 // Offset 2: Write Parameter FIFO (Requires Index 0)
#define CDREG_DATA        2 // Offset 2: Read Data FIFO/Buffer (Requires Index 2)
#define CDREG_REQUEST     3 // Offset 3: Write Request Register (Index 0); Write IRQ Enable/Ack (Index 1)
#define CDREG_IRQ_EN_FLAG 3 // Offset 3: Read IRQ Enable/Flags (Requires Index 1)

// --- CDROM Commands (Partial List) ---
#define CDC_GETSTAT     0x01 // Get current drive status
#define CDC_SETLOC      0x02 // Set position (LBA) for read/play
#define CDC_READN       0x06 // Read sectors starting at SetLoc position (Normal read)
#define CDC_PAUSE       0x09 // Pause playback/reading, sends response
#define CDC_INIT        0x0A // Initialize controller/drive state
#define CDC_SEEKL       0x15 // Seek to LBA (Logical - data track only?)
#define CDC_TEST        0x19 // Test commands (various subfunctions)
#define CDC_GETID       0x1A // Get drive ID (returns SCEx string / No Disc / Licensed status)
#define CDC_STOP        0x08 // Stop CD-DA playback/Read <<< Add this if missing

#define CD_SECTOR_SIZE 2352 // Common raw sector size for Mode 2

// --- Simple FIFO Placeholder ---
// Represents Parameter and Response FIFOs (limited size).
// NOTE: A proper FIFO implementation needs better head/tail/wrap logic.
#define FIFO_SIZE 16
typedef struct {
    uint8_t data[FIFO_SIZE];
    uint8_t count;    // Number of bytes currently in FIFO
    uint8_t read_ptr; // Index of the next byte to read
    // uint8_t write_ptr; // Needed for proper wrap-around
} Fifo8;

// --- CDROM Internal State ---
// Basic state machine for the drive
typedef enum {
    CD_STATE_IDLE,       // Doing nothing
    CD_STATE_CMD_EXEC,   // Processing a command (e.g., Seek, Init) that takes time
    CD_STATE_READING,    // Executing ReadN/ReadS, data transfer pending/active
    CD_STATE_ERROR       // An error occurred on the last command
} CdromState;

// --- CDROM State Structure ---
// Holds the complete state of the emulated CD-ROM drive and controller.
typedef struct {
    // --- Controller Registers/State ---
    /** @brief Currently selected register index (0-3), written via 1800h.0 */
    uint8_t index;
    /** @brief Cached Status register value (read via 1800h.0). Updated dynamically. */
    uint8_t status;
    /** @brief Interrupt Enable register cache (written/read via 1803h.1, lower 5 bits: INT1-5 enable) */
    uint8_t interrupt_enable;
    /** @brief Interrupt Flags cache (read via 1803h.1 upper 3 bits?, cleared by writing 1 to corresponding bit in 1803h.1) */
    uint8_t interrupt_flags; // Bits 0-4 -> INT1-5 Pending? Check docs. Often mapped to upper bits on read.

    // --- FIFOs ---
    /** @brief FIFO for command parameters written via 1802h.0 */
    Fifo8 param_fifo;
    /** @brief FIFO for command responses read via 1801h.1 */
    Fifo8 response_fifo;
    // TODO: Add Data FIFO/Buffer for sector data (read via 1802h.2)

      // --- Data Buffer for Polled Reads --- <<< NEW SECTION
    /** @brief Buffer to hold the last read sector's data */
    uint8_t data_buffer[CD_SECTOR_SIZE];
    /** @brief Number of bytes currently available in the data buffer */
    uint32_t data_buffer_count;
    /** @brief Read pointer within the data buffer */
    uint32_t data_buffer_read_ptr;
    // --------------------------------------- <<< END NEW SECTION

    // --- Internal State Machine ---
    /** @brief Current operational state of the drive */
    CdromState current_state;
    /** @brief Command code currently being processed */
    uint8_t pending_command;
    /** @brief Logical Block Address (LBA) target set by SetLoc command */
    uint32_t target_lba;
    // TODO: Add timers for command completion delays (Seek, Read, Init etc.)

    // --- Disc Handling ---
    /** @brief Flag indicating if a valid disc image is loaded */
    bool disc_present;
    /** @brief Flag indicating if the loaded disc is an Audio CD */
    bool is_cd_da; // TODO: Determine this from CUE sheet or GetID?
// --- Mode Settings (Set by SetMode 0x0E) --- <<< NEW SECTION
    /** @brief Drive speed (0=normal, 1=double) */
    bool double_speed;
    /** @brief Sector size bit (0=2048 bytes, 1=2340 bytes) */
    bool sector_size_is_2340; // True if mode bit 5 is 1

    /** @brief File handle for the loaded .bin or .iso disc image */
    FILE* disc_file;
    // TODO: Add sector buffer, disc size LBA, track information

    /** @brief Pointer back to the interconnect for requesting interrupts */
    struct Interconnect* inter;

} Cdrom;


// --- Function Prototypes ---

/**
 * @brief Initializes the CD-ROM drive state to default values.
 * @param cdrom Pointer to the Cdrom state structure.
 * @param inter Pointer to the Interconnect structure.
 */
void cdrom_init(Cdrom* cdrom, struct Interconnect* inter);

/**
 * @brief Reads an 8-bit value from a CD-ROM controller register address.
 * Handles register indexing based on cdrom->index.
 * @param cdrom Pointer to the Cdrom state structure.
 * @param addr The physical address being accessed (1F801800h - 1F801803h).
 * @return The 8-bit value read from the effective register.
 */
uint8_t cdrom_read_register(Cdrom* cdrom, uint32_t addr);

/**
 * @brief Writes an 8-bit value to a CD-ROM controller register address.
 * Handles register indexing and triggers command execution.
 * @param cdrom Pointer to the Cdrom state structure.
 * @param addr The physical address being accessed (1F801800h - 1F801803h).
 * @param value The 8-bit value to write.
 */
void cdrom_write_register(Cdrom* cdrom, uint32_t addr, uint8_t value);

/**
 * @brief Attempts to load a disc image file (.bin/.iso).
 * Currently does not handle CUE sheets.
 * @param cdrom Pointer to the Cdrom state structure.
 * @param bin_filename Path to the .bin or .iso file.
 * @return True if successful, false otherwise.
 */
bool cdrom_load_disc(Cdrom* cdrom, const char* bin_filename);

// TODO: Add function prototype for stepping CDROM state machine/timing if needed
// void cdrom_step(Cdrom* cdrom, uint32_t cycles);

#endif // CDROM_H