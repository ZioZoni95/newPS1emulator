#ifndef CPU_H       // Include guard: Prevents multiple inclusions of this header
#define CPU_H

#include <stdint.h>       // Standard header for fixed-width integer types like uint32_t
#include "interconnect.h" // We need the definition of the Interconnect struct

// Define Register Index type for clarity (can just be uint32_t if preferred)
typedef uint32_t RegisterIndex;
#define REG_ZERO ((RegisterIndex)0) // Represents $zero

// --- Exception Cause Codes ---
// (Based on MIPS spec / Guide exception sections)
typedef enum {
    EXCEPTION_INTERRUPT        = 0x00, // Interrupt
    EXCEPTION_LOAD_ADDRESS_ERROR = 0x04, // Address error on load
    EXCEPTION_STORE_ADDRESS_ERROR= 0x05, // Address error on store
    EXCEPTION_SYSCALL          = 0x08, // System call instruction
    EXCEPTION_BREAK            = 0x09, // Break instruction
    EXCEPTION_ILLEGAL_INSTRUCTION= 0x0a, // Reserved/Illegal instruction
    EXCEPTION_COPROCESSOR_ERROR= 0x0b, // Coprocessor Unusable
    EXCEPTION_OVERFLOW         = 0x0c  // Arithmetic Overflow (ADD/ADDI/SUB)
} ExceptionCause;

// --- CPU Structure Definition ---
// Defines the state of the MIPS R3000 CPU we are emulating.
// Based on Guide Section 2.4, 2.13, 2.12
typedef struct {
    uint32_t pc;            // Program Counter register: Holds the address of the *next* instruction to fetch. [cite: 55, 56]
    uint32_t regs[32];      // General Purpose Registers (GPRs): 32 registers used for general computations. [cite: 196, 218]
    uint32_t out_regs[32];  // GPRs written by the *current* instruction (Output set)
    uint32_t next_pc;   // Needed for branch delay slot emulation (Guide §2.71 / §2.23)

    // --- Pending Load Delay Slot --- [Guide §2.32 / §2.33]
    RegisterIndex load_reg_idx; // Target register index for the pending load.
    uint32_t load_value;        // Value to be loaded into the target register.

    
    Interconnect* inter;    // Pointer to the interconnect: How the CPU accesses memory (BIOS, RAM etc.). [cite: 165]

    // --- Coprocessor 0 Registers ---
    uint32_t sr;            // Status Register (COP0 Reg 12)
    uint32_t cause;         // Cause Register (COP0 Reg 13) <-- ADD
    uint32_t epc; 

    // --- Future State Variables (Placeholders based on later Guide sections) ---
    uint32_t current_pc;// Needed for precise exception EPC (Guide §2.71)
    uint32_t hi, lo;    // HI/LO registers for multiplication/division results (Guide §2.12, §2.62) [cite: 211]

} Cpu;


// --- Helper Macros/Functions for Instruction Decoding ---
// These helpers make it easier to extract specific bitfields from a 32-bit instruction word.
// Based on Guide Section 2.10 and MIPS instruction formats.

// Extracts the 6-bit primary opcode (bits 31-26) [cite: 188]
static inline uint32_t instr_function(uint32_t instruction) {
    return instruction >> 26; // Right shift to get the top 6 bits
}
// Extracts the 'rs' register index (bits 25-21) - Source Register
static inline uint32_t instr_s(uint32_t instruction) {
    return (instruction >> 21) & 0x1F; // Shift and mask with 0b11111
}
// Extracts the 'rt' register index (bits 20-16) - Target Register (or second source) [cite: 188]
static inline uint32_t instr_t(uint32_t instruction) {
    return (instruction >> 16) & 0x1F; // Shift and mask
}
// Extracts the 'rd' register index (bits 15-11) - Destination Register [cite: 297]
static inline uint32_t instr_d(uint32_t instruction) {
    return (instruction >> 11) & 0x1F; // Shift and mask
}
// Extracts the 16-bit immediate value (bits 15-0) [cite: 189]
static inline uint32_t instr_imm(uint32_t instruction) {
    return instruction & 0xFFFF; // Mask bottom 16 bits
}
// Extracts the 16-bit immediate value, sign-extended to 32 bits (Guide §2.17) [cite: 281]
static inline uint32_t instr_imm_se(uint32_t instruction) {
    // Cast the lower 16 bits to a signed 16-bit integer
    int16_t imm16 = (int16_t)(instruction & 0xFFFF);
    // Cast to signed 32-bit (performs sign extension), then to unsigned 32-bit
    return (uint32_t)(int32_t)imm16;
}
// Extracts the 5-bit shift amount (bits 10-6) for shift instructions [cite: 298]
static inline uint32_t instr_shift(uint32_t instruction) {
    return (instruction >> 6) & 0x1F; // Shift and mask
}
// Extracts the 6-bit subfunction code for R-type instructions (bits 5-0) [cite: 297]
static inline uint32_t instr_subfunction(uint32_t instruction) {
    return instruction & 0x3F; // Mask bottom 6 bits (0b111111)
}
// Extracts the 26-bit jump target address field (bits 25-0) for J-type instructions [cite: 322]
static inline uint32_t instr_imm_jump(uint32_t instruction) {
    return instruction & 0x03FFFFFF; // Mask bottom 26 bits
}


