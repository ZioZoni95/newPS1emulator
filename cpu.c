#include "cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h> // For memcpy
#include <stdint.h> // Make sure this is included at the top of cpu.c

// --- Memory Access via Interconnect ---
// Fetches a 32-bit word (like an instruction) from the specified memory address.
// This function delegates the actual memory access to the interconnect module.
uint32_t cpu_load32(Cpu* cpu, uint32_t address) {
    // Call the interconnect's function to handle the load based on the address map.
    return interconnect_load32(cpu->inter, address);
}

// NEW: Delegates 16-bit memory load to the interconnect.
uint16_t cpu_load16(Cpu* cpu, uint32_t address) {
    return interconnect_load16(cpu->inter, address); // Assumes interconnect_load16 exists
}

// NEW: Delegates memory store to the interconnect.
void cpu_store32(Cpu* cpu, uint32_t address, uint32_t value) {
    interconnect_store32(cpu->inter, address, value);
}

// NEW: Delegates 16-bit memory store to the interconnect.
void cpu_store16(Cpu* cpu, uint32_t address, uint16_t value) {
    interconnect_store16(cpu->inter, address, value);
}

// NEW: Delegates 8-bit memory store to the interconnect.
void cpu_store8(Cpu* cpu, uint32_t address, uint8_t value) {
    interconnect_store8(cpu->inter, address, value);
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
void cpu_set_reg(Cpu* cpu, RegisterIndex index, uint32_t value) {
    if (index >= 32) {
        fprintf(stderr, "GPR write index out of bounds: %u\n", index);
        return;
    }
    // Write to the output register set for the current instruction.
    if (index != 0) { // Ensure $zero is not written directly
        cpu->out_regs[index] = value;
    }
    // Safety: Ensure $zero in the output set is always 0.
    cpu->out_regs[REG_ZERO] = 0;
}


// --- Branch Helper Function ---
// Calculates the target address for branch instructions and updates next_pc.
// The offset is sign-extended and relative to the delay slot instruction's PC.
// Based on Section 2.29
void cpu_branch(Cpu* cpu, uint32_t offset_se) {
    // The immediate offset is shifted left by 2 because instructions are word-aligned.
    uint32_t branch_offset = offset_se << 2;

    // The offset is relative to the instruction *in the delay slot*.
    // At this point in decode_and_execute, cpu->pc already points to the delay slot instruction.
    uint32_t delay_slot_pc = cpu->pc;

    // Calculate the target address. Standard C unsigned arithmetic handles wrapping.
    uint32_t target_address = delay_slot_pc + branch_offset;

    // Update the *next* PC (the one after the delay slot executes).
    cpu->next_pc = target_address;

    // Note: The guide's Rust code compensates for a PC+4 happening *after* execute.
    // Our C code updates next_pc *before* execute, so the guide's wrapping_sub(4)
    // compensation might not be directly needed here if our run_next_instruction logic is correct.
    // We'll verify this logic during testing. If branches seem off by one instruction,
    // we might need `cpu->next_pc = target_address - 4;` here. Let's stick to the
    // direct calculation first.
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


// SLL: Shift Left Logical (Opcode 0x00, Subfunction 0x00)
// Shifts register 'rt' left by 'shamt' bits, storing the result in 'rd'.
// Zeroes are shifted in from the right.
// Based on Section 2.19
void op_sll(Cpu* cpu, uint32_t instruction) {
    // Extract the shift amount (5 bits).
    uint32_t shamt = instr_shift(instruction);
    // Extract the source register index (rt) - the value to shift.
    uint32_t rt = instr_t(instruction);
    // Extract the destination register index (rd).
    uint32_t rd = instr_d(instruction);

    // Read the value to be shifted from register rt.
    uint32_t value_to_shift = cpu_reg(cpu, rt);

    // Perform the logical left shift.
    uint32_t result = value_to_shift << shamt;

    // Write the result to the destination register rd.
    cpu_set_reg(cpu, rd, result);

    // Debug print - Note: If rd, rt, and shamt are all 0, this is a NOP.
    if (rd == 0 && rt == 0 && shamt == 0) {
         printf("Executed SLL: NOP\n");
    } else {
        printf("Executed SLL: R%u = R%u << %u => 0x%08x\n", rd, rt, shamt, result);
    }
}

void op_addiu(Cpu* cpu, uint32_t instruction) {
    // Extract the 16-bit immediate and sign-extend it to 32 bits. [cite: 301]
    uint32_t imm_se = instr_imm_se(instruction);
    // Extract the target register index (rt) - this is the destination.
    uint32_t rt = instr_t(instruction);
    // Extract the source register index (rs).
    uint32_t rs = instr_s(instruction);

    // Read the value from the source register rs.
    uint32_t rs_value = cpu_reg(cpu, rs);

    // Perform the addition. Standard C unsigned arithmetic provides wrapping behavior. [cite: 301]
    uint32_t result = rs_value + imm_se;

    // Write the result back to the target register rt.
    cpu_set_reg(cpu, rt, result);

    // Debug print.
    printf("Executed ADDIU: R%u = R%u + %d => 0x%08x\n", rt, rs, (int32_t)imm_se, result);
}

// J: Jump (Opcode 0b000010 = 0x2)
// Unconditionally jumps after the delay slot.
// Updates next_pc, not pc directly.
// Based on Section 2.22 and corrected for delay slot (§2.23).
void op_j(Cpu* cpu, uint32_t instruction) {
    uint32_t target_imm = instr_imm_jump(instruction);

    // Calculate the target address:
    // Upper 4 bits come from the PC of the instruction *in the delay slot*.
    // In our new `run_next_instruction`, `cpu->pc` already holds the address
    // of the delay slot instruction when `op_j` is called.
    uint32_t target_address = (cpu->pc & 0xF0000000) | (target_imm << 2);

    // Set the NEXT program counter (the one AFTER the delay slot).
    cpu->next_pc = target_address;

    // ---> SET BRANCH FLAG <---
    cpu->branch_taken = true;

    printf("Executed J: Branch delay slot. Next PC set to 0x%08x\n", target_address);
}

// OR: Bitwise OR (Opcode 0x00, Subfunction 0x25)
// Performs a bitwise OR between registers 'rs' and 'rt'. Stores result in 'rd'.
// Based on Section 2.24
void op_or(Cpu* cpu, uint32_t instruction) {
    // Extract the destination register index (rd).
    uint32_t rd = instr_d(instruction);
    // Extract the first source register index (rs).
    uint32_t rs = instr_s(instruction);
    // Extract the second source register index (rt).
    uint32_t rt = instr_t(instruction);

    // Read the values from the source registers.
    uint32_t rs_value = cpu_reg(cpu, rs);
    uint32_t rt_value = cpu_reg(cpu, rt);

    // Perform the bitwise OR operation.
    uint32_t result = rs_value | rt_value;

    // Write the result back to the destination register rd.
    cpu_set_reg(cpu, rd, result);

    // Debug print. (Example from guide: or $1, $zero, $zero => R1 = R0 | R0 => 0)
    printf("Executed OR: R%u = R%u | R%u => 0x%08x\n", rd, rs, rt, result);
}

// SW: Store Word (Opcode 0b101011 = 0x2B)
// Based on Section 2.16, 2.17, 2.18. UPDATE for Cache Isolate (§2.28)
void op_sw(Cpu* cpu, uint32_t instruction) {
    // Check cache isolation bit in Status Register (SR) before proceeding
    // SR Bit 16: IsC - Isolate Cache. If set, memory accesses target cache directly.
    // For now, we simulate this by ignoring the write entirely. [cite: 390]
    if ((cpu->sr & 0x10000) != 0) {
        printf("Executed SW: Ignored (Cache Isolated, SR=0x%08x)\n", cpu->sr);
        return; // Skip the memory store
    }

    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction); // Value to store is in rt
    uint32_t rs = instr_s(instruction); // Base address is in rs
    uint32_t base_addr = cpu_reg(cpu, rs);
    uint32_t value_to_store = cpu_reg(cpu, rt);
    uint32_t address = base_addr + offset;
    cpu_store32(cpu, address, value_to_store); // Calls interconnect
    printf("Executed SW: M[R%u + %d (0x%08x)] = R%u (0x%08x) @ address 0x%08x\n",
           rs, (int16_t)offset, offset, rt, value_to_store, address);
}

// MTC0: Move To Coprocessor 0 (Opcode 0x10, Specific MTC0 op 0b00100 in rs field)
// Moves value from CPU register 'rt' to Coprocessor 0 register 'rd'.
// Based on Section 2.28 [cite: 382, 387]
void op_mtc0(Cpu* cpu, uint32_t instruction) {
    // CPU source register index is in 'rt' field
    uint32_t cpu_r = instr_t(instruction);
    // COP0 destination register index is in 'rd' field
    uint32_t cop_r = instr_d(instruction);

    // Get the value from the CPU source register.
    uint32_t value = cpu_reg(cpu, cpu_r);

    // Write to the specified COP0 register.
    switch (cop_r) {
        case 12: // Register 12: SR (Status Register)
            cpu->sr = value;
            printf("Executed MTC0: SR = R%u (0x%08x)\n", cpu_r, value);
            break;
        // Add cases for other COP0 registers like CAUSE (13), EPC (14), BDA, BPC etc. later
        // Guide §2.35 suggests ignoring writes of 0 to breakpoint registers initially.
        case 3: // BPC
        case 5: // BDA
        case 6: // ??? (Seems unused)
        case 7: // DCIC
        case 9: // BDAM
        case 11: // BPCM
             if (value == 0) {
                 printf("Executed MTC0: COP0 Reg %u = R%u (0x%08x) - Ignored Zero Write\n", cop_r, cpu_r, value);
             } else {
                 fprintf(stderr, "Warning: Write to unhandled COP0 Reg %u with non-zero value 0x%08x\n", cop_r, value);
                 // For now, we just warn. A real implementation might store these.
             }
             break;
        case 13: // CAUSE - Read-only except for specific bits, handle later
             fprintf(stderr, "Warning: Write to COP0 CAUSE Register ignored (Reg 13 = 0x%08x)\n", value);
             break;
        default:
            fprintf(stderr, "Error: Write to unhandled COP0 Register %u\n", cop_r);
            // We could panic here, or just ignore. Let's ignore for now.
            // exit(1);
             printf("Executed MTC0: COP0 Reg %u = R%u (0x%08x) - Unhandled\n", cop_r, cpu_r, value);
            break;
    }
}

// Dispatcher for COP0 instructions (Opcode 0x10)
// Based on Section 2.28 [cite: 381]
void op_cop0(Cpu* cpu, uint32_t instruction) {
    // COP0 uses the 'rs' field to differentiate between operations like MTC0, MFC0, RFE etc.
    // The guide calls this `cop_opcode()`. Let's use `instr_s()`.
    uint32_t cop_opcode = instr_s(instruction);

    switch (cop_opcode) {
        case 0b00100: // Value 4: MTC0 (Move To Coprocessor 0)
            op_mtc0(cpu, instruction);
            break;
        case 0b00000: // Value 0: MFC0 (Move From Coprocessor 0) <-- Add this case
            op_mfc0(cpu, instruction);
            break;
        case 0b10000: // Value 16: RFE (Return From Exception) [cite: 911]
            op_rfe(cpu, instruction); // You already have the op_rfe implementation
            break;        
        // Add cases for MFC0 (0b00000), RFE (sub-case of 0b10000) later
        default:
             fprintf(stderr, "Unhandled COP0 instruction: PC=0x%08x, Inst=0x%08x, CopOpcode=0x%02x\n",
                    cpu->pc, instruction, cop_opcode);
            exit(1);
    }
}

// BNE: Branch if Not Equal (Opcode 0b000101 = 0x5)
// Compares registers 'rs' and 'rt'. If rs != rt, branches to PC + 4 + (offset * 4).
// Based on Section 2.29
void op_bne(Cpu* cpu, uint32_t instruction) {
    // Extract the 16-bit immediate and sign-extend it.
    uint32_t imm_se = instr_imm_se(instruction);
    // Extract the source register indices.
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);

    // Read the values from the source registers.
    uint32_t rs_value = cpu_reg(cpu, rs);
    uint32_t rt_value = cpu_reg(cpu, rt);

    // Compare the register values.
    if (rs_value != rt_value) {
        // If not equal, call the branch helper to update next_pc.
        cpu_branch(cpu, imm_se);
        
        // ---> SET BRANCH FLAG <---
        cpu->branch_taken = true;
        // ------------------------

        printf("Executed BNE: R%u (0x%x) != R%u (0x%x), Branch taken. Next PC will be 0x%08x\n",
               rs, rs_value, rt, rt_value, cpu->next_pc);
    } else {
        // If equal, do nothing (next_pc remains pc+4 from run_next_instruction).
        printf("Executed BNE: R%u (0x%x) == R%u (0x%x), Branch not taken.\n",
               rs, rs_value, rt, rt_value);
    }
}

