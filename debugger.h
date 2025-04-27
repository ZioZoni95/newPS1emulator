// debugger.h
#ifndef DEBUGGER_H
#define DEBUGGER_H

#include <stdint.h>
#include <stdbool.h>
// #include "cpu.h" // <<< REMOVED to break circular dependency

// --- Forward Declaration ---
/**
 * @brief Forward declaration of the Cpu struct.
 * This tells the compiler that a struct named 'Cpu' exists, allowing us to use
 * pointers like 'struct Cpu*' in function prototypes below without needing the full
 * definition of the Cpu struct in this header file. The full definition is
 * needed only in the .c file where we access Cpu members.
 */
struct Cpu; // <<< Use 'struct Cpu;' only, no typedef here

// --- Configuration ---
// Simple fixed-size arrays for now. Could use dynamic allocation later.
#define MAX_BREAKPOINTS 16 ///< Maximum number of active breakpoints.
#define MAX_WATCHPOINTS 16 ///< Maximum number of active read/write watchpoints.

// --- Debugger State Structure ---
/**
 * @brief Holds the state and data for the debugger.
 */
typedef struct {
    // Breakpoints (triggered by PC value)
    uint32_t breakpoints[MAX_BREAKPOINTS];      ///< Array of breakpoint addresses.
    uint32_t breakpoint_count;                  ///< Number of active breakpoints.

    // Read Watchpoints (triggered by memory load address)
    uint32_t read_watchpoints[MAX_WATCHPOINTS]; ///< Array of read watchpoint addresses.
    uint32_t read_watchpoint_count;             ///< Number of active read watchpoints.

    // Write Watchpoints (triggered by memory store address)
    uint32_t write_watchpoints[MAX_WATCHPOINTS];///< Array of write watchpoint addresses.
    uint32_t write_watchpoint_count;            ///< Number of active write watchpoints.

    // Add other state if needed (e.g., step mode, running status)
    bool paused;                                ///< Flag indicating if execution should pause for debugging.

} Debugger;

// --- Function Prototypes ---

/**
 * @brief Initializes the debugger state.
 * Sets counts to 0 and clears the paused flag.
 * @param dbg Pointer to the Debugger struct to initialize.
 */
void debugger_init(Debugger* dbg);

/**
 * @brief Adds a breakpoint at the specified program counter address.
 * Checks for duplicates and available space.
 * @param dbg Pointer to the Debugger instance.
 * @param addr The memory address (PC value) for the breakpoint.
 * @return True if added successfully or already exists, false if the list is full.
 */
bool debugger_add_breakpoint(Debugger* dbg, uint32_t addr);

/**
 * @brief Removes a breakpoint at the specified address.
 * @param dbg Pointer to the Debugger instance.
 * @param addr The memory address of the breakpoint to remove.
 * @return True if removed successfully, false if the breakpoint was not found.
 */
bool debugger_remove_breakpoint(Debugger* dbg, uint32_t addr);

/**
 * @brief Adds a read watchpoint at the specified memory address.
 * Checks for duplicates and available space.
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
 * @brief Adds a write watchpoint at the specified memory address.
 * Checks for duplicates and available space.
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

/**
 * @brief Checks if the current Program Counter hits an active breakpoint.
 * This should be called by the CPU *before* executing the instruction at the current PC.
 * If a breakpoint is hit, it calls debugger_handle_break().
 * @param dbg Pointer to the Debugger instance.
 * @param cpu Pointer to the Cpu instance (using forward declared type).
 */
void debugger_check_breakpoint(Debugger* dbg, struct Cpu* cpu); // Use 'struct Cpu*'

/**
 * @brief Checks if a memory read access hits an active read watchpoint.
 * This should be called by the CPU *before* performing a memory load operation.
 * If a watchpoint is hit, it calls debugger_handle_break().
 * @param dbg Pointer to the Debugger instance.
 * @param cpu Pointer to the Cpu instance.
 * @param addr The memory address being read from.
 * @param size The size of the read access (1, 2, or 4 bytes).
 */
void debugger_check_read_watchpoint(Debugger* dbg, struct Cpu* cpu, uint32_t addr, uint32_t size); // Use 'struct Cpu*'

/**
 * @brief Checks if a memory write access hits an active write watchpoint.
 * This should be called by the CPU *before* performing a memory store operation.
 * If a watchpoint is hit, it calls debugger_handle_break().
 * @param dbg Pointer to the Debugger instance.
 * @param cpu Pointer to the Cpu instance.
 * @param addr The memory address being written to.
 * @param size The size of the write access (1, 2, or 4 bytes).
 */
void debugger_check_write_watchpoint(Debugger* dbg, struct Cpu* cpu, uint32_t addr, uint32_t size); // Use 'struct Cpu*'


/**
 * @brief Handles the debugger state when a break condition occurs.
 * (e.g., breakpoint hit, watchpoint hit, manual pause request).
 * This function is intended to pause execution and provide debug information.
 * Current implementation is a placeholder: prints info and sets the 'paused' flag.
 * @param dbg Pointer to the Debugger instance.
 * @param cpu Pointer to the Cpu instance containing the current state.
 * @param reason A string describing why the break occurred (e.g., "Breakpoint hit").
 */
void debugger_handle_break(Debugger* dbg, struct Cpu* cpu, const char* reason); // Use 'struct Cpu*'


#endif // DEBUGGER_H