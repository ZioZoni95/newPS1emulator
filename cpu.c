#include "cpu.h"       // Include the header for our CPU definitions
#include <stdio.h>      // Standard I/O for printing messages (e.g., printf)
#include <stdlib.h>     // Standard library, used here for exit() on errors

// --- Memory Access via Interconnect ---
// Fetches a 32-bit word (like an instruction) from the specified memory address.
// This function delegates the actual memory access to the interconnect module.
uint32_t cpu_load32(Cpu* cpu, uint32_t address) {
    // Call the interconnect's function to handle the load based on the address map.
    return interconnect_load32(cpu->inter, address);
}

// --- GPR Accessors ---
// Reads the value from a specified General Purpose Register (GPR).
// 'index' is the register number (0-31).
uint32_t cpu_reg(Cpu* cpu, uint32_t index) {
    // Basic bounds check (should not happen if instruction decoding is correct).
    if (index >= 32) {
        fprintf(stderr, "GPR read index out of bounds: %u\n", index);
        return 0; // Or handle error more formally (e.g., exception)
    }
    // Return the value stored in the register array.
    return cpu->regs[index];
}

// Writes a 'value' to the specified General Purpose Register (GPR).
// 'index' is the register number (0-31).
// Based on Guide Section 2.13 set_reg example [cite: 223]
void cpu_set_reg(Cpu* cpu, uint32_t index, uint32_t value) {
    // Basic bounds check.
    if (index >= 32) {
        fprintf(stderr, "GPR write index out of bounds: %u\n", index);
        return; // Or handle error
    }
    // Special case: Register $zero (index 0) is hardwired to zero and cannot be written to. [cite: 204]
    if (index != 0) {
        // Write the value to the specified register.
        cpu->regs[index] = value;
    }
    // Optional safety check from guide: ensure $0 is always 0. [cite: 223]
    // This prevents needing the check in every instruction implementation.
    cpu->regs[0] = 0;
}

// --- Instruction Implementations ---

// LUI: Load Upper Immediate (Opcode 0b001111 = 0xF)
// Loads the 16-bit immediate value into the upper 16 bits of the target register (rt).
// The lower 16 bits of the target register are set to 0.
// Based on Guide Section 2.14 [cite: 227, 228]
void op_lui(Cpu* cpu, uint32_t instruction) {
    // Extract the 16-bit immediate value from the instruction. [cite: 192]
    uint32_t imm = instr_imm(instruction);
    // Extract the target register index (rt) from the instruction. [cite: 192]
    uint32_t rt = instr_t(instruction);

    // Shift the immediate value left by 16 bits to place it in the upper halfword.
    // The lower 16 bits become zero automatically due to the shift. [cite: 228, 229]
    uint32_t value = imm << 16;

    // Write the resulting value to the target register using the accessor function. [cite: 228]
    cpu_set_reg(cpu, rt, value);

    // Debug print to show the operation performed.
    printf("Executed LUI: R%u = 0x%08x\n", rt, value);
}

//ORI INSTR
// ORI: Bitwise OR Immediate (Opcode 0b001101 = 0xD)
// Performs a bitwise OR between register 'rs' and the zero-extended immediate value,
// storing the result in register 'rt'.
// Based on Section 2.15 [cite: 236]
void op_ori(Cpu* cpu, uint32_t instruction) {
    // Extract the 16-bit immediate value (zero-extended).
    uint32_t imm = instr_imm(instruction);
    // Extract the target register index (rt).
    uint32_t rt = instr_t(instruction);
    // Extract the source register index (rs).
    uint32_t rs = instr_s(instruction);

    // Read the value from the source register.
    uint32_t rs_value = cpu_reg(cpu, rs);

    // Perform the bitwise OR operation.
    uint32_t result = rs_value | imm;

    // Write the result back to the target register.
    cpu_set_reg(cpu, rt, result);

    // Debug print.
    printf("Executed ORI: R%u = R%u | 0x%04x => 0x%08x\n", rt, rs, imm, result);
}


// Add implementations for ORI, SW, SLL, ADDIU etc. here...