// BGTZ: Branch if Greater Than Zero (Opcode 0b000111 = 0x7)
// Compares register 'rs' (signed) against 0. If rs > 0, branches.
// Based on Section 2.54
void op_bgtz(Cpu* cpu, uint32_t instruction) {
    // Extract the 16-bit immediate and sign-extend it.
    uint32_t imm_se = instr_imm_se(instruction);
    // Extract the source register index (rs) to compare against zero.
    uint32_t rs = instr_s(instruction);
    // Note: 'rt' field is not used for the comparison in BGTZ.

    // Read the value from the source register and treat as signed.
    int32_t rs_value = (int32_t)cpu_reg(cpu, rs);

    // Perform the signed comparison against zero.
    if (rs_value > 0) {
        // If greater than zero, call the branch helper to update next_pc.
        cpu_branch(cpu, imm_se);

        // ---> SET BRANCH FLAG <---
        cpu->branch_taken = true;
        // ------------------------

        printf("Executed BGTZ: R%u (%d) > 0, Branch taken. Next PC will be 0x%08x\n",
               rs, rs_value, cpu->next_pc);
    } else {
        // If zero or less, do nothing (next_pc remains pc+4).
        printf("Executed BGTZ: R%u (%d) <= 0, Branch not taken.\n",
               rs, rs_value);
    }
}

// ADDI: Add Immediate (Signed, with Overflow Check) (Opcode 0b001000 = 0x8)
// Adds register 'rs' and the sign-extended immediate value. Stores result in 'rt'.
// Triggers an exception on signed overflow.
// Based on Section 2.30
void op_addi(Cpu* cpu, uint32_t instruction) {
    // Extract the 16-bit immediate and sign-extend it to 32 bits.
    int32_t imm_se = (int32_t)instr_imm_se(instruction); // Treat immediate as signed
    // Extract the target register index (rt) - this is the destination.
    uint32_t rt = instr_t(instruction);
    // Extract the source register index (rs).
    uint32_t rs = instr_s(instruction);

    // Read the value from the source register rs, treat as signed.
    int32_t rs_value = (int32_t)cpu_reg(cpu, rs);

    // Perform signed addition and check for overflow.
    // Method 1: Using GCC/Clang built-in (preferred if available)
    int32_t result;
    if (__builtin_add_overflow(rs_value, imm_se, &result)) {
        // Overflow occurred!
        fprintf(stderr, "ADDI Signed Overflow: %d + %d\n", rs_value, imm_se);
        // TODO: Trigger Integer Overflow Exception (Guide §2.77)
        // cpu_exception(cpu, EXCEPTION_OVERFLOW);
        exit(1); // Stop emulator for now (guide's initial approach)
    } else {
        // No overflow, write the result (cast back to uint32_t for storage).
        cpu_set_reg(cpu, rt, (uint32_t)result);
        printf("Executed ADDI: R%u = R%u + %d => 0x%08x\n", rt, rs, imm_se, (uint32_t)result);
    }

    /*
    // Method 2: Manual check using wider type (less efficient but portable)
    int64_t wide_result = (int64_t)rs_value + (int64_t)imm_se;
    if (wide_result > INT32_MAX || wide_result < INT32_MIN) {
        // Overflow occurred!
        fprintf(stderr, "ADDI Signed Overflow: %d + %d\n", rs_value, imm_se);
        // TODO: Trigger Integer Overflow Exception (Guide §2.77)
        // cpu_exception(cpu, EXCEPTION_OVERFLOW);
        exit(1); // Stop emulator for now
    } else {
        // No overflow, write the result.
        cpu_set_reg(cpu, rt, (uint32_t)wide_result);
         printf("Executed ADDI: R%u = R%u + %d => 0x%08x\n", rt, rs, imm_se, (uint32_t)wide_result);
    }
    */
}

// LW: Load Word (Opcode 0b100011 = 0x23)
// Loads a 32-bit word from memory into register 'rt'.
// Address is calculated as register 'rs' + sign-extended 16-bit immediate offset.
// The actual register write is delayed by one instruction (Load Delay Slot).
// Based on Section 2.31, 2.32, 2.33
void op_lw(Cpu* cpu, uint32_t instruction) {
    // Check cache isolation bit in Status Register (SR).
    // Loads should also be ignored if cache is isolated? Guide doesn't explicitly state for LW,
    // but §2.28 says "all the following read and write target directly the cache". Let's ignore for now.
    if ((cpu->sr & 0x10000) != 0) {
        printf("Executed LW: Ignored (Cache Isolated, SR=0x%08x)\n", cpu->sr);
        // We also need to clear any pending load from previous instruction?
        // For now, let's just not schedule a new load. Existing pending load will be cleared in run_next.
        return;
    }

    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction); // Target register index
    uint32_t rs = instr_s(instruction); // Base address register index

    // Read base address from rs (uses 'regs', the state *before* this instruction)
    uint32_t base_addr = cpu_reg(cpu, rs);

    // Calculate effective memory address.
    uint32_t address = base_addr + offset;

    // Check alignment
    if (address % 4 != 0) {
        fprintf(stderr, "LW Alignment Error: Address 0x%08x\n", address);
        // TODO: Trigger Address Error Load exception (Guide §2.78)
        exit(1); // Stop for now
    } else {
        // Load value from memory via interconnect
        uint32_t value_loaded = cpu_load32(cpu, address);

        // Schedule the load for the *next* instruction cycle.
        // Store the target register index and the loaded value.
        cpu->load_reg_idx = rt;
        cpu->load_value = value_loaded;

        printf("Executed LW: R%u <- M[R%u + %d (0x%08x)] = 0x%08x @ 0x%08x (Delayed)\n",
               rt, rs, (int16_t)offset, offset, value_loaded, address);
    }
}

// SLTU: Set on Less Than Unsigned (Opcode 0x00, Subfunction 0x2B)
// Compares registers 'rs' and 'rt' as unsigned integers.
// If rs < rt, sets register 'rd' to 1, otherwise sets it to 0.
// Based on Section 2.36
void op_sltu(Cpu* cpu, uint32_t instruction) {
    // Extract the destination register index (rd).
    uint32_t rd = instr_d(instruction);
    // Extract the first source register index (rs).
    uint32_t rs = instr_s(instruction);
    // Extract the second source register index (rt).
    uint32_t rt = instr_t(instruction);

    // Read the values from the source registers.
    // IMPORTANT: Treat them as unsigned for the comparison.
    uint32_t rs_value = cpu_reg(cpu, rs);
    uint32_t rt_value = cpu_reg(cpu, rt);

    // Perform the unsigned comparison.
    uint32_t result = (rs_value < rt_value) ? 1 : 0;

    // Write the result (0 or 1) back to the destination register rd.
    cpu_set_reg(cpu, rd, result);

    // Debug print.
    printf("Executed SLTU: R%u = (R%u < R%u) ? 1 : 0 => %u\n", rd, rs, rt, result);
}

// AND: Bitwise AND (Opcode 0x00, Subfunction 0x24)
// Performs a bitwise AND between registers 'rs' and 'rt'. Stores result in 'rd'.
// Based on Section 2.51
void op_and(Cpu* cpu, uint32_t instruction) {
    // Extract register indices
    uint32_t rd = instr_d(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);

    // Read values from source registers
    uint32_t rs_value = cpu_reg(cpu, rs);
    uint32_t rt_value = cpu_reg(cpu, rt);

    // Perform bitwise AND
    uint32_t result = rs_value & rt_value;

    // Write result to destination register
    cpu_set_reg(cpu, rd, result);

    // Debug print
    printf("Executed AND: R%u = R%u & R%u => 0x%08x\n", rd, rs, rt, result);
}

// MFC0: Move From Coprocessor 0 (Opcode 0x10, Specific MFC0 op 0b00000 in rs field)
// Moves value from Coprocessor 0 register 'rd' to CPU register 'rt'.
// Has a load delay slot.
// Based on Section 2.50 [cite: 595]
void op_mfc0(Cpu* cpu, uint32_t instruction) {
    // CPU destination register index is in 'rt' field
    uint32_t cpu_r_dest = instr_t(instruction);
    // COP0 source register index is in 'rd' field
    uint32_t cop_r_src = instr_d(instruction);

    uint32_t value_read;

    // Read from the specified COP0 register.
    switch (cop_r_src) {
        case 12: // Register 12: SR (Status Register)
            value_read = cpu->sr;
            printf("Executed MFC0: R%u <- SR (0x%08x) (Delayed)\n", cpu_r_dest, value_read);
            break;
        case 13: // Register 13: CAUSE Register (Added for exceptions) [cite: 850]
            // value_read = cpu->cause; // Uncomment when cause is added to Cpu struct
            value_read = 0; // Placeholder if cause not yet in struct
            printf("Executed MFC0: R%u <- CAUSE (0x%08x) (Delayed)\n", cpu_r_dest, value_read);
             fprintf(stderr, "Warning: Reading from uninitialized CAUSE register (COP0 Reg 13)\n");
            break;
        case 14: // Register 14: EPC Register (Added for exceptions) [cite: 850]
            // value_read = cpu->epc; // Uncomment when epc is added to Cpu struct
            value_read = 0; // Placeholder if epc not yet in struct
            printf("Executed MFC0: R%u <- EPC (0x%08x) (Delayed)\n", cpu_r_dest, value_read);
             fprintf(stderr, "Warning: Reading from uninitialized EPC register (COP0 Reg 14)\n");
            break;
        // Add cases for other readable COP0 registers if needed later
        default:
            fprintf(stderr, "Error: Read from unhandled COP0 Register %u\n", cop_r_src);
            value_read = 0; // Return 0 for unhandled reads for now
            // exit(1); // Optionally exit on error
            break;
    }

    // Schedule the value to be loaded into the CPU register after the delay slot. [cite: 603]
    cpu->load_reg_idx = cpu_r_dest;
    cpu->load_value = value_read;
}

// ADD: Add (Signed, with Overflow Check) (Opcode 0x00, Subfunction 0x20)
// Adds registers 'rs' and 'rt' as signed integers. Stores result in 'rd'.
// Triggers an exception on signed overflow.
// Based on Section 2.52
void op_add(Cpu* cpu, uint32_t instruction) {
    // Extract register indices
    uint32_t rd = instr_d(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);

    // Read values from source registers, cast to signed
    int32_t rs_value = (int32_t)cpu_reg(cpu, rs);
    int32_t rt_value = (int32_t)cpu_reg(cpu, rt);

    // Perform signed addition and check for overflow
    // Using GCC/Clang built-in for overflow check
    int32_t result;
    if (__builtin_add_overflow(rs_value, rt_value, &result)) {
        // Overflow occurred!
        fprintf(stderr, "ADD Signed Overflow: %d + %d\n", rs_value, rt_value);
        // TODO: Trigger Integer Overflow Exception (Guide §2.77)
        // cpu_exception(cpu, EXCEPTION_OVERFLOW);
        exit(1); // Stop emulator for now
    } else {
        // No overflow, write the result (cast back to uint32_t for storage).
        cpu_set_reg(cpu, rd, (uint32_t)result);
        printf("Executed ADD: R%u = R%u + R%u => 0x%08x\n", rd, rs, rt, (uint32_t)result);
    }

    /* // Manual overflow check alternative
    int64_t wide_result = (int64_t)rs_value + (int64_t)rt_value;
    if (wide_result > INT32_MAX || wide_result < INT32_MIN) {
         fprintf(stderr, "ADD Signed Overflow: %d + %d\n", rs_value, rt_value);
         // TODO: Trigger Integer Overflow Exception
         exit(1);
    } else {
         cpu_set_reg(cpu, rd, (uint32_t)wide_result);
         printf("Executed ADD: R%u = R%u + R%u => 0x%08x\n", rd, rs, rt, (uint32_t)wide_result);
    }
    */
}

// ADDU: Add Unsigned (Opcode 0x00, Subfunction 0x21)
// Adds registers 'rs' and 'rt' as unsigned integers. Stores result in 'rd'.
// Does *not* trap on overflow (result wraps).
// Based on Section 2.37
void op_addu(Cpu* cpu, uint32_t instruction) {
    // Extract the destination register index (rd).
    uint32_t rd = instr_d(instruction);
    // Extract the first source register index (rs).
    uint32_t rs = instr_s(instruction);
    // Extract the second source register index (rt).
    uint32_t rt = instr_t(instruction);

    // Read the values from the source registers.
    uint32_t rs_value = cpu_reg(cpu, rs);
    uint32_t rt_value = cpu_reg(cpu, rt);

    // Perform unsigned addition. Standard C unsigned arithmetic handles wrapping.
    uint32_t result = rs_value + rt_value;

    // Write the result back to the destination register rd.
    cpu_set_reg(cpu, rd, result);

    // Debug print.
    printf("Executed ADDU: R%u = R%u + R%u => 0x%08x\n", rd, rs, rt, result);
}

// SH: Store Halfword (Opcode 0b101001 = 0x29)
// Stores the lower 16 bits from register 'rt' into memory.
// Address is calculated as register 'rs' + sign-extended 16-bit immediate offset.
// Requires 16-bit alignment.
// Based on Section 2.39
void op_sh(Cpu* cpu, uint32_t instruction) {
    // Check cache isolation bit in Status Register (SR).
    if ((cpu->sr & 0x10000) != 0) {
        printf("Executed SH: Ignored (Cache Isolated, SR=0x%08x)\n", cpu->sr);
        return; // Skip the memory store
    }

    // Extract the 16-bit immediate and sign-extend it to 32 bits.
    uint32_t offset = instr_imm_se(instruction);
    // Extract the target register (rt) which holds the value to store.
    uint32_t rt = instr_t(instruction);
    // Extract the base register (rs) which holds the base address.
    uint32_t rs = instr_s(instruction);

    // Read the base address from register rs.
    uint32_t base_addr = cpu_reg(cpu, rs);
    // Read the full 32-bit value from register rt.
    uint32_t value_to_store = cpu_reg(cpu, rt);

    // Calculate the effective memory address.
    uint32_t address = base_addr + offset;

    // Call the CPU's store16 function (which delegates to the interconnect).
    // Alignment checks (multiple of 2) happen in interconnect_store16.
    // We only pass the lower 16 bits of the value.
    cpu_store16(cpu, address, (uint16_t)value_to_store);

    // Debug print.
    printf("Executed SH: M[R%u + %d (0x%08x)] = R%u (lower 16b = 0x%04x) @ address 0x%08x\n",
           rs, (int16_t)offset, offset, rt, (uint16_t)value_to_store, address);
}

