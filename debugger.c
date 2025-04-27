/**
 * debugger.c
 * Implementation of the simple debugger component.
 */
#include "debugger.h" // Include own header (which now includes cpu.h)
// #include "cpu.h"   // This include is now redundant here, but harmless
#include <stdio.h>    // For printf, snprintf, fprintf
#include <string.h>   // For memset (optional initialization)

// NOTE: All function definitions now use 'Cpu*' to match the prototypes
//       which see the typedef via debugger.h including cpu.h.

/**
 * @brief Initializes the debugger state.
 * Clears breakpoint/watchpoint counts and the paused flag.
 * @param dbg Pointer to the Debugger struct to initialize.
 */
void debugger_init(Debugger* dbg) {
    printf("Initializing Debugger...\n");
    dbg->breakpoint_count = 0;
    dbg->read_watchpoint_count = 0;
    dbg->write_watchpoint_count = 0;
    dbg->paused = false;
}

// ============================================================================
// Breakpoint Management
// ============================================================================

/**
 * @brief Adds an execution breakpoint at a specific memory address.
 * @param dbg Pointer to the Debugger instance.
 * @param addr The memory address (PC value) for the breakpoint.
 * @return True if added successfully or already exists, false if the list is full.
 */
bool debugger_add_breakpoint(Debugger* dbg, uint32_t addr) {
    // Check for duplicates
    for (uint32_t i = 0; i < dbg->breakpoint_count; ++i) {
        if (dbg->breakpoints[i] == addr) {
            printf("Debugger: Breakpoint at 0x%08x already exists.\n", addr);
            return true;
        }
    }
    // Check for space
    if (dbg->breakpoint_count >= MAX_BREAKPOINTS) {
        fprintf(stderr, "Debugger Error: Cannot add breakpoint at 0x%08x. Maximum (%d) reached.\n", addr, MAX_BREAKPOINTS);
        return false;
    }
    // Add new breakpoint
    dbg->breakpoints[dbg->breakpoint_count] = addr;
    dbg->breakpoint_count++;
    printf("Debugger: Breakpoint added at 0x%08x. (%u/%d)\n", addr, dbg->breakpoint_count, MAX_BREAKPOINTS);
    return true;
}

/**
 * @brief Removes an execution breakpoint at the specified address.
 * @param dbg Pointer to the Debugger instance.
 * @param addr The memory address of the breakpoint to remove.
 * @return True if removed successfully, false if the breakpoint was not found.
 */
bool debugger_remove_breakpoint(Debugger* dbg, uint32_t addr) {
    for (uint32_t i = 0; i < dbg->breakpoint_count; ++i) {
        if (dbg->breakpoints[i] == addr) {
            // Found: Use swap-with-last for O(1) removal
            dbg->breakpoints[i] = dbg->breakpoints[dbg->breakpoint_count - 1];
            dbg->breakpoint_count--;
            printf("Debugger: Breakpoint removed at 0x%08x. (%u/%d)\n", addr, dbg->breakpoint_count, MAX_BREAKPOINTS);
            return true;
        }
    }
    printf("Debugger: Breakpoint at 0x%08x not found for removal.\n", addr);
    return false;
}

/**
 * @brief Checks if the current Program Counter hits an active breakpoint.
 * @param dbg Pointer to the Debugger instance.
 * @param cpu Pointer to the Cpu instance (using 'Cpu*' typedef).
 */
void debugger_check_breakpoint(Debugger* dbg, Cpu* cpu) {
    if (dbg->paused) return;
    uint32_t current_pc = cpu->current_pc; // Access okay
    for (uint32_t i = 0; i < dbg->breakpoint_count; ++i) {
        if (dbg->breakpoints[i] == current_pc) {
            char reason_str[64];
            snprintf(reason_str, sizeof(reason_str), "Breakpoint hit at PC=0x%08x", current_pc);
            debugger_handle_break(dbg, cpu, reason_str);
            return; // Exit after first hit
        }
    }
}

