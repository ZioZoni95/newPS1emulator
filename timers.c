// timers.c
#include "timers.h"
#include "interconnect.h" // Needed for interconnect_request_irq and IRQ defines
#include <stdio.h>
#include "gpu.h"
#include <string.h>
#include <math.h> // For floor()

#define PSX_CPU_HZ 33868800.0
#define PSX_SYSCLK_HZ PSX_CPU_HZ // System Clock is the same as the CPU clock for timers
#define DOTCLOCK_NTSC_HZ 25175000.0
#define DOTCLOCK_PAL_HZ 25200000.0 // PAL frequency, for completeness
#define HBLANK_NTSC_HZ 15625.0 // Horizontal blanking frequency for NTSC


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

    for (int i = 0; i < 3; ++i) {
        Timer* t = &timers->timers[i];

        // --- 1. Determine if timer is paused by Sync Mode ---
        bool is_paused = false;
        if (t->sync_enable) {
            // NOTE: Full sync implementation requires GPU timing signals.
            // For now, we assume mode 0 (pause) and that they are not paused,
            // as this is enough to get past the BIOS hang.
            // A full implementation would check if we are currently inside VBlank/HBlank.
            is_paused = false; 
        }

        if (is_paused) {
            continue; // Timer is paused, do nothing for it this step.
        }
        double timer_clock_hz = 0.0;

        
        // --- 2. Determine Clock Source and Ticks to Add ---
        double ticks_to_add = timers->fractional_ticks[i]; // Start with leftover fraction from last step

        // Timer 0: System Clock or Dot Clock
        if (i == 0) {
            timer_clock_hz = (t->clock_source == 0) ? PSX_SYSCLK_HZ : DOTCLOCK_NTSC_HZ; // Simplified NTSC
            ticks_to_add += (double)cpu_cycles * (timer_clock_hz / PSX_CPU_HZ);
        }
        // Timer 1: System Clock or H-Blank
        else if (i == 1) {
            if (t->clock_source <= 1) { // 0 or 1
                 timer_clock_hz = PSX_SYSCLK_HZ; // Sources 0 and 1 are System Clock for Timer 1
                 ticks_to_add += (double)cpu_cycles * (timer_clock_hz / PSX_CPU_HZ);
            } else { // Source 2 or 3 is H-Blank
                // TODO: This requires accurate GPU dot/line counting. For now, we can approximate.
                ticks_to_add += (double)cpu_cycles * (HBLANK_NTSC_HZ / PSX_CPU_HZ);
            }
        }
        // Timer 2: System Clock or System Clock / 8
        else { // i == 2
             timer_clock_hz = (t->clock_source <= 1) ? PSX_SYSCLK_HZ : (PSX_SYSCLK_HZ / 8.0);
             ticks_to_add += (double)cpu_cycles * (timer_clock_hz / PSX_CPU_HZ);
        }

        uint32_t whole_ticks = (uint32_t)floor(ticks_to_add);
        if (whole_ticks == 0) {
             timers->fractional_ticks[i] = ticks_to_add; // Save fraction and continue
             continue;
        }
        timers->fractional_ticks[i] = ticks_to_add - (double)whole_ticks; // Keep the new remainder

        // --- 3. Increment Counter and Check for Events ---
        uint32_t old_counter = t->counter;
        t->counter += whole_ticks;

        // Check for target reached
        if (old_counter < t->target && t->counter >= t->target) {
            t->reached_target_flag = true;
        }

        // Check for overflow (0xFFFF -> 0x0000)
        if (t->counter < old_counter) { // Simple wrap-around check
            t->reached_ffff_flag = true;
        }

        // --- 4. Handle Interrupts ---
        bool irq = false;
        if (t->irq_on_target && t->reached_target_flag) {
            irq = true;
        }
        if (t->irq_on_ffff && t->reached_ffff_flag) {
            irq = true;
        }

        if (irq) {
            // Set the interrupt request bit in the mode register
            t->mode |= (1 << 10);
            // Request the interrupt line from the interconnect
            interconnect_request_irq(timers->inter, IRQ_TIMER0 + i);
        }

        // --- 5. Handle Counter Reset ---
        if (t->reset_on_target && t->reached_target_flag) {
            t->counter = 0;
            // IMPORTANT: Per specs, sticky flags are only reset by writing to the Mode register,
            // so we don't clear them here. This is correct.
        }
    }
}