// JAL: Jump And Link (Opcode 0b000011 = 0x3)
// Jumps to target address like J, but also stores the return address
// (PC of instruction AFTER delay slot) in register $ra ($31).
// Based on Section 2.41
void op_jal(Cpu* cpu, uint32_t instruction) {
    // 1. Link: Save the return address in $ra ($31)
    // The return address is the address of the instruction AFTER the delay slot.
    // Since cpu->pc currently points to the delay slot instruction,
    // the return address is cpu->pc + 4.
    uint32_t return_addr = cpu->pc + 4;
    cpu_set_reg(cpu, 31, return_addr); // $ra is register 31

    // 2. Jump: Calculate target address and update next_pc (same logic as J)
    uint32_t target_imm = instr_imm_jump(instruction);
    // Upper 4 bits from delay slot PC (which is current cpu->pc)
    uint32_t target_address = (cpu->pc & 0xF0000000) | (target_imm << 2);
    cpu->next_pc = target_address;
    
    // ---> SET BRANCH FLAG <---
    cpu->branch_taken = true;
    // ------------------------
    // Debug print.
    printf("Executed JAL: R31 = 0x%08x, Branch delay slot. Next PC set to 0x%08x\n",
           return_addr, target_address);
}

// ANDI: Bitwise AND Immediate (Opcode 0b001100 = 0xC)
// Performs a bitwise AND between register 'rs' and the zero-extended immediate value.
// Stores the result in register 'rt'.
// Based on Section 2.42
void op_andi(Cpu* cpu, uint32_t instruction) {
    // Extract the 16-bit immediate value (zero-extended by the helper).
    uint32_t imm_zero_ext = instr_imm(instruction);
    // Extract the target register index (rt) - this is the destination.
    uint32_t rt = instr_t(instruction);
    // Extract the source register index (rs).
    uint32_t rs = instr_s(instruction);

    // Read the value from the source register rs.
    uint32_t rs_value = cpu_reg(cpu, rs);

    // Perform the bitwise AND operation.
    uint32_t result = rs_value & imm_zero_ext;

    // Write the result back to the target register rt.
    cpu_set_reg(cpu, rt, result);

    // Debug print. (Example from guide: andi $4, $4, 0xff)
    printf("Executed ANDI: R%u = R%u & 0x%04x => 0x%08x\n", rt, rs, imm_zero_ext, result);
}

// SB: Store Byte (Opcode 0b101000 = 0x28)
// Stores the lowest 8 bits from register 'rt' into memory.
// Address is calculated as register 'rs' + sign-extended 16-bit immediate offset.
// No alignment requirement.
// Based on Section 2.43
void op_sb(Cpu* cpu, uint32_t instruction) {
    // Check cache isolation bit in Status Register (SR).
    if ((cpu->sr & 0x10000) != 0) {
        printf("Executed SB: Ignored (Cache Isolated, SR=0x%08x)\n", cpu->sr);
        return; // Skip the memory store
    }

    // Extract the 16-bit immediate and sign-extend it to 32 bits.
    uint32_t offset = instr_imm_se(instruction);
    // Extract the target register (rt) which holds the value to store.
    uint32_t rt = instr_t(instruction);
    // Extract the base register (rs) which holds the base address.
    uint32_t rs = instr_s(instruction);

    // Read the base address from register rs.
    uint32_t base_addr = cpu_reg(cpu, rs);
    // Read the full 32-bit value from register rt.
    uint32_t value_to_store = cpu_reg(cpu, rt);

    // Calculate the effective memory address.
    uint32_t address = base_addr + offset;

    // Call the CPU's store8 function (which delegates to the interconnect).
    // We only pass the lower 8 bits of the value.
    cpu_store8(cpu, address, (uint8_t)value_to_store);

    // Debug print.
    printf("Executed SB: M[R%u + %d (0x%08x)] = R%u (lower 8b = 0x%02x) @ address 0x%08x\n",
           rs, (int16_t)offset, offset, rt, (uint8_t)value_to_store, address);
}

// MTLO: Move To LO (Opcode 0x00, Subfunction 0x13)
// Copies the value from GPR 'rs' into the special LO register.
// Based on Section 2.73
void op_mtlo(Cpu* cpu, uint32_t instruction) {
    // Extract the source register index (rs).
    uint32_t rs = instr_s(instruction);

    // Read the value from the source register rs.
    uint32_t rs_value = cpu_reg(cpu, rs);

    // Write the value into the LO register.
    cpu->lo = rs_value;

    // Debug print.
    printf("Executed MTLO: LO = R%u (0x%08x)\n", rs, rs_value);
}

// MTHI: Move To HI (Opcode 0x00, Subfunction 0x11)
// Copies the value from GPR 'rs' into the special HI register.
// Based on Section 2.74
void op_mthi(Cpu* cpu, uint32_t instruction) {
    // Extract the source register index (rs).
    uint32_t rs = instr_s(instruction);

    // Read the value from the source register rs.
    uint32_t rs_value = cpu_reg(cpu, rs);

    // Write the value into the HI register.
    cpu->hi = rs_value;

    // Debug print.
    printf("Executed MTHI: HI = R%u (0x%08x)\n", rs, rs_value);
}


// LB: Load Byte (Signed) (Opcode 0b100000 = 0x20)
// Loads an 8-bit byte from memory, sign-extends it to 32 bits,
// and schedules the write to register 'rt' after the delay slot.
// Address = rs + sign_extended_offset.
// Based on Section 2.46
void op_lb(Cpu* cpu, uint32_t instruction) {
    // Cache isolate check (assuming loads might also be affected, like LW)
    // Guide doesn't explicitly mention LB for cache isolate, but let's be consistent.
    if ((cpu->sr & 0x10000) != 0) {
        printf("Executed LB: Ignored (Cache Isolated, SR=0x%08x)\n", cpu->sr);
        return;
    }

    uint32_t offset = instr_imm_se(instruction); // Sign-extended immediate offset
    uint32_t rt = instr_t(instruction);         // Target register index
    uint32_t rs = instr_s(instruction);         // Base address register index

    // Read base address from rs
    uint32_t base_addr = cpu_reg(cpu, rs);
    // Calculate effective memory address
    uint32_t address = base_addr + offset;

    // Load the 8-bit byte from memory via interconnect's load8 function
    // Note: Interconnect's load8 should already exist from previous steps
    uint8_t byte_loaded = interconnect_load8(cpu->inter, address); // <-- Make sure load8 is implemented in interconnect

    // *** Sign-extend the loaded byte to 32 bits ***
    // Cast the uint8_t to int8_t, then to int32_t (which performs sign extension),
    // then cast to uint32_t for storage/scheduling. [cite: 559, 562]
    uint32_t value_sign_extended = (uint32_t)(int32_t)(int8_t)byte_loaded;

    // Schedule the sign-extended value for the load delay slot
    cpu->load_reg_idx = rt;
    cpu->load_value = value_sign_extended;

    printf("Executed LB: R%u <- M[R%u + %d (0x%08x)] = 0x%02x (sign-extended to 0x%08x) @ 0x%08x (Delayed)\n",
           rt, rs, (int16_t)offset, offset, byte_loaded, value_sign_extended, address);
}

// JR: Jump Register (Opcode 0x00, Subfunction 0x08)
// Jumps to the address contained in register 'rs'.
// Execution continues at the delay slot instruction before jumping.
// Based on Section 2.45 [cite: 554]
void op_jr(Cpu* cpu, uint32_t instruction) {
    // Extract the source register index (rs) which holds the target address.
    uint32_t rs = instr_s(instruction);

    // Read the target address from the specified register.
    uint32_t target_address = cpu_reg(cpu, rs);

    // Set the *next* program counter (the one AFTER the delay slot executes).
    cpu->next_pc = target_address; // [cite: 555] (adattato per next_pc)

    // ---> SET BRANCH FLAG <---
    cpu->branch_taken = true;
    // ------------------------

    // Debug print. Often used with R31 ($ra) to return from function.
    printf("Executed JR: Jump to R%u (0x%08x). Branch delay slot. Next PC set to 0x%08x\n",
           rs, target_address, target_address);
}

// BEQ: Branch if Equal (Opcode 0b000100 = 0x4)
// Compares registers 'rs' and 'rt'. If rs == rt, branches to PC + 4 + (offset * 4).
// Based on Section 2.47
void op_beq(Cpu* cpu, uint32_t instruction) {
    // Extract the 16-bit immediate and sign-extend it.
    uint32_t imm_se = instr_imm_se(instruction);
    // Extract the source register indices.
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);

    // Read the values from the source registers.
    uint32_t rs_value = cpu_reg(cpu, rs);
    uint32_t rt_value = cpu_reg(cpu, rt);

    // Compare the register values.
    if (rs_value == rt_value) {
        // If equal, call the branch helper to update next_pc.
        cpu_branch(cpu, imm_se);

        // ---> SET BRANCH FLAG <---
        cpu->branch_taken = true;
        // ------------------------

        printf("Executed BEQ: R%u (0x%x) == R%u (0x%x), Branch taken. Next PC will be 0x%08x\n",
               rs, rs_value, rt, rt_value, cpu->next_pc);
    } else {
        // If not equal, do nothing (next_pc remains pc+4 from run_next_instruction).
        printf("Executed BEQ: R%u (0x%x) != R%u (0x%x), Branch not taken.\n",
               rs, rs_value, rt, rt_value);
    }
}

// BLEZ: Branch if Less Than or Equal to Zero (Opcode 0b000110 = 0x6)
// Compares register 'rs' (signed) against 0. If rs <= 0, branches.
// Based on Section 2.55 [cite: 647]
void op_blez(Cpu* cpu, uint32_t instruction) {
    // Extract the 16-bit immediate and sign-extend it.
    uint32_t imm_se = instr_imm_se(instruction);
    // Extract the source register index (rs) to compare against zero.
    uint32_t rs = instr_s(instruction);
    // Note: 'rt' field is not used for the comparison in BLEZ.

    // Read the value from the source register and treat as signed. [cite: 650]
    int32_t rs_value = (int32_t)cpu_reg(cpu, rs);

    // Perform the signed comparison against zero. [cite: 651]
    if (rs_value <= 0) {
        // If less than or equal to zero, call the branch helper to update next_pc.
        cpu_branch(cpu, imm_se);

        // ---> SET BRANCH FLAG <---
        cpu->branch_taken = true;
        // ------------------------

        printf("Executed BLEZ: R%u (%d) <= 0, Branch taken. Next PC will be 0x%08x\n",
               rs, rs_value, cpu->next_pc);
    } else {
        // If greater than zero, do nothing (next_pc remains pc+4).
        printf("Executed BLEZ: R%u (%d) > 0, Branch not taken.\n",
               rs, rs_value);
    }
}

// SLLV: Shift Left Logical Variable (Opcode 0x00, Subfunction 0x04)
// Shifts register 'rt' left by the amount specified in the lower 5 bits
// of register 'rs'. Stores the result in 'rd'.
// Based on Section 2.83
void op_sllv(Cpu* cpu, uint32_t instruction) {
    uint32_t rd = instr_d(instruction);         // Destination register index
    uint32_t rs = instr_s(instruction);         // Register containing shift amount
    uint32_t rt = instr_t(instruction);         // Register containing value to shift

    uint32_t value_to_shift = cpu_reg(cpu, rt);
    // Shift amount comes from the lower 5 bits of register rs
    uint32_t shift_amount = cpu_reg(cpu, rs) & 0x1F; // Mask to 5 bits

    // Perform the logical left shift
    uint32_t result = value_to_shift << shift_amount;

    // Write the result to the destination register rd
    cpu_set_reg(cpu, rd, result);

    printf("Executed SLLV: R%u = R%u << (R%u & 0x1f (%u)) => 0x%08x\n",
           rd, rt, rs, shift_amount, result);
}