// --- Function Declarations (Prototypes) ---

// Initializes the CPU state (PC, registers etc.) to their power-on/reset values.
// Based on Guide Section 2.4.1 Reset value of the PC [cite: 81]
void cpu_init(Cpu* cpu, Interconnect* inter);

// Fetches, decodes, and executes a single CPU instruction cycle.
// Based on Guide Section 2.4 CPU cycle description [cite: 63]
void cpu_run_next_instruction(Cpu* cpu);

// Fetches a 32-bit word from memory via the interconnect.
// Based on Guide Section 2.4's load32 requirement [cite: 63]
uint32_t cpu_load32(Cpu* cpu, uint32_t address);

// Decodes the fetched instruction and calls the appropriate execution function.
// Based on Guide Section 2.10 [cite: 171]
void decode_and_execute(Cpu* cpu, uint32_t instruction);

// Accessor function to read a GPR value.
uint32_t cpu_reg(Cpu* cpu, uint32_t index);
// Accessor function to write a GPR value (handles $zero).
// Based on Guide Section 2.13 set_reg example [cite: 223]
void cpu_set_reg(Cpu* cpu, uint32_t index, uint32_t value);

// Memory Access via CPU (delegates to Interconnect)
void cpu_store32(Cpu* cpu, uint32_t address, uint32_t value);

// Branch Helper
void cpu_branch(Cpu* cpu, uint32_t offset_se);

// Memory Access via CPU (delegates to Interconnect)
void cpu_store16(Cpu* cpu, uint32_t address, uint16_t value); // <-- ADD THIS

void cpu_exception(Cpu* cpu, ExceptionCause cause); // <-- Add prototype


// Specific Instruction Implementation functions (prototypes)
void op_lui(Cpu* cpu, uint32_t instruction);
void op_ori(Cpu* cpu, uint32_t instruction); 
void op_sw(Cpu* cpu, uint32_t instruction);
void op_sll(Cpu* cpu, uint32_t instruction);  
void op_addiu(Cpu* cpu, uint32_t instruction); 
void op_j(Cpu* cpu, uint32_t instruction);  
void op_or(Cpu* cpu, uint32_t instruction);      
void op_cop0(Cpu* cpu, uint32_t instruction);  // <-- ADD: COP0 dispatcher
void op_mtc0(Cpu* cpu, uint32_t instruction);  // <-- ADD: MTC0 handler
void op_bne(Cpu* cpu, uint32_t instruction);
void op_addi(Cpu* cpu, uint32_t instruction);
void op_lw(Cpu* cpu, uint32_t instruction);     // <-- ADD THIS LINE
void op_sltu(Cpu* cpu, uint32_t instruction);    // <-- ADD THIS LINE
void op_addu(Cpu* cpu, uint32_t instruction);    // <-- ADD THIS LINE
void op_sh(Cpu* cpu, uint32_t instruction);     // <-- ADD THIS LINE
void op_jal(Cpu* cpu, uint32_t instruction);     // <-- ADD THIS LINE
void op_andi(Cpu* cpu, uint32_t instruction);
void op_sb(Cpu* cpu, uint32_t instruction);
void op_jr(Cpu* cpu, uint32_t instruction);
void op_lb(Cpu* cpu, uint32_t instruction); // <-- Add this line
void op_beq(Cpu* cpu, uint32_t instruction); // <-- Add this line
void op_mfc0(Cpu* cpu, uint32_t instruction); // <-- Add this line
void op_and(Cpu* cpu, uint32_t instruction); // <-- Add this line
void op_add(Cpu* cpu, uint32_t instruction); // <-- Add this line
void op_bgtz(Cpu* cpu, uint32_t instruction); // <-- Add this line
void op_blez(Cpu* cpu, uint32_t instruction); // <-- Ensure this exists (added as placeholder before)
void op_lbu(Cpu* cpu, uint32_t instruction); // <-- Add this line
void op_jalr(Cpu* cpu, uint32_t instruction); // <-- Add this line
void op_bxx(Cpu* cpu, uint32_t instruction);  // <-- Add this for Opcode 0x01 group (special case)
void op_slti(Cpu* cpu, uint32_t instruction); // <-- Add this line
void op_subu(Cpu* cpu, uint32_t instruction); // <-- Add this line
void op_sra(Cpu* cpu, uint32_t instruction); // <-- Add this line
void op_div(Cpu* cpu, uint32_t instruction); // <-- Add this line
void op_divu(Cpu* cpu, uint32_t instruction); // <-- Add this line
void op_mflo(Cpu* cpu, uint32_t instruction); // <-- Add this line
void op_srl(Cpu* cpu, uint32_t instruction); // <-- Add this line
void op_sltiu(Cpu* cpu, uint32_t instruction); // <-- Add this line
void op_slt(Cpu* cpu, uint32_t instruction); // <-- Add this line
void op_mfhi(Cpu* cpu, uint32_t instruction); // <-- Add this line
void op_syscall(Cpu* cpu, uint32_t instruction); // <-- Add this line
void op_nor(Cpu* cpu, uint32_t instruction); // <-- Add this line
void op_mtlo(Cpu* cpu, uint32_t instruction); // <-- Add this line
void op_mthi(Cpu* cpu, uint32_t instruction); // <-- Add this line


// Memory Access via CPU (delegates to Interconnect)



#endif // CPU_H