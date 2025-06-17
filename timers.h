// timers.h
#ifndef TIMERS_H
#define TIMERS_H

#include <stdint.h>
#include <stdbool.h>


// Forward declaration needed if Timers struct holds Interconnect pointer later
struct Interconnect;

// --- Timer Register Offsets (relative to timer base) ---
// Timer 0: 0x1F801100, Timer 1: 0x1F801110, Timer 2: 0x1F801120
#define TMR_REG_VAL    0x0 // Counter Value Register (16-bit R/W)
#define TMR_REG_MODE   0x4 // Mode Register (16-bit R/W)
#define TMR_REG_TARGET 0x8 // Target Value Register (16-bit R/W)

// --- Timer Mode Register Bits ---
// (Based on Nocash PSX Spec and common knowledge)
// Bit 0: Sync Enable (0=Pause during sync, 1=Reset counter to 0 at sync) - Requires Sync Mode > 0
// Bit 1-2: Sync Mode:
//          0: Pause counter during H/Vblank or use external signal
//          1: Reset counter to 0 at Hblank (Timer 0, 1 only?)
//          2: Reset counter to 0 at Hblank and pause outside Hblank (Timer 0, 1 only?)
//          3: Pause counter until Hblank occurs once, then free-run.
// Bit 3: Reset counter to 0 when Target is reached (0=No, 1=Yes)
// Bit 4: IRQ when Target value is reached (0=Disable, 1=Enable)
// Bit 5: IRQ when Counter overflows (reaches 0xFFFF) (0=Disable, 1=Enable)
// Bit 6: IRQ Repeat Mode (0=One-shot, 1=Repeatedly)
// Bit 7: IRQ Pulse Mode (0=Short pulse, 1=Toggle) - Affects I_STAT bit
// Bit 8-9: Clock Source Select:
//          0: System Clock (CPU Freq / 4 ?)
//          1: Dot Clock (GPU clock, frequency depends on video mode)
//          2: Dot Clock / 8 (Timer 2 only?)
//          3: Hblank (Timer 1 only?)
// Bit 10: IRQ Request (Read-Only, reflects interrupt status)
// Bit 11: Reached Target (Read-Only, sticky until Mode write acknowledges)
// Bit 12: Reached 0xFFFF (Read-Only, sticky until Mode write acknowledges)
// Bit 13-15: Unknown/Unused

// --- Structure for a Single Timer ---
typedef struct {
    uint16_t counter; // Current 16-bit counter value
    uint16_t mode;    // 16-bit mode register value
    uint16_t target;  // 16-bit target value

    // Internal emulation state derived from mode register & runtime behavior
    bool sync_enable;       // Mode[0]
    uint8_t sync_mode;        // Mode[1-2]
    bool reset_on_target;   // Mode[3]
    bool irq_on_target;     // Mode[4]
    bool irq_on_ffff;       // Mode[5]
    bool irq_repeat;        // Mode[6]
    bool irq_pulse;         // Mode[7]
    uint8_t clock_source;     // Mode[8-9]

    bool interrupt_requested; // Internal flag: True if IRQ condition met this cycle
    bool reached_target_flag; // Internal sticky flag mirroring Mode[11]
    bool reached_ffff_flag; // Internal sticky flag mirroring Mode[12]

    // Variables for handling fractional clock cycles might be needed here later
    // double fractional_cycles;

} Timer;

// --- Structure for all Three Timers ---
typedef struct {
    Timer timers[3]; // Array containing state for Timer 0, Timer 1, Timer 2

    // Pointer back to interconnect needed for requesting interrupts
    struct Interconnect* inter;
    double fractional_ticks[3]; // <<< ADD THIS

} Timers;


// --- Function Prototypes ---

/**
 * @brief Initializes the state of all three timers.
 * @param timers Pointer to the Timers structure.
 * @param inter Pointer to the Interconnect (needed for interrupts).
 */
void timers_init(Timers* timers, struct Interconnect* inter);

/**
 * @brief Reads a 16-bit value from a timer register.
 * @param timers Pointer to the Timers structure.
 * @param timer_index Index of the timer (0, 1, or 2).
 * @param offset Register offset (0x0, 0x4, 0x8).
 * @return The 16-bit value read.
 */
uint16_t timer_read16(Timers* timers, int timer_index, uint32_t offset);

/**
 * @brief Reads a 32-bit value from a timer register pair (Not standard PSX access).
 * Included for completeness if needed by interconnect, but likely just reads lower 16.
 * @param timers Pointer to the Timers structure.
 * @param timer_index Index of the timer (0, 1, or 2).
 * @param offset Register offset (0x0, 0x4, 0x8).
 * @return The 32-bit value (likely just the 16-bit register zero-extended).
 */
uint32_t timer_read32(Timers* timers, int timer_index, uint32_t offset);


/**
 * @brief Writes a 16-bit value to a timer register.
 * @param timers Pointer to the Timers structure.
 * @param timer_index Index of the timer (0, 1, or 2).
 * @param offset Register offset (0x0, 0x4, 0x8).
 * @param value The 16-bit value to write.
 */
void timer_write16(Timers* timers, int timer_index, uint32_t offset, uint16_t value);

/**
 * @brief Writes a 32-bit value to a timer register pair (Not standard PSX access).
 * Included for completeness if needed by interconnect, likely just writes lower 16.
 * @param timers Pointer to the Timers structure.
 * @param timer_index Index of the timer (0, 1, or 2).
 * @param offset Register offset (0x0, 0x4, 0x8).
 * @param value The 32-bit value (lower 16 bits are likely used).
 */
void timer_write32(Timers* timers, int timer_index, uint32_t offset, uint32_t value);

/**
 * @brief Steps the timers forward by a number of elapsed master clock cycles.
 * Updates counters, checks for target/overflow, and requests interrupts.
 * @param timers Pointer to the Timers structure.
 * @param cycles Number of master clock cycles that have passed.
 */
void timers_step(Timers* timers, uint32_t cycles);


#endif // TIMERS_H