// LHU: Load Halfword Unsigned (Opcode 0b100101 = 0x25)
// Loads a 16-bit value from memory, zero-extends it to 32 bits,
// and schedules the write to register 'rt' after the delay slot.
// Address = rs + sign_extended_offset.
// Based on Section 2.82
void op_lhu(Cpu* cpu, uint32_t instruction) {
    // Check cache isolation (consistency with other loads)
    if ((cpu->sr & 0x10000) != 0) {
        printf("Executed LHU: Ignored (Cache Isolated, SR=0x%08x)\n", cpu->sr);
        // Ensure no pending load is scheduled if ignored
        // (Current logic in run_next_instruction already clears pending load target)
        return;
    }

    uint32_t offset = instr_imm_se(instruction); // Sign-extended immediate offset
    uint32_t rt = instr_t(instruction);         // Target register index
    uint32_t rs = instr_s(instruction);         // Base address register index

    // Read base address from rs
    uint32_t base_addr = cpu_reg(cpu, rs);
    // Calculate effective memory address
    uint32_t address = base_addr + offset;

    // Check alignment (must be multiple of 2 for halfword)
    if (address % 2 != 0) {
        fprintf(stderr, "LHU Alignment Error: Address 0x%08x\n", address);
        cpu_exception(cpu, EXCEPTION_LOAD_ADDRESS_ERROR); // Trigger Address Error Load exception
        return; // Stop processing this instruction after exception
    }

    // Load the 16-bit halfword from memory via CPU helper -> interconnect
    uint16_t halfword_loaded = cpu_load16(cpu, address);

    // *** Zero-extend the loaded halfword to 32 bits ***
    // Simply casting uint16_t to uint32_t achieves zero-extension.
    uint32_t value_zero_extended = (uint32_t)halfword_loaded;

    // Schedule the zero-extended value for the load delay slot
    cpu->load_reg_idx = rt;
    cpu->load_value = value_zero_extended;

    printf("Executed LHU: R%u <- M[R%u + %d (0x%08x)] = 0x%04x (zero-extended to 0x%08x) @ 0x%08x (Delayed)\n",
           rt, rs, (int16_t)offset, offset, halfword_loaded, value_zero_extended, address);
}

// LH: Load Halfword (Signed) (Opcode 0b100001 = 0x21)
// Loads a 16-bit value from memory, sign-extends it to 32 bits,
// and schedules the write to register 'rt' after the delay slot.
// Address = rs + sign_extended_offset.
// Based on Section 2.84
void op_lh(Cpu* cpu, uint32_t instruction) {
    // Check cache isolation
    if ((cpu->sr & 0x10000) != 0) {
        printf("Executed LH: Ignored (Cache Isolated, SR=0x%08x)\n", cpu->sr);
        return;
    }

    uint32_t offset = instr_imm_se(instruction); // Sign-extended immediate offset
    uint32_t rt = instr_t(instruction);         // Target register index
    uint32_t rs = instr_s(instruction);         // Base address register index

    // Read base address from rs
    uint32_t base_addr = cpu_reg(cpu, rs);
    // Calculate effective memory address
    uint32_t address = base_addr + offset;

    // Check alignment (must be multiple of 2 for halfword)
    if (address % 2 != 0) {
        fprintf(stderr, "LH Alignment Error: Address 0x%08x\n", address);
        cpu_exception(cpu, EXCEPTION_LOAD_ADDRESS_ERROR); // Trigger Address Error Load exception
        return;
    }

    // Load the 16-bit halfword from memory via CPU helper -> interconnect
    uint16_t halfword_loaded = cpu_load16(cpu, address);

    // *** Sign-extend the loaded halfword to 32 bits ***
    // Cast uint16 -> int16 -> int32 (performs sign extension) -> uint32
    uint32_t value_sign_extended = (uint32_t)(int32_t)(int16_t)halfword_loaded;

    // Schedule the sign-extended value for the load delay slot
    cpu->load_reg_idx = rt;
    cpu->load_value = value_sign_extended;

    printf("Executed LH: R%u <- M[R%u + %d (0x%08x)] = 0x%04x (sign-extended to 0x%08x) @ 0x%08x (Delayed)\n",
           rt, rs, (int16_t)offset, offset, halfword_loaded, value_sign_extended, address);
}

// SRAV: Shift Right Arithmetic Variable (Opcode 0x00, Subfunction 0x07)
// Arithmetically shifts register 'rt' right by the amount specified in the
// lower 5 bits of register 'rs'. Stores the result in 'rd'.
// Based on Section 2.86
void op_srav(Cpu* cpu, uint32_t instruction) {
    uint32_t rd = instr_d(instruction);         // Destination register index
    uint32_t rs = instr_s(instruction);         // Register containing shift amount
    uint32_t rt = instr_t(instruction);         // Register containing value to shift

    // Read value to shift and cast to signed *before* shifting
    int32_t value_to_shift_signed = (int32_t)cpu_reg(cpu, rt);

    // Shift amount comes from the lower 5 bits of register rs
    uint32_t shift_amount = cpu_reg(cpu, rs) & 0x1F; // Mask to 5 bits

    // Perform the arithmetic right shift (C >> on signed type is usually arithmetic)
    int32_t shifted_signed = value_to_shift_signed >> shift_amount;

    // Cast result back to uint32_t for storage
    uint32_t result = (uint32_t)shifted_signed;

    // Write the result to the destination register rd
    cpu_set_reg(cpu, rd, result);

    printf("Executed SRAV: R%u = R%d >> (R%u & 0x1f (%u)) => 0x%08x\n",
           rd, rt, rs, shift_amount, result);
}

// SRLV: Shift Right Logical Variable (Opcode 0x00, Subfunction 0x06)
// Logically shifts register 'rt' right by the amount specified in the
// lower 5 bits of register 'rs'. Stores the result in 'rd'.
// Based on Section 2.87
void op_srlv(Cpu* cpu, uint32_t instruction) {
    uint32_t rd = instr_d(instruction);         // Destination register index
    uint32_t rs = instr_s(instruction);         // Register containing shift amount
    uint32_t rt = instr_t(instruction);         // Register containing value to shift

    // Read value to shift (already uint32_t)
    uint32_t value_to_shift = cpu_reg(cpu, rt);

    // Shift amount comes from the lower 5 bits of register rs
    uint32_t shift_amount = cpu_reg(cpu, rs) & 0x1F; // Mask to 5 bits

    // Perform the logical right shift (C >> on unsigned type is logical)
    uint32_t result = value_to_shift >> shift_amount;

    // Write the result to the destination register rd
    cpu_set_reg(cpu, rd, result);

    printf("Executed SRLV: R%u = R%u >> (R%u & 0x1f (%u)) => 0x%08x\n",
           rd, rt, rs, shift_amount, result);
}

// MULTU: Multiply Unsigned (Opcode 0x00, Subfunction 0x19)
// Multiplies rs by rt (unsigned). Stores 64-bit result in HI/LO.
// Based on Section 2.88
void op_multu(Cpu* cpu, uint32_t instruction) {
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);

    // Read values and cast to 64-bit unsigned for multiplication
    uint64_t val_rs = (uint64_t)cpu_reg(cpu, rs);
    uint64_t val_rt = (uint64_t)cpu_reg(cpu, rt);

    // Perform 64-bit multiplication
    uint64_t result_64 = val_rs * val_rt;

    // Store upper 32 bits in HI
    cpu->hi = (uint32_t)(result_64 >> 32);
    // Store lower 32 bits in LO
    cpu->lo = (uint32_t)(result_64 & 0xFFFFFFFF); // Masking is optional here due to cast

    // Note: MIPS multiplication latency is ignored for now.
    printf("Executed MULTU: (HI, LO) = R%u * R%u => (0x%08x, 0x%08x)\n",
           rs, rt, cpu->hi, cpu->lo);
}

// XOR: Bitwise Exclusive OR (Opcode 0x00, Subfunction 0x26)
// Calculates rs ^ rt and stores the result in rd.
// Based on Section 2.93
void op_xor(Cpu* cpu, uint32_t instruction) {
    uint32_t rd = instr_d(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);

    // Read values from source registers
    uint32_t rs_value = cpu_reg(cpu, rs);
    uint32_t rt_value = cpu_reg(cpu, rt);

    // Perform bitwise XOR
    uint32_t result = rs_value ^ rt_value;

    // Write result to destination register
    cpu_set_reg(cpu, rd, result);

    printf("Executed XOR: R%u = R%u ^ R%u => 0x%08x\n", rd, rs, rt, result);
}

// BREAK: Breakpoint (Opcode 0x00, Subfunction 0x0d)
// Triggers a Breakpoint Exception.
// Based on Section 2.94
void op_break(Cpu* cpu, uint32_t instruction) {
    // The instruction argument might sometimes contain a code for the debugger,
    // but the CPU itself just triggers the exception.
    printf("Executed BREAK: Triggering exception...\n");
    cpu_exception(cpu, EXCEPTION_BREAK);
}

// MULT: Multiply (Signed) (Opcode 0x00, Subfunction 0x18)
// Multiplies rs by rt (signed). Stores 64-bit result in HI/LO.
// Based on Section 2.95
void op_mult(Cpu* cpu, uint32_t instruction) {
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);

    // Read values, cast to signed 32-bit, then sign-extend to 64-bit signed
    int64_t val_rs = (int64_t)(int32_t)cpu_reg(cpu, rs);
    int64_t val_rt = (int64_t)(int32_t)cpu_reg(cpu, rt);

    // Perform 64-bit signed multiplication
    int64_t result_s64 = val_rs * val_rt;

    // Treat the 64-bit result as unsigned for splitting into HI/LO
    uint64_t result_u64 = (uint64_t)result_s64;

    // Store upper 32 bits in HI
    cpu->hi = (uint32_t)(result_u64 >> 32);
    // Store lower 32 bits in LO
    cpu->lo = (uint32_t)(result_u64 & 0xFFFFFFFF);

    // Note: MIPS multiplication latency is ignored for now.
    printf("Executed MULT: (HI, LO) = R%d * R%d => (0x%08x, 0x%08x)\n",
           rs, rt, cpu->hi, cpu->lo);
}

// SUB: Subtract (Signed, with Overflow Check) (Opcode 0x00, Subfunction 0x22)
// Subtracts rt from rs (signed). Stores result in rd.
// Triggers exception on signed overflow.
// Based on Section 2.96
void op_sub(Cpu* cpu, uint32_t instruction) {
    uint32_t rd = instr_d(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);

    // Read values from source registers, cast to signed
    int32_t rs_value = (int32_t)cpu_reg(cpu, rs);
    int32_t rt_value = (int32_t)cpu_reg(cpu, rt);

    // Perform signed subtraction and check for overflow
    // Method 1: Using GCC/Clang built-in (preferred)
    int32_t result;
    if (__builtin_sub_overflow(rs_value, rt_value, &result)) {
        // Overflow occurred!
        fprintf(stderr, "SUB Signed Overflow: %d - %d\n", rs_value, rt_value);
        cpu_exception(cpu, EXCEPTION_OVERFLOW);
    } else {
        // No overflow, write the result (cast back to uint32_t for storage).
        cpu_set_reg(cpu, rd, (uint32_t)result);
        printf("Executed SUB: R%u = R%u - R%u => 0x%08x\n", rd, rs, rt, (uint32_t)result);
    }

    /*
    // Method 2: Manual check (Less preferred, more complex to get right)
    // Check if signs are different AND sign of result is different from minuend (rs_value)
    int32_t result = rs_value - rt_value; // Perform subtraction first
    if (((rs_value ^ rt_value) & (rs_value ^ result)) < 0) {
         // Overflow occurred!
         fprintf(stderr, "SUB Signed Overflow: %d - %d\n", rs_value, rt_value);
         cpu_exception(cpu, EXCEPTION_OVERFLOW);
    } else {
        // No overflow, write the result.
        cpu_set_reg(cpu, rd, (uint32_t)result);
        printf("Executed SUB: R%u = R%u - R%u => 0x%08x\n", rd, rs, rt, (uint32_t)result);
    }
    */
}

// XORI: Bitwise Exclusive OR Immediate (Opcode 0b001110 = 0xE)
// Performs a bitwise XOR between register 'rs' and the zero-extended
// immediate value. Stores the result in register 'rt'.
// Based on Section 2.97
void op_xori(Cpu* cpu, uint32_t instruction) {
    // Immediate value is zero-extended by instr_imm helper
    uint32_t imm_zero_ext = instr_imm(instruction);
    // Target register index (rt) - this is the destination
    uint32_t rt = instr_t(instruction);
    // Source register index (rs)
    uint32_t rs = instr_s(instruction);

    // Read the value from the source register rs.
    uint32_t rs_value = cpu_reg(cpu, rs);

    // Perform the bitwise XOR operation.
    uint32_t result = rs_value ^ imm_zero_ext;

    // Write the result back to the target register rt.
    cpu_set_reg(cpu, rt, result);

    printf("Executed XORI: R%u = R%u ^ 0x%04x => 0x%08x\n", rt, rs, imm_zero_ext, result);
}

// COP1: Coprocessor 1 Instructions (Opcode 0x11)
// COP1 (FPU) is not implemented on PSX. Triggers exception.
// Based on Section 2.98
void op_cop1(Cpu* cpu, uint32_t instruction) {
    printf("Unhandled COP1 instruction: 0x%08x\n", instruction);
    cpu_exception(cpu, EXCEPTION_COPROCESSOR_ERROR);
}

// COP2: Coprocessor 2 (GTE) Instructions (Opcode 0x12)
// Geometry Transformation Engine. Implementation deferred. Panics for now.
// Based on Section 2.98
void op_cop2(Cpu* cpu, uint32_t instruction) {
    // For now, we panic if we encounter a GTE instruction.
    // Later, this will dispatch GTE-specific commands.
    fprintf(stderr, "Unhandled GTE instruction: 0x%08x at PC=0x%08x\n",
            instruction, cpu->current_pc);
    // Optionally, trigger Coprocessor Unusable exception instead of panic?
    // cpu_exception(cpu, EXCEPTION_COPROCESSOR_ERROR);
    exit(1); // Panic as per guide's suggestion for now
}

