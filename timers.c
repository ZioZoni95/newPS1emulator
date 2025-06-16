// timers.c
#include "timers.h"
#include "interconnect.h" // Needed for interconnect_request_irq and IRQ defines
#include <stdio.h>
#include "gpu.h"
#include <string.h>
#include <math.h> // For floor()

// --- Clock Frequencies (Approximations) ---
#define PSX_CPU_HZ 33868800.0
#define PSX_SYSCLK_HZ (PSX_CPU_HZ / 1.0)
// Define the standard NTSC dot clock frequency from PSXSPX Specifications
#define NTSC_DOTCLOCK_HZ 25175000.0 // <<< ADD THIS


/**
 * @brief Helper function to decode the mode register into internal state flags.
 * Called whenever the mode register is written.
 * @param timer Pointer to the Timer instance to update.
 */
static void timer_update_internal_state(Timer* timer) {
    uint16_t mode = timer->mode;

    timer->sync_enable       = (mode & (1 << 0)) != 0;
    timer->sync_mode         = (mode >> 1) & 0x3;
    timer->reset_on_target   = (mode & (1 << 3)) != 0;
    timer->irq_on_target     = (mode & (1 << 4)) != 0;
    timer->irq_on_ffff       = (mode & (1 << 5)) != 0;
    timer->irq_repeat        = (mode & (1 << 6)) != 0;
    timer->irq_pulse         = (mode & (1 << 7)) != 0;
    timer->clock_source      = (mode >> 8) & 0x3;

    // Writing to mode register acknowledges/clears sticky IRQ flags (Bits 11, 12)
    // Also clear our internal tracking flags.
    timer->reached_target_flag = false;
    timer->reached_ffff_flag   = false;
    // Clear the request flag as well, it will be re-asserted if conditions still met
    timer->interrupt_requested = false;
    // Clear Mode[10] interrupt request flag in the actual hardware register value
    timer->mode &= ~(1 << 10);
}

/**
 * @brief Initializes the state of all three timers.
 * @param timers Pointer to the Timers structure.
 * @param inter Pointer to the Interconnect (needed for requesting interrupts).
 */
void timers_init(Timers* timers, struct Interconnect* inter) {
    printf("Initializing Timers...\n");
    timers->inter = inter; // Store interconnect pointer

    // Initialize all three timers
    for (int i = 0; i < 3; ++i) {
        Timer* t = &timers->timers[i];
        // Reset hardware registers
        t->counter = 0;
        t->mode    = 0;
        t->target  = 0;
        // Reset internal emulation state
        timer_update_internal_state(t); // Decode the initial mode (0)
        // Ensure flags start clear
        t->reached_target_flag = false;
        t->reached_ffff_flag   = false;
        t->interrupt_requested = false;
        timers->fractional_ticks[i] = 0.0;
    }
}


/**
 * @brief Reads a 16-bit value from a timer register.
 * @param timers Pointer to the Timers structure.
 * @param timer_index Index of the timer (0, 1, or 2).
 * @param offset Register offset (0x0, 0x4, 0x8).
 * @return The 16-bit value read.
 */
uint16_t timer_read16(Timers* timers, int timer_index, uint32_t offset) {
    if (timer_index < 0 || timer_index > 2) {
        fprintf(stderr, "Timer Read Error: Invalid timer index %d\n", timer_index);
        return 0;
    }
    Timer* t = &timers->timers[timer_index];

    switch (offset) {
        case TMR_REG_VAL: // 0x0: Counter Value
            return t->counter;
        case TMR_REG_MODE: // 0x4: Mode Register
            {
                // Update read-only status bits before returning mode value
                uint16_t mode = t->mode & ~0x1F00; // Clear status bits 12:10
                mode |= (uint16_t)t->reached_target_flag << 11;
                mode |= (uint16_t)t->reached_ffff_flag << 12;
                // Bit 10 (IRQ flag) reflects internal request state combined with enable bits
                bool irq_flag = (t->reached_target_flag && t->irq_on_target) ||
                                (t->reached_ffff_flag && t->irq_on_ffff);
                mode |= (uint16_t)irq_flag << 10;
                return mode;
            }
        case TMR_REG_TARGET: // 0x8: Target Value
            return t->target;
        default:
            fprintf(stderr, "Timer Read Error: Unhandled timer%d offset 0x%x\n", timer_index, offset);
            return 0;
    }
}

