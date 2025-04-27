/**
 * debugger.h
 * Header file for the simple debugger component.
 * Defines the debugger state and functions for setting breakpoints and watchpoints.
 */
#ifndef DEBUGGER_H
#define DEBUGGER_H

#include <stdint.h> // For uint32_t etc.
#include <stdbool.h> // For bool type

// --- Include Cpu Definition ---
/**
 * @brief Include cpu.h directly here.
 * This makes the 'Cpu' typedef (defined in cpu.h) visible
 * for use in the function prototypes below. This ensures type consistency
 * between the declarations here and the definitions in debugger.c.
 */
#include "cpu.h" // <<< INCLUDE cpu.h HERE

// --- Configuration ---
#define MAX_BREAKPOINTS 16 ///< Maximum number of active execution breakpoints.
#define MAX_WATCHPOINTS 16 ///< Maximum number of active read/write watchpoints.

// --- Debugger State Structure ---
/**
 * @brief Holds the state for the debugger, including breakpoints and watchpoints.
 */
typedef struct {
    // Breakpoints (triggered by Program Counter value)
    uint32_t breakpoints[MAX_BREAKPOINTS]; ///< Array storing addresses of active breakpoints.
    uint32_t breakpoint_count;             ///< Current number of active breakpoints.

    // Read Watchpoints (triggered by memory load address)
    uint32_t read_watchpoints[MAX_WATCHPOINTS]; ///< Array storing addresses of read watchpoints.
    uint32_t read_watchpoint_count;             ///< Current number of active read watchpoints.

    // Write Watchpoints (triggered by memory store address)
    uint32_t write_watchpoints[MAX_WATCHPOINTS];///< Array storing addresses of write watchpoints.
    uint32_t write_watchpoint_count;            ///< Current number of active write watchpoints.

    /**
     * @brief Flag indicating if the emulator execution should be paused.
     * Set by debugger_handle_break, should be checked by the main loop.
     */
    bool paused;

} Debugger;

// --- Function Prototypes ---
// Prototypes now use 'Cpu*' (the typedef) because cpu.h was included above.

/**
 * @brief Initializes the debugger state.
 * Clears breakpoint/watchpoint counts and the paused flag.
 * @param dbg Pointer to the Debugger struct to initialize.
 */
void debugger_init(Debugger* dbg);

// --- Breakpoint Management ---

/**
 * @brief Adds an execution breakpoint at a specific memory address.
 * Execution will pause when the Program Counter (PC) reaches this address.
 * @param dbg Pointer to the Debugger instance.
 * @param addr The memory address (PC value) for the breakpoint.
 * @return True if added successfully or already exists, false if the list is full.
 */
bool debugger_add_breakpoint(Debugger* dbg, uint32_t addr);

/**
 * @brief Removes an execution breakpoint at the specified address.
 * @param dbg Pointer to the Debugger instance.
 * @param addr The memory address of the breakpoint to remove.
 * @return True if removed successfully, false if the breakpoint was not found.
 */
bool debugger_remove_breakpoint(Debugger* dbg, uint32_t addr);

// --- Watchpoint Management ---

/**
 * @brief Adds a read watchpoint at a specific memory address.
 * Execution will pause when the CPU attempts to read from this address.
 * @param dbg Pointer to the Debugger instance.
 * @param addr The memory address to watch for read accesses.
 * @return True if added successfully or already exists, false if the list is full.
 */
bool debugger_add_read_watchpoint(Debugger* dbg, uint32_t addr);

/**
 * @brief Removes a read watchpoint at the specified address.
 * @param dbg Pointer to the Debugger instance.
 * @param addr The memory address of the read watchpoint to remove.
 * @return True if removed successfully, false if the watchpoint was not found.
 */
bool debugger_remove_read_watchpoint(Debugger* dbg, uint32_t addr);

/**
 * @brief Adds a write watchpoint at a specific memory address.
 * Execution will pause when the CPU attempts to write to this address.
 * @param dbg Pointer to the Debugger instance.
 * @param addr The memory address to watch for write accesses.
 * @return True if added successfully or already exists, false if the list is full.
 */
bool debugger_add_write_watchpoint(Debugger* dbg, uint32_t addr);

/**
 * @brief Removes a write watchpoint at the specified address.
 * @param dbg Pointer to the Debugger instance.
 * @param addr The memory address of the write watchpoint to remove.
 * @return True if removed successfully, false if the watchpoint was not found.
 */
bool debugger_remove_write_watchpoint(Debugger* dbg, uint32_t addr);

// --- Debugger Hooks (Called by CPU) ---

/**
 * @brief Checks if the current Program Counter hits an active breakpoint.
 * Should be called by the CPU *before* executing the instruction at cpu->current_pc.
 * @param dbg Pointer to the Debugger instance.
 * @param cpu Pointer to the Cpu instance (using 'Cpu*' typedef).
 */
void debugger_check_breakpoint(Debugger* dbg, Cpu* cpu); // <<< Use 'Cpu*'

/**
 * @brief Checks if a memory read access hits an active read watchpoint.
 * Should be called by the CPU *before* performing a memory load operation.
 * @param dbg Pointer to the Debugger instance.
 * @param cpu Pointer to the Cpu instance (using 'Cpu*' typedef).
 * @param addr The memory address being read from.
 * @param size The size of the read access (1, 2, or 4 bytes).
 */
void debugger_check_read_watchpoint(Debugger* dbg, Cpu* cpu, uint32_t addr, uint32_t size); // <<< Use 'Cpu*'

/**
 * @brief Checks if a memory write access hits an active write watchpoint.
 * Should be called by the CPU *before* performing a memory store operation.
 * @param dbg Pointer to the Debugger instance.
 * @param cpu Pointer to the Cpu instance (using 'Cpu*' typedef).
 * @param addr The memory address being written to.
 * @param size The size of the write access (1, 2, or 4 bytes).
 */
void debugger_check_write_watchpoint(Debugger* dbg, Cpu* cpu, uint32_t addr, uint32_t size); // <<< Use 'Cpu*'

// --- Debugger Action ---

/**
 * @brief Handles the debugger state when a break condition occurs (breakpoint, watchpoint, manual pause).
 * This function pauses execution and provides debug information.
 * Current implementation prints info and sets the 'paused' flag. Needs integration
 * with the main loop to actually halt execution flow.
 * @param dbg Pointer to the Debugger instance.
 * @param cpu Pointer to the Cpu instance containing the current state (using 'Cpu*' typedef).
 * @param reason A string describing why the break occurred.
 */
void debugger_handle_break(Debugger* dbg, Cpu* cpu, const char* reason); // <<< Use 'Cpu*'

#endif // DEBUGGER_H