// COP3: Coprocessor 3 Instructions (Opcode 0x13)
// COP3 is not implemented on PSX. Triggers exception.
// Based on Section 2.98
void op_cop3(Cpu* cpu, uint32_t instruction) {
    printf("Unhandled COP3 instruction: 0x%08x\n", instruction);
    cpu_exception(cpu, EXCEPTION_COPROCESSOR_ERROR);
}

// LWL: Load Word Left (Little-Endian) (Opcode 0b100010 = 0x22)
// Loads the MSBs of a potentially unaligned word into the MSBs of rt.
// Based on Section 2.99.1
void op_lwl(Cpu* cpu, uint32_t instruction) {
    // Check cache isolation? Guide doesn't explicitly mention for LWL/LWR
    // Let's assume they might be affected like LW for consistency.
    if ((cpu->sr & 0x10000) != 0) {
        printf("Executed LWL: Ignored (Cache Isolated, SR=0x%08x)\n", cpu->sr);
        return;
    }

    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction); // Target register index
    uint32_t rs = instr_s(instruction); // Base address register index

    uint32_t addr = cpu_reg(cpu, rs) + offset;

    // LWL/LWR bypass the load delay slot *if* the target register rt
    // is the same as the currently pending load register.
    // We need the value that *will be* in the register *before* this load completes.
    // This is either the value currently pending load, or the last value written.
    uint32_t current_rt_value;
    if (cpu->load_reg_idx == rt) {
        // Use the value pending from the previous load instruction
        current_rt_value = cpu->load_value;
        printf("LWL: Using pending load value (0x%08x) for R%u\n", current_rt_value, rt);
    } else {
        // Use the value from the output register set of the previous instruction
        current_rt_value = cpu->out_regs[rt]; // Use out_regs BEFORE memcpy in run_next
         printf("LWL: Using out_regs value (0x%08x) for R%u\n", current_rt_value, rt);
    }


    // Load the *aligned* word containing the address
    uint32_t aligned_addr = addr & ~3; // addr & 0xFFFFFFFC
    uint32_t aligned_word = cpu_load32(cpu, aligned_addr); // No alignment exception here

    uint32_t merged_value;

    // Merge based on address alignment (Little Endian)
    switch (addr & 3) {
        case 0: // Load bits 31..24 from aligned_word[0]
            merged_value = (current_rt_value & 0x00FFFFFF) | (aligned_word << 24);
            break;
        case 1: // Load bits 31..16 from aligned_word[0..1]
            merged_value = (current_rt_value & 0x0000FFFF) | (aligned_word << 16);
            break;
        case 2: // Load bits 31..8 from aligned_word[0..2]
            merged_value = (current_rt_value & 0x000000FF) | (aligned_word << 8);
            break;
        case 3: // Load bits 31..0 from aligned_word[0..3]
            merged_value = (current_rt_value & 0x00000000) | (aligned_word << 0);
            break;
        default: // Should be unreachable
             fprintf(stderr,"LWL: Unreachable case in alignment switch!\n");
             merged_value = current_rt_value; // Keep old value on error?
             break;
    }

    // Schedule the merged value for the load delay slot
    cpu->load_reg_idx = rt;
    cpu->load_value = merged_value;

    printf("Executed LWL: R%u <- M[R%u + %d @ 0x%08x], AlignedWord=0x%08x => Merged=0x%08x (Delayed)\n",
           rt, rs, (int16_t)offset, addr, aligned_word, merged_value);
}

// LWR: Load Word Right (Little-Endian) (Opcode 0b100110 = 0x26)
// Loads the LSBs of a potentially unaligned word into the LSBs of rt.
// Based on Section 2.99.2
void op_lwr(Cpu* cpu, uint32_t instruction) {
    // Check cache isolation
    if ((cpu->sr & 0x10000) != 0) {
        printf("Executed LWR: Ignored (Cache Isolated, SR=0x%08x)\n", cpu->sr);
        return;
    }

    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction); // Target register index
    uint32_t rs = instr_s(instruction); // Base address register index

    uint32_t addr = cpu_reg(cpu, rs) + offset;

    // Load delay bypass logic (same as LWL)
    uint32_t current_rt_value;
    if (cpu->load_reg_idx == rt) {
        current_rt_value = cpu->load_value;
         printf("LWR: Using pending load value (0x%08x) for R%u\n", current_rt_value, rt);
    } else {
        current_rt_value = cpu->out_regs[rt];
         printf("LWR: Using out_regs value (0x%08x) for R%u\n", current_rt_value, rt);
    }

    // Load the *aligned* word containing the address
    uint32_t aligned_addr = addr & ~3;
    uint32_t aligned_word = cpu_load32(cpu, aligned_addr);

    uint32_t merged_value;

    // Merge based on address alignment (Little Endian)
    switch (addr & 3) {
        case 0: // Load bits 31..0 from aligned_word[0..3]
            merged_value = (current_rt_value & 0x00000000) | (aligned_word >> 0);
            break;
        case 1: // Load bits 23..0 from aligned_word[1..3]
            merged_value = (current_rt_value & 0xFF000000) | (aligned_word >> 8);
            break;
        case 2: // Load bits 15..0 from aligned_word[2..3]
            merged_value = (current_rt_value & 0xFFFF0000) | (aligned_word >> 16);
            break;
        case 3: // Load bits 7..0 from aligned_word[3]
            merged_value = (current_rt_value & 0xFFFFFF00) | (aligned_word >> 24);
            break;
         default: // Should be unreachable
             fprintf(stderr,"LWR: Unreachable case in alignment switch!\n");
             merged_value = current_rt_value; // Keep old value on error?
             break;
    }

    // Schedule the merged value for the load delay slot
    cpu->load_reg_idx = rt;
    cpu->load_value = merged_value;

    printf("Executed LWR: R%u <- M[R%u + %d @ 0x%08x], AlignedWord=0x%08x => Merged=0x%08x (Delayed)\n",
           rt, rs, (int16_t)offset, addr, aligned_word, merged_value);
}

// SWL: Store Word Left (Little-Endian) (Opcode 0b101010 = 0x2A)
// Stores the MSBs from rt into the LSBs of a potentially unaligned word in memory.
// Based on Section 2.100.1
void op_swl(Cpu* cpu, uint32_t instruction) {
    // Check cache isolation
    if ((cpu->sr & 0x10000) != 0) {
        printf("Executed SWL: Ignored (Cache Isolated, SR=0x%08x)\n", cpu->sr);
        return;
    }

    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction); // Value to store is in rt
    uint32_t rs = instr_s(instruction); // Base address is in rs

    uint32_t addr = cpu_reg(cpu, rs) + offset;
    uint32_t value_to_store = cpu_reg(cpu, rt);

    // Read the *aligned* word containing the address
    uint32_t aligned_addr = addr & ~3;
    // Use load32 directly - bypassing debugger checks for internal read
    uint32_t current_mem_word = interconnect_load32(cpu->inter, aligned_addr);

    uint32_t modified_mem_word;

    // Merge based on address alignment (Little Endian)
    switch (addr & 3) {
        case 0: // Store bits 31..24 from value_to_store into mem byte 0
            modified_mem_word = (current_mem_word & 0xFFFFFF00) | (value_to_store >> 24);
            break;
        case 1: // Store bits 31..16 from value_to_store into mem bytes 0..1
            modified_mem_word = (current_mem_word & 0xFFFF0000) | (value_to_store >> 16);
            break;
        case 2: // Store bits 31..8 from value_to_store into mem bytes 0..2
            modified_mem_word = (current_mem_word & 0xFF000000) | (value_to_store >> 8);
            break;
        case 3: // Store bits 31..0 from value_to_store into mem bytes 0..3
            modified_mem_word = (current_mem_word & 0x00000000) | (value_to_store >> 0);
            break;
        default: // Should be unreachable
            fprintf(stderr,"SWL: Unreachable case in alignment switch!\n");
            modified_mem_word = current_mem_word; // Keep old value on error?
            break;
    }

    // Store the modified aligned word back to memory
    // Use store32 directly - bypassing cache check for internal write
    interconnect_store32(cpu->inter, aligned_addr, modified_mem_word);

    printf("Executed SWL: M[R%u + %d @ 0x%08x] using R%u (0x%08x) => AlignedWrite(0x%08x @ 0x%08x)\n",
           rs, (int16_t)offset, addr, rt, value_to_store, modified_mem_word, aligned_addr);
}

// SWR: Store Word Right (Little-Endian) (Opcode 0b101110 = 0x2E)
// Stores the LSBs from rt into the MSBs of a potentially unaligned word in memory.
// Based on Section 2.100.2
void op_swr(Cpu* cpu, uint32_t instruction) {
    // Check cache isolation
    if ((cpu->sr & 0x10000) != 0) {
        printf("Executed SWR: Ignored (Cache Isolated, SR=0x%08x)\n", cpu->sr);
        return;
    }

    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction); // Value to store is in rt
    uint32_t rs = instr_s(instruction); // Base address is in rs

    uint32_t addr = cpu_reg(cpu, rs) + offset;
    uint32_t value_to_store = cpu_reg(cpu, rt);

    // Read the *aligned* word containing the address
    uint32_t aligned_addr = addr & ~3;
    uint32_t current_mem_word = interconnect_load32(cpu->inter, aligned_addr);

    uint32_t modified_mem_word;

    // Merge based on address alignment (Little Endian)
    switch (addr & 3) {
        case 0: // Store bits 31..0 from value_to_store into mem bytes 0..3
            modified_mem_word = (current_mem_word & 0x00000000) | (value_to_store << 0);
            break;
        case 1: // Store bits 23..0 from value_to_store into mem bytes 1..3
            modified_mem_word = (current_mem_word & 0x000000FF) | (value_to_store << 8);
            break;
        case 2: // Store bits 15..0 from value_to_store into mem bytes 2..3
            modified_mem_word = (current_mem_word & 0x0000FFFF) | (value_to_store << 16);
            break;
        case 3: // Store bits 7..0 from value_to_store into mem byte 3
            modified_mem_word = (current_mem_word & 0x00FFFFFF) | (value_to_store << 24);
            break;
        default: // Should be unreachable
            fprintf(stderr,"SWR: Unreachable case in alignment switch!\n");
            modified_mem_word = current_mem_word; // Keep old value on error?
            break;
    }

    // Store the modified aligned word back to memory
    interconnect_store32(cpu->inter, aligned_addr, modified_mem_word);

    printf("Executed SWR: M[R%u + %d @ 0x%08x] using R%u (0x%08x) => AlignedWrite(0x%08x @ 0x%08x)\n",
           rs, (int16_t)offset, addr, rt, value_to_store, modified_mem_word, aligned_addr);
}

// LWC0: Load Word Coprocessor 0 (Opcode 0x30)
// Not supported on PSX COP0. Triggers exception.
// Based on Section 2.101.1
void op_lwc0(Cpu* cpu, uint32_t instruction) {
    printf("Unsupported LWC0 instruction: 0x%08x\n", instruction);
    cpu_exception(cpu, EXCEPTION_COPROCESSOR_ERROR);
}

// LWC1: Load Word Coprocessor 1 (Opcode 0x31)
// Not supported on PSX COP1. Triggers exception.
// Based on Section 2.101.1
void op_lwc1(Cpu* cpu, uint32_t instruction) {
    printf("Unsupported LWC1 instruction: 0x%08x\n", instruction);
    cpu_exception(cpu, EXCEPTION_COPROCESSOR_ERROR);
}

// LWC2: Load Word Coprocessor 2 (GTE) (Opcode 0x32)
// GTE load. Implementation deferred. Panics for now.
// Based on Section 2.101.1
void op_lwc2(Cpu* cpu, uint32_t instruction) {
    fprintf(stderr, "Unhandled GTE LWC2 instruction: 0x%08x at PC=0x%08x\n",
            instruction, cpu->current_pc);
    // Optionally trigger exception instead:
    // cpu_exception(cpu, EXCEPTION_COPROCESSOR_ERROR);
    exit(1); // Panic as per guide's suggestion
}

// LWC3: Load Word Coprocessor 3 (Opcode 0x33)
// Not supported on PSX COP3. Triggers exception.
// Based on Section 2.101.1
void op_lwc3(Cpu* cpu, uint32_t instruction) {
    printf("Unsupported LWC3 instruction: 0x%08x\n", instruction);
    cpu_exception(cpu, EXCEPTION_COPROCESSOR_ERROR);
}

// SWC0: Store Word Coprocessor 0 (Opcode 0x38)
// Not supported on PSX COP0. Triggers exception.
// Based on Section 2.101.2
void op_swc0(Cpu* cpu, uint32_t instruction) {
    printf("Unsupported SWC0 instruction: 0x%08x\n", instruction);
    cpu_exception(cpu, EXCEPTION_COPROCESSOR_ERROR);
}

// SWC1: Store Word Coprocessor 1 (Opcode 0x39)
// Not supported on PSX COP1. Triggers exception.
// Based on Section 2.101.2
void op_swc1(Cpu* cpu, uint32_t instruction) {
    printf("Unsupported SWC1 instruction: 0x%08x\n", instruction);
    cpu_exception(cpu, EXCEPTION_COPROCESSOR_ERROR);
}