/**
 * @brief Reads a 32-bit value from a timer register pair (Not standard PSX access).
 * For simplicity, just reads the lower 16 bits from the specified offset.
 * @param timers Pointer to the Timers structure.
 * @param timer_index Index of the timer (0, 1, or 2).
 * @param offset Register offset (0x0, 0x4, 0x8).
 * @return The 32-bit value (16-bit register zero-extended).
 */
uint32_t timer_read32(Timers* timers, int timer_index, uint32_t offset) {
    // 32-bit reads to timer registers likely only read the first 16 bits
    return (uint32_t)timer_read16(timers, timer_index, offset);
}


/**
 * @brief Writes a 16-bit value to a timer register.
 * @param timers Pointer to the Timers structure.
 * @param timer_index Index of the timer (0, 1, or 2).
 * @param offset Register offset (0x0, 0x4, 0x8).
 * @param value The 16-bit value to write.
 */
void timer_write16(Timers* timers, int timer_index, uint32_t offset, uint16_t value) {
     if (timer_index < 0 || timer_index > 2) {
        fprintf(stderr, "Timer Write Error: Invalid timer index %d\n", timer_index);
        return;
    }
    Timer* t = &timers->timers[timer_index];

    switch (offset) {
        case TMR_REG_VAL: // 0x0: Counter Value
            t->counter = value;
            break;
        case TMR_REG_MODE: // 0x4: Mode Register
            t->mode = value;
            // Update internal derived state whenever mode changes
            timer_update_internal_state(t);
            break;
        case TMR_REG_TARGET: // 0x8: Target Value
            t->target = value;
            break;
        default:
            fprintf(stderr, "Timer Write Error: Unhandled timer%d offset 0x%x = 0x%04x\n", timer_index, offset, value);
            break;
    }
}

/**
 * @brief Writes a 32-bit value to a timer register pair (Not standard PSX access).
 * Writes the lower 16 bits of the value to the specified register offset.
 * @param timers Pointer to the Timers structure.
 * @param timer_index Index of the timer (0, 1, or 2).
 * @param offset Register offset (0x0, 0x4, 0x8).
 * @param value The 32-bit value (lower 16 bits are used).
 */
void timer_write32(Timers* timers, int timer_index, uint32_t offset, uint32_t value) {
    // 32-bit writes to timer registers likely only write the lower 16 bits
    timer_write16(timers, timer_index, offset, (uint16_t)value);
}

/**
 * @brief Steps the timers forward by a number of elapsed CPU clock cycles.
 * Updates counters based on selected clock source, checks for target/overflow,
 * and requests interrupts via the interconnect. Uses fractional accumulation.
 * @param timers Pointer to the Timers structure.
 * @param cpu_cycles Number of CPU clock cycles presumed to have passed since last call.
 */