// --- Instruction Decoding & Execution ---
// This function takes the fetched instruction and determines which operation to perform.
// Based on Guide Section 2.10 [cite: 171]
void decode_and_execute(Cpu* cpu, uint32_t instruction) {
    // Extract the primary 6-bit opcode (bits 31-26) using the helper function. [cite: 177, 187]
    uint32_t opcode = instr_function(instruction);

    // Debug print showing the instruction being decoded and its primary opcode.
    printf("Decoding instruction: 0x%08x (Opcode: 0x%02x)\n", instruction, opcode);

    // Use a switch statement to handle different primary opcodes.
    switch(opcode) {
        // --- I-Type Instructions (and others identified by primary opcode) ---
        case 0b001111: // Opcode 0xF: LUI (Load Upper Immediate) [cite: 178]
            op_lui(cpu, instruction); // Call the LUI implementation function.
            break;
        case 0b001101: // 0xD: ORI <-- ADD THIS CASE
            op_ori(cpu, instruction);
            break;    

        // --- R-Type Instructions (identified by primary opcode 0x00) ---
        case 0b000000: // Opcode 0x00: These instructions use the subfunction field (bits 5-0). [cite: 287]
            { // Inner scope for subfunction variable
                // Extract the 6-bit subfunction code. [cite: 287]
                uint32_t subfunc = instr_subfunction(instruction);
                // Inner switch statement based on the subfunction code.
                switch(subfunc) {
                    // case 0b000000: // Subfunction 0x00: SLL (Shift Left Logical) [cite: 288]
                    //    op_sll(cpu, instruction); // Call SLL implementation (to be added)
                    //    break;
                    // Add cases for JR (0x8), OR (0x25), ADDU (0x21), etc. here
                    default: // Handle any unknown/unimplemented R-type instructions.
                        fprintf(stderr, "Unhandled R-type instruction: Subfunction 0x%02x\n", subfunc);
                        exit(1); // Stop emulation on unhandled instruction for debugging.
                }
            }
            break; // End of case 0b000000

        // Add cases for ORI (0xD), ADDIU (0x9), J (0x2), SW (0x2B), LW (0x23), COP0 (0x10) etc. here

        default: // Handle any unknown/unimplemented primary opcodes.
            fprintf(stderr, "Unhandled instruction: 0x%08x (Opcode 0x%02x)\n", instruction, opcode);
            exit(1); // Stop emulation for debugging.
    }
}


// --- CPU Initialization ---
// Sets up the initial state of the CPU.
// Based on Guide Section 2.13 Implementing the general purpose registers [cite: 218]
void cpu_init(Cpu* cpu, Interconnect* inter) {
    // Set the Program Counter to the BIOS entry point. [cite: 79, 81]
    cpu->pc = 0xbfc00000;
    // Store the pointer to the interconnect module for memory access. [cite: 165]
    cpu->inter = inter;

    // Initialize all General Purpose Registers (GPRs).
    for (int i = 0; i < 32; ++i) {
        // Use a recognizable "garbage" value for uninitialized registers (Guide §2.13) [cite: 219]
        cpu->regs[i] = 0xdeadbeef;
    }
    // GPR $zero (index 0) is hardwired to the value 0. [cite: 204, 221]
    cpu->regs[0] = 0;

    // Print confirmation message.
    printf("CPU Initialized. PC = 0x%08x, GPRs initialized.\n", cpu->pc);
}

// --- Basic Execution Cycle ---
// Simulates one cycle of the CPU: fetch, increment PC, decode & execute.
// Based on Guide Section 2.4 CPU cycle description [cite: 63]
// NOTE: This simple version will be updated later to handle branch delay slots correctly (Guide §2.23, §2.71)
void cpu_run_next_instruction(Cpu* cpu) {
    // Store the address of the instruction we are about to fetch.
    uint32_t current_pc = cpu->pc;

    // 1. Fetch the 32-bit instruction from memory at the current PC address. [cite: 63]
    //    This uses the interconnect, which currently accesses the BIOS.
    uint32_t instruction = cpu_load32(cpu, current_pc);

    // 2. Increment the Program Counter to point to the *next* instruction. [cite: 63]
    //    PlayStation instructions are always 4 bytes long. [cite: 60]
    //    Standard C unsigned integer addition handles wrapping correctly (e.g., 0xfffffffc + 4 = 0x00000000). [cite: 66]
    cpu->pc = current_pc + 4;

    // 3. Decode the fetched instruction and perform the corresponding action. [cite: 64]
    decode_and_execute(cpu, instruction);

    // Safety net: Ensure R0 ($zero) remains 0 after any instruction execution.
    // This is usually handled by cpu_set_reg, but added here for extra safety.
    cpu->regs[0] = 0;
}