// SWC2: Store Word Coprocessor 2 (GTE) (Opcode 0x3A)
// GTE store. Implementation deferred. Panics for now.
// Based on Section 2.101.2
void op_swc2(Cpu* cpu, uint32_t instruction) {
    fprintf(stderr, "Unhandled GTE SWC2 instruction: 0x%08x at PC=0x%08x\n",
            instruction, cpu->current_pc);
    // Optionally trigger exception instead:
    // cpu_exception(cpu, EXCEPTION_COPROCESSOR_ERROR);
    exit(1); // Panic as per guide's suggestion
}

// SWC3: Store Word Coprocessor 3 (Opcode 0x3B)
// Not supported on PSX COP3. Triggers exception.
// Based on Section 2.101.2
void op_swc3(Cpu* cpu, uint32_t instruction) {
    printf("Unsupported SWC3 instruction: 0x%08x\n", instruction);
    cpu_exception(cpu, EXCEPTION_COPROCESSOR_ERROR);
}

// Handles any unimplemented or invalid instruction opcode/subfunction
// Triggers an Illegal Instruction Exception.
// Based on Section 2.102
void op_illegal(Cpu* cpu, uint32_t instruction) {
    fprintf(stderr, "Illegal instruction encountered: 0x%08x at PC=0x%08x\n",
            instruction, cpu->current_pc);
    cpu_exception(cpu, EXCEPTION_ILLEGAL_INSTRUCTION);
}
// LBU: Load Byte Unsigned (Opcode 0b100100 = 0x24)
// Loads an 8-bit byte from memory, zero-extends it to 32 bits,
// and schedules the write to register 'rt' after the delay slot.
// Address = rs + sign_extended_offset.
// Based on Section 2.56
void op_lbu(Cpu* cpu, uint32_t instruction) {
    // Cache isolate check (consistency with other loads)
    if ((cpu->sr & 0x10000) != 0) {
        printf("Executed LBU: Ignored (Cache Isolated, SR=0x%08x)\n", cpu->sr);
        return;
    }

    uint32_t offset = instr_imm_se(instruction); // Sign-extended immediate offset
    uint32_t rt = instr_t(instruction);         // Target register index
    uint32_t rs = instr_s(instruction);         // Base address register index

    // Read base address from rs
    uint32_t base_addr = cpu_reg(cpu, rs);
    // Calculate effective memory address
    uint32_t address = base_addr + offset;

    // Load the 8-bit byte from memory via interconnect's load8 function
    uint8_t byte_loaded = interconnect_load8(cpu->inter, address);

    // *** Zero-extend the loaded byte to 32 bits ***
    // Simply casting uint8_t to uint32_t achieves zero-extension.
    uint32_t value_zero_extended = (uint32_t)byte_loaded;

    // Schedule the zero-extended value for the load delay slot
    cpu->load_reg_idx = rt;
    cpu->load_value = value_zero_extended;

    printf("Executed LBU: R%u <- M[R%u + %d (0x%08x)] = 0x%02x (zero-extended to 0x%08x) @ 0x%08x (Delayed)\n",
           rt, rs, (int16_t)offset, offset, byte_loaded, value_zero_extended, address);
}

// JALR: Jump And Link Register (Opcode 0x00, Subfunction 0x09)
// Jumps to address in register 'rs'. Stores return address (PC + 4) in register 'rd'.
// Based on Section 2.57
void op_jalr(Cpu* cpu, uint32_t instruction) {
    // Register holding the target jump address
    uint32_t rs = instr_s(instruction);
    // Register where the return address (PC+4) will be stored
    uint32_t rd = instr_d(instruction); // Defaults to 31 ($ra) if not specified in asm

    // Read the target jump address from register rs
    uint32_t target_address = cpu_reg(cpu, rs);

    // Calculate the return address (instruction after the delay slot)
    // cpu->pc currently points *to* the delay slot instruction.
    uint32_t return_addr = cpu->pc + 4;

    // Store the return address in register rd
    cpu_set_reg(cpu, rd, return_addr);

    // Set the *next* program counter (the one AFTER the delay slot executes)
    // This performs the jump.
    cpu->next_pc = target_address;

    // ---> SET BRANCH FLAG <---
    cpu->branch_taken = true;

    printf("Executed JALR: R%u = 0x%08x, Jump to R%u (0x%08x). Branch delay slot. Next PC set to 0x%08x\n",
           rd, return_addr, rs, target_address, target_address);
}

//SPECIAL INSTRUCTIONS
// Handles Opcode 0x01: BLTZ, BGEZ, BLTZAL, BGEZAL
// Differentiates based on bits 16 and 20 within the instruction.
// Based on Section 2.58
void op_bxx(Cpu* cpu, uint32_t instruction) {
    uint32_t imm_se = instr_imm_se(instruction); // Sign-extended offset
    uint32_t rs = instr_s(instruction);         // Register to compare against zero

    // Determine the specific instruction variant from bits 16 & 20
    // These bits are within the 'rt' field's usual position
    int is_bgez = (instruction >> 16) & 1; // 0 for BLTZ, 1 for BGEZ type
    int is_link = (instruction >> 20) & 1; // 1 for linking (BLTZAL/BGEZAL)

    // Read the value from rs and treat as signed
    int32_t rs_value = (int32_t)cpu_reg(cpu, rs);

    // Perform the base comparison (less than zero)
    int test_ltz = (rs_value < 0);

    // Determine the actual condition based on is_bgez
    // test = test_ltz if BLTZ-type (is_bgez=0)
    // test = !test_ltz if BGEZ-type (is_bgez=1)
    // This XOR trick implements the conditional negation:
    int condition_met = test_ltz ^ is_bgez;

    // Determine instruction name for logging (optional but helpful)
    const char* name = "UNKNOWN_BXX";
    if (is_link) {
        name = is_bgez ? "BGEZAL" : "BLTZAL";
    } else {
        name = is_bgez ? "BGEZ" : "BLTZ";
    }

    // Branch if the condition is met
    if (condition_met) {
        // If it's a linking variant, store the return address (PC+4) in $ra ($31)
        if (is_link) {
            uint32_t return_addr = cpu->pc + 4;
            cpu_set_reg(cpu, 31, return_addr); // $ra = 31
             printf("Executed %s: R%u (%d) condition met, R31 = 0x%08x, Branch taken. Next PC will be 0x%08x\n",
                   name, rs, rs_value, return_addr, cpu->pc + (imm_se << 2)); // Approx target for logging
        } else {
             printf("Executed %s: R%u (%d) condition met, Branch taken. Next PC will be 0x%08x\n",
                   name, rs, rs_value, cpu->pc + (imm_se << 2)); // Approx target for logging
        }
        // Schedule the branch
        cpu_branch(cpu, imm_se);

        // ---> SET BRANCH FLAG <---
        cpu->branch_taken = true;
        // ------------------------

    } else {
        printf("Executed %s: R%u (%d) condition not met, Branch not taken.\n",
               name, rs, rs_value);
    }
}

// SLTI: Set on Less Than Immediate (Signed) (Opcode 0b001010 = 0xA)
// Compares register 'rs' (signed) with the sign-extended immediate.
// If rs < immediate, sets register 'rt' to 1, otherwise sets it to 0.
// Based on Section 2.59
void op_slti(Cpu* cpu, uint32_t instruction) {
    // Extract the 16-bit immediate and sign-extend it.
    int32_t imm_se = (int32_t)instr_imm_se(instruction); // Immediate is signed
    // Extract the target register index (rt) - the destination.
    uint32_t rt = instr_t(instruction);
    // Extract the source register index (rs) - the value to compare.
    uint32_t rs = instr_s(instruction);

    // Read the value from the source register rs and treat as signed.
    int32_t rs_value = (int32_t)cpu_reg(cpu, rs);

    // Perform the signed comparison.
    uint32_t result = (rs_value < imm_se) ? 1 : 0;

    // Write the result (0 or 1) back to the target register rt.
    cpu_set_reg(cpu, rt, result);

    // Debug print.
    printf("Executed SLTI: R%u = (R%u (%d) < %d) ? 1 : 0 => %u\n",
           rt, rs, rs_value, imm_se, result);
}

// SUBU: Subtract Unsigned (Opcode 0x00, Subfunction 0x23)
// Subtracts register 'rt' from 'rs' (unsigned). Stores result in 'rd'.
// Does not trap on overflow (wraps).
// Based on Section 2.60
void op_subu(Cpu* cpu, uint32_t instruction) {
    // Extract register indices
    uint32_t rd = instr_d(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);

    // Read values from source registers
    uint32_t rs_value = cpu_reg(cpu, rs);
    uint32_t rt_value = cpu_reg(cpu, rt);

    // Perform unsigned subtraction. Standard C unsigned math handles wrapping.
    uint32_t result = rs_value - rt_value;

    // Write result to destination register
    cpu_set_reg(cpu, rd, result);

    // Debug print
    printf("Executed SUBU: R%u = R%u - R%u => 0x%08x\n", rd, rs, rt, result);
}

// SRA: Shift Right Arithmetic (Opcode 0x00, Subfunction 0x03)
// Shifts register 'rt' right by 'shamt' bits, preserving the sign bit.
// Stores result in 'rd'.
// Based on Section 2.61
void op_sra(Cpu* cpu, uint32_t instruction) {
    // Extract the shift amount (5 bits).
    uint32_t shamt = instr_shift(instruction);
    // Extract the source register index (rt) - the value to shift.
    uint32_t rt = instr_t(instruction);
    // Extract the destination register index (rd).
    uint32_t rd = instr_d(instruction);

    // Read the value to be shifted from register rt.
    uint32_t rt_value = cpu_reg(cpu, rt);

    // Perform the arithmetic right shift.
    // In C, right-shifting a *signed* integer type typically performs
    // an arithmetic shift (implementation-defined, but common).
    // Cast to int32_t first to encourage arithmetic shift.
    int32_t signed_value = (int32_t)rt_value;
    int32_t shifted_signed = signed_value >> shamt;
    // Cast result back to uint32_t for storage.
    uint32_t result = (uint32_t)shifted_signed;

    // Write the result to the destination register rd.
    cpu_set_reg(cpu, rd, result);

    // Debug print.
    printf("Executed SRA: R%u = R%u >> %u => 0x%08x\n", rd, rt, shamt, result);
}

// DIV: Divide Signed (Opcode 0x00, Subfunction 0x1a)
// Divides rs by rt (signed). Stores quotient in LO, remainder in HI.
// Handles division by zero and overflow cases.
// Based on Section 2.62
void op_div(Cpu* cpu, uint32_t instruction) {
    // Extract source register indices
    uint32_t rs = instr_s(instruction); // Numerator
    uint32_t rt = instr_t(instruction); // Denominator

    // Read values from source registers and treat as signed
    int32_t n = (int32_t)cpu_reg(cpu, rs);
    int32_t d = (int32_t)cpu_reg(cpu, rt);

    // Handle special cases based on Guide Table 7 [cite: 727]
    if (d == 0) {
        // Division by zero [cite: 738]
        cpu->hi = (uint32_t)n; // HI = numerator [cite: 739]
        if (n >= 0) {
            cpu->lo = 0xffffffff; // LO = -1 [cite: 740]
        } else {
            cpu->lo = 1;          // LO = 1 [cite: 741]
        }
        printf("Executed DIV: R%u (%d) / 0 => HI=0x%08x, LO=0x%08x (Division by Zero)\n",
               rs, n, cpu->hi, cpu->lo);
    } else if ((uint32_t)n == 0x80000000 && d == -1) {
        // Overflow case: most negative / -1 [cite: 742]
        cpu->hi = 0;              // HI = 0 [cite: 743]
        cpu->lo = 0x80000000;     // LO = most negative (0x80000000) [cite: 743]
        printf("Executed DIV: 0x80000000 / -1 => HI=0x%08x, LO=0x%08x (Overflow)\n",
               cpu->hi, cpu->lo);
    } else {
        // Normal signed division
        int32_t quotient = n / d;
        int32_t remainder = n % d;

        cpu->lo = (uint32_t)quotient;   // LO = quotient [cite: 746]
        cpu->hi = (uint32_t)remainder;  // HI = remainder [cite: 745]
        printf("Executed DIV: R%u (%d) / R%u (%d) => HI=0x%08x (rem %d), LO=0x%08x (quot %d)\n",
               rs, n, rt, d, cpu->hi, remainder, cpu->lo, quotient);
    }
    // Note: In a real system, HI/LO are not available immediately.
    // We are skipping the timing aspect for now.
}

// MFLO: Move From LO (Opcode 0x00, Subfunction 0x12)
// Copies the value from the special LO register into GPR 'rd'.
// Based on Section 2.63
void op_mflo(Cpu* cpu, uint32_t instruction) {
    // Extract the destination register index (rd).
    uint32_t rd = instr_d(instruction);

    // Read the value from the LO register.
    uint32_t lo_value = cpu->lo;

    // Write the value to the destination register rd.
    // Note: MFLO itself doesn't have a delay slot like loads or MFC0.
    cpu_set_reg(cpu, rd, lo_value);

    // Debug print.
    printf("Executed MFLO: R%u = LO (0x%08x)\n", rd, lo_value);
}

