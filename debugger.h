/**
 * debugger.h
 * Header file for the simple debugger component.
 */
#ifndef DEBUGGER_H
#define DEBUGGER_H

#include <stdint.h>
#include <stdbool.h>

// --- Include Cpu Definition ---
/**
 * @brief Include cpu.h directly here.
 * This makes the 'Cpu' typedef (defined in cpu.h) visible
 * for use in the function prototypes below. This ensures consistency
 * between the declarations here and the definitions in debugger.c.
 */
#include "cpu.h" // <<< INCLUDE cpu.h HERE

// --- Configuration ---
#define MAX_BREAKPOINTS 16
#define MAX_WATCHPOINTS 16

// --- Debugger State Structure ---
typedef struct {
    uint32_t breakpoints[MAX_BREAKPOINTS];
    uint32_t breakpoint_count;
    uint32_t read_watchpoints[MAX_WATCHPOINTS];
    uint32_t read_watchpoint_count;
    uint32_t write_watchpoints[MAX_WATCHPOINTS];
    uint32_t write_watchpoint_count;
    bool paused;
} Debugger;

// --- Function Prototypes ---
// Prototypes now use 'Cpu*' (the typedef) because cpu.h was included above.

void debugger_init(Debugger* dbg);

// Breakpoint Management
bool debugger_add_breakpoint(Debugger* dbg, uint32_t addr);
bool debugger_remove_breakpoint(Debugger* dbg, uint32_t addr);

// Watchpoint Management
bool debugger_add_read_watchpoint(Debugger* dbg, uint32_t addr);
bool debugger_remove_read_watchpoint(Debugger* dbg, uint32_t addr);
bool debugger_add_write_watchpoint(Debugger* dbg, uint32_t addr);
bool debugger_remove_write_watchpoint(Debugger* dbg, uint32_t addr);

// Debugger Hooks (Called by CPU)
void debugger_check_breakpoint(Debugger* dbg, Cpu* cpu); // <<< USE 'Cpu*' TYPEDEF
void debugger_check_read_watchpoint(Debugger* dbg, Cpu* cpu, uint32_t addr, uint32_t size); // <<< USE 'Cpu*' TYPEDEF
void debugger_check_write_watchpoint(Debugger* dbg, Cpu* cpu, uint32_t addr, uint32_t size); // <<< USE 'Cpu*' TYPEDEF

// Debugger Action
void debugger_handle_break(Debugger* dbg, Cpu* cpu, const char* reason); // <<< USE 'Cpu*' TYPEDEF

#endif // DEBUGGER_H