void timers_step(Timers* timers, uint32_t cpu_cycles) {
    if (cpu_cycles == 0) return;

    // Get current GPU state needed for DotClock/HBlank (Placeholder access)
    Gpu* gpu = &timers->inter->gpu; // Assumes Interconnect struct has Gpu gpu;
    double current_dot_clock_hz = NTSC_DOTCLOCK_HZ;
    // TODO: Calculate actual current_dot_clock_hz from gpu state (hres, vmode)
    // TODO: Get HBlank count if simulating GPU timing for Timer 1 source 3

    for (int i = 0; i < 3; ++i) {
        Timer* t = &timers->timers[i];
        double timer_clock_hz = 0.0;
        uint32_t external_ticks = 0; // Ticks from HBlank etc.

        // --- Determine Effective Clock Source ---
        switch (t->clock_source) {
            case 0: timer_clock_hz = PSX_SYSCLK_HZ; break;
            case 1: timer_clock_hz = (i == 2) ? (PSX_SYSCLK_HZ / 8.0) : current_dot_clock_hz; break; // Timer2 has Sys/8 if bit 9 is set
            case 2: timer_clock_hz = PSX_SYSCLK_HZ / 8.0; break;
            case 3: if (i == 1) { /* external_ticks = num_hblanks_in_cpu_cycles; */ } break; // Needs GPU timing
        }

        // --- Handle Sync Modes (PLACEHOLDER) ---
        bool paused_by_sync = false;
        if (t->sync_enable) {
            // TODO: Implement sync logic based on GPU VBlank/HBlank state
            // E.g., if (mode == 0 && !in_hblank_or_vblank) paused_by_sync = true;
            // E.g., if ((mode==1||mode==2) && hblank_just_occurred) t->counter = 0;
        }

        if (paused_by_sync) {
            timers->fractional_ticks[i] = 0.0; // Reset fractional part if paused? Maybe not.
            continue;
        }

        // --- Calculate Ticks To Add ---
        double ticks_to_add = timers->fractional_ticks[i]; // Start with leftover fraction
        if (timer_clock_hz > 0) {
            ticks_to_add += (double)cpu_cycles * (timer_clock_hz / PSX_CPU_HZ);
        }
        ticks_to_add += external_ticks; // Add ticks from Hblank etc.

        uint32_t whole_ticks = (uint32_t)floor(ticks_to_add);
        timers->fractional_ticks[i] = ticks_to_add - floor(ticks_to_add); // Store remainder

        if (whole_ticks == 0) continue; // No whole ticks elapsed

        // --- Increment Counter & Check Flags ---
        uint16_t old_counter = t->counter;
        uint64_t next_counter_val_64 = (uint64_t)old_counter + whole_ticks;
        t->counter = (uint16_t)next_counter_val_64; // Assign wrapped 16-bit value

        bool target_just_reached = false;
        bool overflow_just_occurred = (next_counter_val_64 > 0xFFFF);

        // Check if target was reached or passed during this step interval
        if (t->target != 0 && old_counter < t->target && next_counter_val_64 >= t->target) {
            target_just_reached = true;
        }
        // Handle rarer case where counter wraps *past* target in one large step
        if (t->target != 0 && overflow_just_occurred && t->target >= old_counter) {
             target_just_reached = true;
        }

        // Update sticky flags if condition met this step
        if (target_just_reached) t->reached_target_flag = true;
        if (overflow_just_occurred) t->reached_ffff_flag = true;

        // --- Handle Reset ---
        if (target_just_reached && t->reset_on_target) {
            t->counter = 0;
            // Sticky flags are NOT reset here, only by writing Mode reg
        }

        // --- Handle Interrupt Request ---
        bool should_request_irq = (t->reached_target_flag && t->irq_on_target) ||
                                  (t->reached_ffff_flag && t->irq_on_ffff);

        if (should_request_irq) {
            // Only set internal request flag if condition newly met this step?
            // Or just set it if condition met and enabled? Let's assume the latter for repeat mode.
            bool condition_newly_met = (target_just_reached && t->irq_on_target) ||
                                       (overflow_just_occurred && t->irq_on_ffff);

            if (condition_newly_met || t->irq_repeat) {
                 // If repeating, or if newly met (for one-shot or repeat)
                 t->interrupt_requested = true;
                 uint32_t irq_line = IRQ_TIMER0 + i;
                 interconnect_request_irq(timers->inter, irq_line);
            }
        }
        // How does one-shot clear? Writing to Mode clears sticky flags,
        // which prevents should_request_irq from being true again until flags re-set.
        // So, no need to explicitly clear t->interrupt_requested for one-shot here.
    }
}