// SRL: Shift Right Logical (Opcode 0x00, Subfunction 0x02)
// Shifts register 'rt' right by 'shamt' bits, inserting zeros from the left.
// Stores result in 'rd'.
// Based on Section 2.64
void op_srl(Cpu* cpu, uint32_t instruction) {
    // Extract the shift amount (5 bits).
    uint32_t shamt = instr_shift(instruction);
    // Extract the source register index (rt) - the value to shift.
    uint32_t rt = instr_t(instruction);
    // Extract the destination register index (rd).
    uint32_t rd = instr_d(instruction);

    // Read the value to be shifted from register rt.
    uint32_t rt_value = cpu_reg(cpu, rt);

    // Perform the logical right shift.
    // In C, right-shifting an *unsigned* integer type performs a logical shift.
    uint32_t result = rt_value >> shamt;

    // Write the result to the destination register rd.
    cpu_set_reg(cpu, rd, result);

    // Debug print.
    printf("Executed SRL: R%u = R%u >>> %u => 0x%08x\n", rd, rt, shamt, result);
}

// SLTIU: Set on Less Than Immediate Unsigned (Opcode 0b001011 = 0xB)
// Compares register 'rs' (unsigned) with the sign-extended immediate (unsigned).
// If rs < immediate, sets register 'rt' to 1, otherwise sets it to 0.
// Based on Section 2.65
void op_sltiu(Cpu* cpu, uint32_t instruction) {
    // Extract the 16-bit immediate and sign-extend it. [cite: 763]
    // The immediate is sign-extended *even though* the comparison is unsigned.
    uint32_t imm_se = instr_imm_se(instruction);
    // Extract the target register index (rt) - the destination.
    uint32_t rt = instr_t(instruction);
    // Extract the source register index (rs) - the value to compare.
    uint32_t rs = instr_s(instruction);

    // Read the value from the source register rs (already unsigned).
    uint32_t rs_value = cpu_reg(cpu, rs);

    // Perform the unsigned comparison between rs_value and the sign-extended immediate. [cite: 766]
    uint32_t result = (rs_value < imm_se) ? 1 : 0;

    // Write the result (0 or 1) back to the target register rt. [cite: 767]
    cpu_set_reg(cpu, rt, result);

    // Debug print.
    printf("Executed SLTIU: R%u = (R%u (0x%x) < 0x%x (SE Imm)) ? 1 : 0 => %u\n",
           rt, rs, rs_value, imm_se, result);
}

// Based on Section 2.75
void op_rfe(Cpu* cpu, uint32_t instruction) {
    // MIPS I RFE instruction checks the lower 6 bits are 0b010000. [cite: 913, 923]
    if ((instruction & 0x3f) != 0b010000) {
        fprintf(stderr, "Invalid RFE instruction format: 0x%08x\n", instruction);
        // Handle as an illegal instruction or reserved instruction exception?
        // Guide doesn't specify, let's trigger illegal for now.
        cpu_exception(cpu, EXCEPTION_ILLEGAL_INSTRUCTION);
        return;
    }

    // Restore the previous interrupt mask and user/kernel mode bits.
    // Shift bits [5:2] of the status register right by two positions.
    uint32_t mode = cpu->sr & 0x3f;     // Get bottom 6 bits
    cpu->sr &= ~0xf;                    // Clear bits [3:0]
    cpu->sr |= (mode >> 2);             // Shift stack right (pop) and insert [cite: 928]

    printf("Executed RFE: Restored SR mode bits. SR = 0x%08x\n", cpu->sr);
}

// DIVU: Divide Unsigned (Opcode 0x00, Subfunction 0x1b)
// Divides rs by rt (unsigned). Stores quotient in LO, remainder in HI.
// Handles division by zero.
// Based on Section 2.66
void op_divu(Cpu* cpu, uint32_t instruction) {
    // Extract source register indices
    uint32_t rs = instr_s(instruction); // Numerator
    uint32_t rt = instr_t(instruction); // Denominator

    // Read values from source registers (already unsigned)
    uint32_t n = cpu_reg(cpu, rs);
    uint32_t d = cpu_reg(cpu, rt);

    // Handle special case: division by zero
    if (d == 0) {
        cpu->hi = n;          // HI = numerator
        cpu->lo = 0xffffffff; // LO = all 1s
        printf("Executed DIVU: R%u (0x%x) / 0 => HI=0x%08x, LO=0x%08x (Division by Zero)\n",
               rs, n, cpu->hi, cpu->lo);
    } else {
        // Normal unsigned division
        uint32_t quotient = n / d;
        uint32_t remainder = n % d;

        cpu->lo = quotient;   // LO = quotient
        cpu->hi = remainder;  // HI = remainder
        printf("Executed DIVU: R%u (0x%x) / R%u (0x%x) => HI=0x%08x (rem), LO=0x%08x (quot)\n",
               rs, n, rt, d, cpu->hi, cpu->lo);
    }
    // Note: Timing ignored for now.
}

// MFHI: Move From HI (Opcode 0x00, Subfunction 0x10)
// Copies the value from the special HI register into GPR 'rd'.
// Based on Section 2.67
void op_mfhi(Cpu* cpu, uint32_t instruction) {
    // Extract the destination register index (rd).
    uint32_t rd = instr_d(instruction);

    // Read the value from the HI register.
    uint32_t hi_value = cpu->hi;

    // Write the value to the destination register rd.
    cpu_set_reg(cpu, rd, hi_value);

    // Debug print.
    printf("Executed MFHI: R%u = HI (0x%08x)\n", rd, hi_value);
}

// SLT: Set on Less Than (Signed) (Opcode 0x00, Subfunction 0x2a)
// Compares registers 'rs' and 'rt' (signed).
// If rs < rt, sets register 'rd' to 1, otherwise sets it to 0.
// Based on Section 2.68
void op_slt(Cpu* cpu, uint32_t instruction) {
    // Extract register indices
    uint32_t rd = instr_d(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);

    // Read values from source registers and treat as signed
    int32_t rs_value = (int32_t)cpu_reg(cpu, rs);
    int32_t rt_value = (int32_t)cpu_reg(cpu, rt);

    // Perform the signed comparison.
    uint32_t result = (rs_value < rt_value) ? 1 : 0;

    // Write the result (0 or 1) back to the destination register rd.
    cpu_set_reg(cpu, rd, result);

    // Debug print.
    printf("Executed SLT: R%u = (R%u (%d) < R%u (%d)) ? 1 : 0 => %u\n",
           rd, rs, rs_value, rt, rt_value, result);
}

// NOR: Bitwise Not OR (Opcode 0x00, Subfunction 0x27)
// Calculates ~(rs | rt) and stores the result in rd.
// Based on Section 2.85
void op_nor(Cpu* cpu, uint32_t instruction) {
    // Extract register indices
    uint32_t rd = instr_d(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);

    // Read values from source registers
    uint32_t rs_value = cpu_reg(cpu, rs);
    uint32_t rt_value = cpu_reg(cpu, rt);

    // Perform bitwise OR, then bitwise NOT
    uint32_t or_result = rs_value | rt_value;
    uint32_t result = ~or_result; // Bitwise NOT

    // Write result to destination register
    cpu_set_reg(cpu, rd, result);

    // Debug print
    printf("Executed NOR: R%u = ~(R%u | R%u) => 0x%08x\n", rd, rs, rt, result);
}

// SYSCALL: System Call (Opcode 0x00, Subfunction 0x0c)
// Triggers a System Call Exception (Cause Code 0x8).
// Based on Section 2.72
void op_syscall(Cpu* cpu, uint32_t instruction) {
    // The instruction argument itself is often unused for SYSCALL,
    // but specific conventions might use it (ignored here).
    // The main action is to trigger the exception.
    printf("Executed SYSCALL: Triggering exception...\n");
    cpu_exception(cpu, EXCEPTION_SYSCALL);
}
// Add implementations for ORI, SW, SLL, ADDIU etc. here...


// --- Instruction Decoding & Execution ---
// This function takes the fetched instruction and determines which operation to perform.
// Based on Guide Section 2.10 [cite: 171]
void decode_and_execute(Cpu* cpu, uint32_t instruction) {
    // Extract the primary 6-bit opcode (bits 31-26) using the helper function. [cite: 177, 187]
    uint32_t opcode = instr_function(instruction);

    // Debug print showing the instruction being decoded and its primary opcode.
    //printf("Decoding instruction: 0x%08x (Opcode: 0x%02x)\n", instruction, opcode);

    // Use a switch statement to handle different primary opcodes.
    switch(opcode) {
        // --- J-Type Instructions ---
        case 0b000010: // 0x2: J      <-- ADD THIS CASE
            op_j(cpu, instruction);
            break;
        case 0b000011: // 0x3: JAL <-- ADD THIS CASE
            op_jal(cpu, instruction);
            break;
        case 0b001010: // Opcode 0x0A: SLTI <-- Add this case
            op_slti(cpu, instruction);
            break;        
        // --- Special Instructions --
        case 0b000001: // Opcode 0x01: BGEZ/BLTZ/BGEZAL/BLTZAL group <-- Add this case
            op_bxx(cpu, instruction);
            break;    

        // --- Branch Instructions ---
        case 0b000101: // 0x5: BNE  <-- ADD THIS CASE
            op_bne(cpu, instruction);
            break;
        case 0b000100: // 0x4: BEQ  <-- Add this case
            op_beq(cpu, instruction);
            break;    
        case 0b000111: // Opcode 0x7: BGTZ <-- Add this case
            op_bgtz(cpu, instruction);
            break; 
        case 0b000110: // Opcode 0x6: BLEZ <-- Update this case
            op_blez(cpu, instruction); // Call the actual function now
            break;
        case 0b001011: // Opcode 0x0B: SLTIU <-- Add this case
            op_sltiu(cpu, instruction);
            break; 
        case 0b101010: // Opcode 0x2A: SWL <-- ADD THIS CASE
            op_swl(cpu, instruction);
            break;
            case 0b101110: // Opcode 0x2E: SWR <-- ADD THIS CASE
            op_swr(cpu, instruction);
            break;           
   
            
        // --- I-Type Instructions (and others identified by primary opcode) ---
        case 0b001111: // Opcode 0xF: LUI (Load Upper Immediate) [cite: 178]
            op_lui(cpu, instruction); // Call the LUI implementation function.
            break;
        case 0b001101: // 0xD: ORI <-- ADD THIS CASE
            op_ori(cpu, instruction);
            break;
        case 0b001100: // 0xC: ANDI <-- ADD THIS CASE
            op_andi(cpu, instruction);
            break;    

        // --- Load/Store Instructions ---
        case 0b100011: // 0x23: LW  <-- ADD THIS CASE
             op_lw(cpu, instruction);
             break;
        case 0b100100: // 0x24: LBU
             op_lbu(cpu,instruction);
             break;    
        case 0b100000: // Opcode 0x20: LB  <-- Add this case
             op_lb(cpu, instruction);
             break;
        case 0b100001: // Opcode 0x21: LH <-- ADD THIS CASE
             op_lh(cpu, instruction);     
             break;
        case 0b100010: // Opcode 0x22: LWL <-- ADD THIS CASE
             op_lwl(cpu, instruction);
             break;    
        case 0b100110: // Opcode 0x26: LWR <-- ADD THIS CASE
             op_lwr(cpu, instruction);
             break;      
        case 0b100101: // Opcode 0x25: LHU <-- ADD THIS CASE
             op_lhu(cpu, instruction);
             break;     
        case 0b101011: // 0x2B: SW <-- ADD THIS CASE
            op_sw(cpu, instruction);
            break;    
        case 0b101001: // 0x29: SH <-- ADD THIS CASE
            op_sh(cpu, instruction);
            break;
        case 0b101000: // 0x28: SB <-- ADD THIS CASE
            op_sb(cpu, instruction);
            break;             
        // --- Coprocessor 0 Instructions ---
        case 0b010000: // 0x10: COP0 <-- ADD THIS CASE
            op_cop0(cpu, instruction);
            break;
        case 0b010001: // Opcode 0x11: COP1 <-- ADD THIS CASE
            op_cop1(cpu, instruction);
            break;
        case 0b010010: // Opcode 0x12: COP2 (GTE) <-- ADD THIS CASE
            op_cop2(cpu, instruction);
            break;
        case 0b010011: // Opcode 0x13: COP3 <-- ADD THIS CASE
            op_cop3(cpu, instruction);
            break;    
        
        case 0b001000: //0x08: ADDI
            op_addi(cpu,instruction);
            break;    

        case 0b001001: // 0x9: ADDIU  <-- ADD THIS CASE
            op_addiu(cpu, instruction);
            break;
        case 0b001110: // Opcode 0x0E: XORI <-- ADD THIS CASE
            op_xori(cpu, instruction);
            break;
        // --- LWCn Opcodes ---
        case 0b110000: // Opcode 0x30: LWC0 <-- ADD
            op_lwc0(cpu, instruction);
            break;
        case 0b110001: // Opcode 0x31: LWC1 <-- ADD
            op_lwc1(cpu, instruction);
            break;
        case 0b110010: // Opcode 0x32: LWC2 (GTE) <-- ADD
            op_lwc2(cpu, instruction);
            break;
        case 0b110011: // Opcode 0x33: LWC3 <-- ADD
            op_lwc3(cpu, instruction);
            break;

        // --- SWCn Opcodes ---
        case 0b111000: // Opcode 0x38: SWC0 <-- ADD
            op_swc0(cpu, instruction);
            break;
        case 0b111001: // Opcode 0x39: SWC1 <-- ADD
            op_swc1(cpu, instruction);
            break;
        case 0b111010: // Opcode 0x3A: SWC2 (GTE) <-- ADD
            op_swc2(cpu, instruction);
            break;
        case 0b111011: // Opcode 0x3B: SWC3 <-- ADD
            op_swc3(cpu, instruction);
            break;            
        // Add cases for COP1(Exception), COP2(GTE), COP3(Exception) later
        
        // --- R-Type Instructions (identified by primary opcode 0x00) ---
        case 0b000000: // Opcode 0x00: These instructions use the subfunction field (bits 5-0). [cite: 287]
            { // Inner scope for subfunction variable
                // Extract the 6-bit subfunction code. [cite: 287]
                uint32_t subfunc = instr_subfunction(instruction);
                // Inner switch statement based on the subfunction code.
                switch(subfunc) {
                    case 0b000000: // Subfunction 0x00: SLL (Shift Left Logical) [cite: 288]
                        op_sll(cpu, instruction); // Call SLL implementation (to be added)
                        break;
                    case 0b000010: // Subfunction 0x02: SRL <-- Add this case
                        op_srl(cpu, instruction);
                        break;
                    case 0b101010: // Subfunction 0x2a: SLT <-- Add this case
                        op_slt(cpu, instruction);
                        break;
                    case 0b001100: // Subfunction 0x0c: SYSCALL <-- Add this case
                        op_syscall(cpu, instruction);
                        break;     
                    case 0b100111: // Subfunction 0x27: NOR <-- Add this case
                        op_nor(cpu, instruction);
                        break;
                    case 0b000100: // Subfunction 0x04: SLLV <-- ADD THIS CASE
                        op_sllv(cpu, instruction);
                        break;
                    case 0b001101: // Subfunction 0x0D: BREAK <-- ADD THIS CASE
                        op_break(cpu, instruction);
                        break;        

                    case 0b001000: // Subfunction 0x08: JR  <-- Aggiungi questo case
                        op_jr(cpu, instruction);
                        break;
                    case 0b001001: // Subfunction 0x09: JALR <-- Add this case
                        op_jalr(cpu, instruction);
                        break;    
                    case 0b010010: // Subfunction 0x12: MFLO <-- Add this case
                        op_mflo(cpu, instruction);
                        break;
                    case 0b010000: // Subfunction 0x10: MFHI <-- Add this case
                        op_mfhi(cpu, instruction);
                        break; 
                    case 0b010011: // Subfunction 0x13: MTLO <-- Add this case
                        op_mtlo(cpu, instruction);
                        break;    
                    case 0b010001: // Subfunction 0x11: MTHI <-- Add this case
                        op_mthi(cpu, instruction);
                        break;
                    case 0b000111: // Subfunction 0x07: SRAV <-- ADD THIS CASE
                        op_srav(cpu, instruction);
                        break;
                    case 0b000110: // Subfunction 0x06: SRLV <-- ADD THIS CASE
                        op_srlv(cpu, instruction);
                        break;
                    case 0b011001: // Subfunction 0x19: MULTU <-- ADD THIS CASE
                        op_multu(cpu, instruction);
                        break; 
                    case 0b011000: // Subfunction 0x18: MULT <-- ADD THIS CASE
                        op_mult(cpu, instruction);
                        break;
    
                    case 0b100110: // Subfunction 0x26: XOR <-- ADD THIS CASE
                        op_xor(cpu, instruction);
                        break;           
                    case 0b100001: //0x21: ADDU
                        op_addu(cpu,instruction);
                        break;
                    case 0b100000: // Subfunction 0x20: ADD <-- Add this case
                        op_add(cpu, instruction);
                        break;
                    case 0b100010: // Subfunction 0x22: SUB <-- ADD THIS CASE
                        op_sub(cpu, instruction);
                        break;    
                    case 0b100011: // Subfunction 0x23: SUBU <-- Add this case
                        op_subu(cpu, instruction);
                        break;  
                    case 0b000011: // Subfunction 0x03: SRA <-- Add this case
                        op_sra(cpu, instruction);
                        break;          
                    case 0b100100: // Subfunction 0x24: AND <-- Add this case
                        op_and(cpu, instruction);
                        break;    
                    case 0b100101: //0x25: OR instruction
                        op_or(cpu,instruction);
                        break; 
                    case 0b011010: // Subfunction 0x1a: DIV <-- Add this case
                        op_div(cpu, instruction);
                        break;    
                    case 0b011011: // Subfunction 0x1b: DIVU <-- Add this case
                        op_divu(cpu, instruction);
                        break;    
                    case 0b101011: //0x2B: SLTU
                        op_sltu(cpu,instruction);
                        break;    
                    // Add cases for JR (0x8), OR (0x25), ADDU (0x21), etc. here
                    default: // <-- MODIFY R-TYPE DEFAULT
                    op_illegal(cpu, instruction);
                    break;
                }
            }
            break; // End of case 0b000000
            default: // <-- MODIFY MAIN DEFAULT
            op_illegal(cpu, instruction);
            break;
    }
}