// ============================================================================
// Watchpoint Management
// ============================================================================

/**
 * @brief Adds a read watchpoint at a specific memory address.
 * @param dbg Pointer to the Debugger instance.
 * @param addr The memory address to watch.
 * @return True if added successfully or already exists, false if the list is full.
 */
bool debugger_add_read_watchpoint(Debugger* dbg, uint32_t addr) {
    for (uint32_t i = 0; i < dbg->read_watchpoint_count; ++i) {
        if (dbg->read_watchpoints[i] == addr) {
             printf("Debugger: Read watchpoint at 0x%08x already exists.\n", addr);
             return true;
        }
    }
    if (dbg->read_watchpoint_count >= MAX_WATCHPOINTS) {
        fprintf(stderr, "Debugger Error: Cannot add read watchpoint at 0x%08x. Maximum (%d) reached.\n", addr, MAX_WATCHPOINTS);
        return false;
    }
    dbg->read_watchpoints[dbg->read_watchpoint_count++] = addr;
    printf("Debugger: Read watchpoint added at 0x%08x. (%u/%d)\n", addr, dbg->read_watchpoint_count, MAX_WATCHPOINTS);
    return true;
}

/**
 * @brief Removes a read watchpoint at the specified address.
 * @param dbg Pointer to the Debugger instance.
 * @param addr The memory address of the watchpoint to remove.
 * @return True if removed successfully, false if not found.
 */
bool debugger_remove_read_watchpoint(Debugger* dbg, uint32_t addr) {
     for (uint32_t i = 0; i < dbg->read_watchpoint_count; ++i) {
        if (dbg->read_watchpoints[i] == addr) {
            dbg->read_watchpoints[i] = dbg->read_watchpoints[--dbg->read_watchpoint_count];
            printf("Debugger: Read watchpoint removed at 0x%08x. (%u/%d)\n", addr, dbg->read_watchpoint_count, MAX_WATCHPOINTS);
            return true;
        }
    }
    printf("Debugger: Read watchpoint at 0x%08x not found for removal.\n", addr);
    return false;
}

/**
 * @brief Adds a write watchpoint at a specific memory address.
 * @param dbg Pointer to the Debugger instance.
 * @param addr The memory address to watch.
 * @return True if added successfully or already exists, false if the list is full.
 */
bool debugger_add_write_watchpoint(Debugger* dbg, uint32_t addr) {
    for (uint32_t i = 0; i < dbg->write_watchpoint_count; ++i) {
        if (dbg->write_watchpoints[i] == addr) {
             printf("Debugger: Write watchpoint at 0x%08x already exists.\n", addr);
             return true;
        }
    }
    if (dbg->write_watchpoint_count >= MAX_WATCHPOINTS) {
        fprintf(stderr, "Debugger Error: Cannot add write watchpoint at 0x%08x. Maximum (%d) reached.\n", addr, MAX_WATCHPOINTS);
        return false;
    }
    dbg->write_watchpoints[dbg->write_watchpoint_count++] = addr;
    printf("Debugger: Write watchpoint added at 0x%08x. (%u/%d)\n", addr, dbg->write_watchpoint_count, MAX_WATCHPOINTS);
    return true;
}

/**
 * @brief Removes a write watchpoint at the specified address.
 * @param dbg Pointer to the Debugger instance.
 * @param addr The memory address of the watchpoint to remove.
 * @return True if removed successfully, false if not found.
 */
bool debugger_remove_write_watchpoint(Debugger* dbg, uint32_t addr) {
     for (uint32_t i = 0; i < dbg->write_watchpoint_count; ++i) {
        if (dbg->write_watchpoints[i] == addr) {
            dbg->write_watchpoints[i] = dbg->write_watchpoints[--dbg->write_watchpoint_count];
            printf("Debugger: Write watchpoint removed at 0x%08x. (%u/%d)\n", addr, dbg->write_watchpoint_count, MAX_WATCHPOINTS);
            return true;
        }
    }
    printf("Debugger: Write watchpoint at 0x%08x not found for removal.\n", addr);
    return false;
}

