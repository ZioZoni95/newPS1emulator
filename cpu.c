#include "cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h> // For memcpy

// --- Memory Access via Interconnect ---
// Fetches a 32-bit word (like an instruction) from the specified memory address.
// This function delegates the actual memory access to the interconnect module.
uint32_t cpu_load32(Cpu* cpu, uint32_t address) {
    // Call the interconnect's function to handle the load based on the address map.
    return interconnect_load32(cpu->inter, address);
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
        printf("Executed BNE: R%u (0x%x) != R%u (0x%x), Branch taken. Next PC will be 0x%08x\n",
               rs, rs_value, rt, rt_value, cpu->next_pc);
    } else {
        // If equal, do nothing (next_pc remains pc+4 from run_next_instruction).
        printf("Executed BNE: R%u (0x%x) == R%u (0x%x), Branch not taken.\n",
               rs, rs_value, rt, rt_value);
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
        printf("Executed BEQ: R%u (0x%x) == R%u (0x%x), Branch taken. Next PC will be 0x%08x\n",
               rs, rs_value, rt, rt_value, cpu->next_pc);
    } else {
        // If not equal, do nothing (next_pc remains pc+4 from run_next_instruction).
        printf("Executed BEQ: R%u (0x%x) != R%u (0x%x), Branch not taken.\n",
               rs, rs_value, rt, rt_value);
    }
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

        // --- Branch Instructions ---
        case 0b000101: // 0x5: BNE  <-- ADD THIS CASE
            op_bne(cpu, instruction);
            break;
        case 0b000100: // 0x4: BEQ  <-- Add this case
            op_beq(cpu, instruction);
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
        case 0b100000: // Opcode 0x20: LB  <-- Add this case
             op_lb(cpu, instruction);
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
        
        case 0b001000: //0x08: ADDI
            op_addi(cpu,instruction);
            break;    

        case 0b001001: // 0x9: ADDIU  <-- ADD THIS CASE
            op_addiu(cpu, instruction);
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

                    case 0b001000: // Subfunction 0x08: JR  <-- Aggiungi questo case
                        op_jr(cpu, instruction);
                        break;
                    
                    case 0b100001: //0x21: ADDU
                        op_addu(cpu,instruction);
                        break;

                    case 0b100101: //0x25: OR instruction
                        op_or(cpu,instruction);
                        break; 
                        
                    case 0b101011: //0x2B: SLTU
                        op_sltu(cpu,instruction);
                        break;    
                    // Add cases for JR (0x8), OR (0x25), ADDU (0x21), etc. here
                    default:
                         fprintf(stderr, "Unhandled R-type instruction: PC=0x%08x, Inst=0x%08x, Subfunction=0x%02x\n",
                                cpu->pc, // PC holds delay slot PC here
                                instruction, subfunc);
                        exit(1);
                }
            }
            break; // End of case 0b000000

        // Add cases for ORI (0xD), ADDIU (0x9), J (0x2), SW (0x2B), LW (0x23), COP0 (0x10) etc. here

        default:
             fprintf(stderr, "Unhandled instruction: PC=0x%08x, Inst=0x%08x, Opcode=0x%02x\n",
                    cpu->pc, // PC holds delay slot PC here
                    instruction, opcode);
            exit(1);
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

    printf("CPU Initialized. PC = 0x%08x, Next PC = 0x%08x, SR=0x%08x, GPRs initialized.\n",
            cpu->pc, cpu->next_pc, cpu->sr);    

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
    uint32_t current_instruction_pc = cpu->pc;

    // 2. Fetch the instruction at that address.
    uint32_t instruction = cpu_load32(cpu, current_instruction_pc);

    // 3. Prepare PC state for the *next* cycle (handles branch delay).
    cpu->pc = cpu->next_pc;
    cpu->next_pc = cpu->pc + 4; // Default next instruction (unless branch changes next_pc)

    // --- Commit State BEFORE Execute ---
    // Copy the output registers from the *previous* instruction execution
    // (including the just-applied pending load) into the input registers
    // that the *current* instruction will read from.
    // Based on Guide §2.32 [cite: 452]
    memcpy(cpu->regs, cpu->out_regs, sizeof(cpu->regs));
    // Ensure R0 remains 0 after copy, just in case.
    cpu->regs[REG_ZERO] = 0;

    // --- Decode and Execute Current Instruction ---
    // This reads from 'cpu->regs' and writes results to 'cpu->out_regs'.
    // If it's an LW, it updates cpu->load_reg_idx/load_value.
    // If it's a branch/jump, it updates cpu->next_pc.
    decode_and_execute(cpu, instruction);

    // Safety net: Ensure R0 ($zero) in out_regs remains 0 after execution.
    // Usually handled by cpu_set_reg, but good practice.
    cpu->out_regs[REG_ZERO] = 0;

    // At the end of this cycle:
    // - 'cpu->regs' holds the state visible to the instruction we just executed.
    // - 'cpu->out_regs' holds the state AFTER this instruction executed.
    // - 'cpu->load_reg_idx/load_value' holds any load scheduled by this instruction.
    // - 'cpu->pc' holds the address of the NEXT instruction to execute.
    // - 'cpu->next_pc' holds the address AFTER the next instruction (or branch target).
    // The results in out_regs/pending load will be committed at the START of the next cycle.
}