// --- CPU Initialization ---
// Sets up the initial state of the CPU.
// Based on Guide Section 2.13 Implementing the general purpose registers [cite: 218]
void cpu_init(Cpu* cpu, Interconnect* inter) {
    // Set the Program Counter to the BIOS entry point. [cite: 79, 81]
    cpu->pc = 0xbfc00000;
    // Store the pointer to the interconnect module for memory access. [cite: 165]
    cpu->next_pc = cpu ->pc +4;
    cpu->inter = inter;

    // Initialize all General Purpose Registers (GPRs).
    for (int i = 0; i < 32; ++i) {
        // Use a recognizable "garbage" value for uninitialized registers (Guide §2.13) [cite: 219]
        cpu->regs[i] = 0xdeadbeef;
        cpu->out_regs[i] = 0xdeadbeef; // Initialize output regs too

    }
    // GPR $zero (index 0) is hardwired to the value 0. [cite: 204, 221]
    cpu->regs[0] = 0;
    cpu->out_regs[REG_ZERO] = 0; // Ensure $zero is 0 in both


    //Init COPROCESSOR 0 REgisters
    cpu->sr = 0; // Status Register - Initial value unknown, guide uses 0 [cite: 386]
    // The first MTC0 will set it anyway.


    // Initialize load delay slot state
    cpu->load_reg_idx = REG_ZERO; // Target $zero initially (no effect)
    cpu->load_value = 0;

    // Initialize new COP0 registers
    cpu->cause = 0; // Cause Register
    cpu->epc = 0;   // Exception PC
    cpu->current_pc = 0xbfc0000; // Initialize current_pc


    // Initialize HI and LO to garbage values (like other regs) [cite: 730]
    cpu->hi = 0xdeadbeef;
    cpu->lo = 0xdeadbeef;

    // ---> INITIALIZE NEW FLAGS <---
    cpu->branch_taken = false;
    cpu->in_delay_slot = false;

    printf("CPU Initialized. PC = 0x%08x, Next PC = 0x%08x, SR=0x%08x, ... Flags Initialized.\n",
        cpu->pc, cpu->next_pc, cpu->sr /* , ... other fields */);   

    // Print confirmation message.
    printf("CPU Initialized. PC = 0x%08x, Next PC = 0x%08x, GPRs initialized.\n", cpu->pc, cpu->next_pc);
}

// --- Execution Cycle with Branch Delay Slot Handling ---
// Refactored based on Guide §2.23 / §2.32 / §2.71 concepts
void cpu_run_next_instruction(Cpu* cpu) {

    // --- Handle Load Delay Slot (Part 1: Apply previous load) ---
    // Apply the load that was scheduled in the *previous* cycle.
    // This writes the loaded value into the 'out_regs' set for the *current* cycle.
    // Based on Guide §2.32 [cite: 450]
    cpu_set_reg(cpu, cpu->load_reg_idx, cpu->load_value);

    // Reset the pending load target to $zero (no-op) for the current cycle.
    // If the current instruction is LW, it will overwrite this again.
    cpu->load_reg_idx = REG_ZERO;
    // cpu->load_value = 0; // Value doesn't matter when target is R0

    // --- Fetch & Branch Delay Slot Handling ---
    // 1. Get the address of the instruction to execute *now*.
    cpu ->current_pc = cpu->pc; //Store PC before execution
    // 2. Check for PC alignment *before* fetching (Guide Section 2.79)
    if (cpu->current_pc % 4 != 0) {
        fprintf(stderr, "PC Alignment Error: PC=0x%08x\n", cpu->current_pc);
        cpu_exception(cpu, EXCEPTION_LOAD_ADDRESS_ERROR); // Address error on instruction fetch
        // Exception occurred, state is updated by cpu_exception,
        // so we just return to let the next cycle run the handler.
        return;
   }

    // 3. Fetch the instruction at that address.
    uint32_t instruction = cpu_load32(cpu, cpu->current_pc);

    // ---> UPDATE DELAY SLOT FLAGS <---
    // Update in_delay_slot based on whether the *previous* instruction took a branch
    cpu->in_delay_slot = cpu->branch_taken;
    // Reset branch_taken flag for the *current* instruction cycle
    cpu->branch_taken = false;

    // 4. Prepare PC state for the *next* cycle (handles branch delay).
    // This happens *before* execution, so branches/jumps in decode_and_execute
    // will overwrite next_pc if needed.
    cpu->pc = cpu->next_pc;
    cpu->next_pc = cpu->pc + 4; // Default next instruction

   // --- Commit State BEFORE Execute ---
    // Apply pending load value from previous cycle *before* copying regs
    // Note: We already did this at the top, but ensure the order is correct
    // cpu_set_reg(cpu, cpu->load_reg_idx, cpu->load_value); // Redundant if done above
    // cpu->load_reg_idx = REG_ZERO; // Redundant if done above

    // Copy registers from previous cycle's output to current cycle's input
    memcpy(cpu->regs, cpu->out_regs, sizeof(cpu->regs));
    cpu->regs[REG_ZERO] = 0; // Ensure R0 remains 0

    // --- Decode and Execute Current Instruction ---
    // The branch/jump instructions inside decode_and_execute will now set cpu->branch_taken = true if they branch
    decode_and_execute(cpu, instruction);

    // --- Safety net for R0 ---
    // Ensure R0 in the output set is always 0 for the *next* cycle
    cpu->out_regs[REG_ZERO] = 0;
}

// Triggers a CPU exception
// Based on Section 2.71
void cpu_exception(Cpu* cpu, ExceptionCause cause) {
    printf("!!! CPU Exception !!! Cause: 0x%02x at PC=0x%08x\n", cause, cpu->current_pc);

    // Determine exception handler address based on SR bit 22 (BEV)
    uint32_t handler_addr;
    int bev_set = (cpu->sr >> 22) & 1;
    if (bev_set) {
        handler_addr = 0xbfc00180; // Boot Exception Vector
    } else {
        handler_addr = 0x80000080; // General Exception Vector
    }

    // Update Status Register (SR)
    // Shift bits [5:0] left by 2 positions ("push" onto interrupt/mode stack)
    // This disables interrupts and forces kernel mode in the new state.
    uint32_t mode = cpu->sr & 0x3f; // Get bottom 6 bits (current + 2 previous states)
    cpu->sr &= ~0x3f;               // Clear bottom 6 bits
    cpu->sr |= (mode << 2) & 0x3f;  // Shift stack left (push 00) and re-insert

    // Update Cause Register
    // Set bits [6:2] (ExcCode) to the exception cause code.
    // Clear other writable bits for now (e.g., Interrupt Pending, BD).
    // Note: We should preserve IP bits [15:8] if/when interrupts are added.
    cpu->cause &= ~0x7C; // Clear bits 6-2 (ExcCode)
    // cpu->cause &= ~(1 << 31); // Clear Branch Delay (BD) bit initially
    cpu->cause |= ((uint32_t)cause << 2); // Set the exception code

   // ---> MODIFY CAUSE/EPC HANDLING <---
    // Update Cause Register with exception code (bits 6:2)
    cpu->cause = (uint32_t)cause << 2; // Set the exception code first

    // Update EPC (Exception PC) and Cause BD bit based on delay slot
    if (cpu->in_delay_slot) {
        // Exception occurred in a branch delay slot
        cpu->epc = cpu->current_pc - 4; // EPC points to the branch instruction
        cpu->cause |= (1 << 31);        // Set the Branch Delay (BD) bit
        printf("    Exception in delay slot: EPC=0x%08x, CAUSE=0x%08x\n", cpu->epc, cpu->cause);
    } else {
        // Exception occurred in a normal instruction
        cpu->epc = cpu->current_pc;     // EPC points to the faulting instruction
        // Ensure BD bit is clear (optional, but good practice if CAUSE wasn't fully cleared before)
        // cpu->cause &= ~(1 << 31); // Uncomment if CAUSE wasn't cleared before setting ExcCode
        printf("    Exception not in delay slot: EPC=0x%08x, CAUSE=0x%08x\n", cpu->epc, cpu->cause);
    }
    // ----------------------------------

    // Jump to the exception handler (no delay slot)
    cpu->pc = handler_addr;
    cpu->next_pc = cpu->pc + 4; // Use wrapping add if necessary
}