/**
 * @brief Checks if a memory read access overlaps with any active read watchpoints.
 * @param dbg Pointer to the Debugger instance.
 * @param cpu Pointer to the Cpu instance (using 'Cpu*' typedef).
 * @param addr The starting memory address being read from.
 * @param size The size of the read access (1, 2, or 4 bytes).
 */
void debugger_check_read_watchpoint(Debugger* dbg, Cpu* cpu, uint32_t addr, uint32_t size) {
    if (dbg->paused) return;
    for (uint32_t i = 0; i < dbg->read_watchpoint_count; ++i) {
        uint32_t wp_addr = dbg->read_watchpoints[i];
        // Basic check: Does the watchpoint address fall within the access range [addr, addr + size)?
        if (wp_addr >= addr && wp_addr < (addr + size)) {
             char reason_str[100];
             snprintf(reason_str, sizeof(reason_str),
                      "Read watchpoint triggered for wp@0x%08x (Access Addr=0x%08x, Size=%u, PC=0x%08x)",
                      wp_addr, addr, size, cpu->current_pc); // Access member okay
             debugger_handle_break(dbg, cpu, reason_str); // Pass 'Cpu*'
             return; // Exit after first hit
        }
    }
}

/**
 * @brief Checks if a memory write access overlaps with any active write watchpoints.
 * @param dbg Pointer to the Debugger instance.
 * @param cpu Pointer to the Cpu instance (using 'Cpu*' typedef).
 * @param addr The starting memory address being written to.
 * @param size The size of the write access (1, 2, or 4 bytes).
 */
void debugger_check_write_watchpoint(Debugger* dbg, Cpu* cpu, uint32_t addr, uint32_t size) {
     if (dbg->paused) return;
     for (uint32_t i = 0; i < dbg->write_watchpoint_count; ++i) {
        uint32_t wp_addr = dbg->write_watchpoints[i];
        // Basic check: Does the watchpoint address fall within the access range [addr, addr + size)?
        if (wp_addr >= addr && wp_addr < (addr + size)) {
             char reason_str[100];
             snprintf(reason_str, sizeof(reason_str),
                      "Write watchpoint triggered for wp@0x%08x (Access Addr=0x%08x, Size=%u, PC=0x%08x)",
                      wp_addr, addr, size, cpu->current_pc); // Access member okay
             debugger_handle_break(dbg, cpu, reason_str); // Pass 'Cpu*'
             return; // Exit after first hit
        }
    }
}


// ============================================================================
// Break Handler
// ============================================================================

/**
 * @brief Handles the debugger state when a break condition occurs.
 * Prints information and sets the paused flag. The main loop must check this flag.
 * @param dbg Pointer to the Debugger instance.
 * @param cpu Pointer to the Cpu instance (using 'Cpu*' typedef).
 * @param reason A string describing why the break occurred.
 */
void debugger_handle_break(Debugger* dbg, Cpu* cpu, const char* reason) {
    printf("\n--- Debugger Break ---\n");
    printf("Reason: %s\n", reason);
    printf("PC:     0x%08x\n", cpu->current_pc); // Access member okay
    // Example: Print some registers
    printf(" R4(a0): %08x  R5(a1): %08x  R6(a2): %08x  R7(a3): %08x\n",
           cpu->regs[4], cpu->regs[5], cpu->regs[6], cpu->regs[7]);
    printf(" R8(t0): %08x  R9(t1): %08x R10(t2): %08x R11(t3): %08x\n",
           cpu->regs[8], cpu->regs[9], cpu->regs[10], cpu->regs[11]);
    dbg->paused = true; // Signal the main loop to pause
    printf("Execution Paused. (Implement resume mechanism in main loop)\n");
    printf("----------------------\n");
}