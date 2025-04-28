#ifndef CPU_H
#define CPU_H

#include <stdbool.h> // For bool type
#include <stdint.h>  // For uint32_t, int32_t etc.
#include "interconnect.h" // Needs definition of Interconnect for the pointer member

// Define Register Index type for clarity (can just be uint32_t if preferred)
// Using a distinct type can help prevent mixing indices and values, though not strictly enforced here.
typedef uint32_t RegisterIndex;

// Define constants for special register indices
#define REG_ZERO ((RegisterIndex)0)  // $zero GPR, always 0
#define REG_RA   ((RegisterIndex)31) // $ra GPR, Return Address for JAL/JALR


// --- Exception Cause Codes ---
// MIPS Exception codes used in the Cause register (bits 6:2)
// Based on MIPS spec / Guide exception sections
typedef enum {
    EXCEPTION_INTERRUPT        = 0x00, // Hardware Interrupt requested (from I_STAT/I_MASK)
    EXCEPTION_LOAD_ADDRESS_ERROR = 0x04, // Data Load/Instruction Fetch Address Error (Alignment or Bus Error)
    EXCEPTION_STORE_ADDRESS_ERROR= 0x05, // Data Store Address Error (Alignment or Bus Error)
    EXCEPTION_SYSCALL          = 0x08, // Syscall instruction executed
    EXCEPTION_BREAK            = 0x09, // Break instruction executed
    EXCEPTION_ILLEGAL_INSTRUCTION= 0x0a, // CPU encountered an undefined/illegal instruction
    EXCEPTION_COPROCESSOR_ERROR= 0x0b, // Coprocessor Unusable (COP0/COP1/COP2/COP3 operation error)
    EXCEPTION_OVERFLOW         = 0x0c  // Arithmetic Overflow (ADD/ADDI/SUB instructions)
} ExceptionCause;


// ============================================================= //
// ==========>>> ADD INSTRUCTION CACHE PARTS BELOW <<<========== //
// ============================================================= //

/**
 * @brief Constants defining the instruction cache geometry.
 * Based on Guide Section 8.1 [cite: 2987]
 */
#define ICACHE_NUM_LINES 256       // 256 lines in the cache
#define ICACHE_LINE_WORDS 4        // 4 words (instructions) per cache line
#define ICACHE_SIZE_BYTES (ICACHE_NUM_LINES * ICACHE_LINE_WORDS * 4) // 4096 bytes total

/**
 * @brief Represents a single line in the instruction cache.
 * Contains the tag, valid bits for each word, and the cached instruction data.
 * Based on Guide Section 8.1
 */
typedef struct {
    /**
     * @brief The upper 20 bits of the physical address stored in this cache line.
     * Used to verify if the cached data matches the requested address. [cite: 2990, 2999]
     */
    uint32_t tag;
    /**
     * @brief Validity flag for each of the 4 words in the cache line.
     * True if the corresponding data word holds valid instruction data. [cite: 2991]
     */
    bool     valid[ICACHE_LINE_WORDS];
    /**
     * @brief The 4 cached instruction words (32-bit each).
     */
    uint32_t data[ICACHE_LINE_WORDS];
} ICacheLine;

// ============================================================= //
// ============================================================= //

// --- CPU State Structure ---
// Defines the internal state of the emulated MIPS R3000A-compatible CPU.
typedef struct Cpu {
    // --- Core Registers ---
    uint32_t pc;            // Program Counter: Address of the instruction currently being fetched.
    uint32_t next_pc;       // Address of the instruction *after* the delay slot (used for branch delay).
    uint32_t current_pc;    // Address of the instruction currently executing (used for exception EPC).

    // --- General Purpose Registers (GPRs) ---
    uint32_t regs[32];      // Input register values for the current instruction. R0 is hardwired to 0.
    uint32_t out_regs[32];  // Output register values written by the current instruction.

    // --- Load Delay Slot ---
    // MIPS I has a one-instruction delay after a load before the data is available.
    RegisterIndex load_reg_idx; // Target register for the pending load.
    uint32_t load_value;        // Value to be loaded into the target register.

    // --- HI/LO Registers ---
    // Used for results of multiplication and division.
    uint32_t hi;            // Remainder (division), High 32 bits (multiplication).
    uint32_t lo;            // Quotient (division), Low 32 bits (multiplication).

    // --- Branch Delay Slot State ---
    bool branch_taken;      // True if the current instruction caused a jump/branch.
    bool in_delay_slot;     // True if the current instruction is executing in a branch delay slot.

    // --- Coprocessor 0 (System Control Coprocessor) Registers ---
    uint32_t sr;            // COP0 Reg 12: Status Register (Interrupt enables, Cache isolation, etc.).
    uint32_t cause;         // COP0 Reg 13: Cause Register (Exception code, pending interrupts, branch delay flag).
    uint32_t epc;           // COP0 Reg 14: Exception Program Counter (Address of instruction causing exception).
    // Note: Other COP0 registers (Breakpoint, DCIC, etc.) are not fully modeled yet.

    // --- Connection to Memory System ---
    Interconnect* inter;    // Pointer to the interconnect module for memory accesses.

    ICacheLine icache[ICACHE_NUM_LINES];

} Cpu;


// --- Helper Macros/Functions for Instruction Decoding ---
// Static inline functions for efficient extraction of instruction fields.

static inline uint32_t instr_function(uint32_t i) { return i >> 26; } // Opcode
static inline uint32_t instr_s(uint32_t i) { return (i >> 21) & 0x1F; } // Reg rs
static inline uint32_t instr_t(uint32_t i) { return (i >> 16) & 0x1F; } // Reg rt
static inline uint32_t instr_d(uint32_t i) { return (i >> 11) & 0x1F; } // Reg rd
static inline uint32_t instr_imm(uint32_t i) { return i & 0xFFFF; } // Imm (zero-extended)
static inline uint32_t instr_imm_se(uint32_t i) { return (uint32_t)(int32_t)(int16_t)(i & 0xFFFF); } // Imm (sign-extended)
static inline uint32_t instr_shift(uint32_t i) { return (i >> 6) & 0x1F; } // Shift amount
static inline uint32_t instr_subfunction(uint32_t i) { return i & 0x3F; } // Sub-opcode (R-Type)
static inline uint32_t instr_imm_jump(uint32_t i) { return i & 0x03FFFFFF; } // Jump target
// Helper for COP0/COPz opcodes (uses 's' field bits)
static inline uint32_t instr_cop_opcode(uint32_t i) { return (i >> 21) & 0x1F; }


// --- Function Declarations (Prototypes) ---

/**
 * @brief Initializes the CPU state to power-on defaults.
 * Sets PC to BIOS entry, clears registers, sets initial COP0 state.
 * @param cpu Pointer to the Cpu struct to initialize.
 * @param inter Pointer to the initialized Interconnect struct.
 */
void cpu_init(Cpu* cpu, Interconnect* inter);

/**
 * @brief Executes a single CPU instruction cycle.
 * Checks for interrupts, handles load delay slot, fetches, decodes, executes,
 * and updates PC/state for the next cycle.
 * @param cpu Pointer to the Cpu state.
 */
void cpu_run_next_instruction(Cpu* cpu);

/**
 * @brief Decodes the fetched instruction and calls the appropriate handler function.
 * @param cpu Pointer to the Cpu state.
 * @param instruction The 32-bit instruction word to decode and execute.
 */
void decode_and_execute(Cpu* cpu, uint32_t instruction);

/**
 * @brief Triggers a CPU exception.
 * Saves current state (EPC, Cause, SR), updates SR mode bits,
 * and jumps to the appropriate exception handler vector.
 * @param cpu Pointer to the Cpu state.
 * @param cause The reason for the exception (from ExceptionCause enum).
 */
void cpu_exception(Cpu* cpu, ExceptionCause cause);

// --- Register Access ---
/**
 * @brief Reads the value of a General Purpose Register (GPR) from the input set.
 * Handles reads from $zero (always returns 0).
 * @param cpu Pointer to the Cpu state.
 * @param index The index (0-31) of the register to read.
 * @return The 32-bit value of the register.
 */
uint32_t cpu_reg(Cpu* cpu, RegisterIndex index);

/**
 * @brief Writes a value to a General Purpose Register (GPR) in the output set.
 * Ignores writes to $zero (index 0), ensuring it remains 0.
 * @param cpu Pointer to the Cpu state.
 * @param index The index (0-31) of the register to write.
 * @param value The 32-bit value to write.
 */
void cpu_set_reg(Cpu* cpu, RegisterIndex index, uint32_t value);

// --- Branch/Jump Helper ---
/**
 * @brief Updates the next_pc for a branch instruction.
 * Calculates target address based on current PC and sign-extended offset.
 * NOTE: Does NOT set the cpu->branch_taken flag, the caller instruction must do that.
 * @param cpu Pointer to the Cpu state.
 * @param offset_se Sign-extended 16-bit branch offset (*not* shifted).
 */
void cpu_branch(Cpu* cpu, uint32_t offset_se);


/**
 * @brief Fetches an instruction word from memory, using the instruction cache.
 * Handles cache lookup, hit/miss logic, and fetching from interconnect on miss.
 * @param cpu Pointer to the Cpu state (containing the cache).
 * @param vaddr The virtual address of the instruction to fetch.
 * @return The 32-bit instruction word.
 */
static uint32_t cpu_icache_fetch(Cpu* cpu, uint32_t vaddr); // Use Cpu* typedef

// --- Instruction Handler Prototypes (Internal linkage) ---
// These functions implement the behavior of individual MIPS instructions.
static void op_lui(Cpu* cpu, uint32_t instruction);
static void op_ori(Cpu* cpu, uint32_t instruction);
static void op_sw(Cpu* cpu, uint32_t instruction);
static void op_sll(Cpu* cpu, uint32_t instruction);
static void op_addiu(Cpu* cpu, uint32_t instruction);
static void op_j(Cpu* cpu, uint32_t instruction);
static void op_or(Cpu* cpu, uint32_t instruction);
static void op_cop0(Cpu* cpu, uint32_t instruction);
static void op_mtc0(Cpu* cpu, uint32_t instruction);
static void op_rfe(Cpu* cpu, uint32_t instruction);
static void op_bne(Cpu* cpu, uint32_t instruction);
static void op_addi(Cpu* cpu, uint32_t instruction);
static void op_lw(Cpu* cpu, uint32_t instruction);
static void op_sltu(Cpu* cpu, uint32_t instruction);
static void op_addu(Cpu* cpu, uint32_t instruction);
static void op_sh(Cpu* cpu, uint32_t instruction);
static void op_jal(Cpu* cpu, uint32_t instruction);
static void op_andi(Cpu* cpu, uint32_t instruction);
static void op_sb(Cpu* cpu, uint32_t instruction);
static void op_jr(Cpu* cpu, uint32_t instruction);
static void op_lb(Cpu* cpu, uint32_t instruction);
static void op_beq(Cpu* cpu, uint32_t instruction);
static void op_mfc0(Cpu* cpu, uint32_t instruction);
static void op_and(Cpu* cpu, uint32_t instruction);
static void op_add(Cpu* cpu, uint32_t instruction);
static void op_bgtz(Cpu* cpu, uint32_t instruction);
static void op_blez(Cpu* cpu, uint32_t instruction);
static void op_lbu(Cpu* cpu, uint32_t instruction);
static void op_jalr(Cpu* cpu, uint32_t instruction);
static void op_bxx(Cpu* cpu, uint32_t instruction);
static void op_slti(Cpu* cpu, uint32_t instruction);
static void op_subu(Cpu* cpu, uint32_t instruction);
static void op_sra(Cpu* cpu, uint32_t instruction);
static void op_div(Cpu* cpu, uint32_t instruction);
static void op_divu(Cpu* cpu, uint32_t instruction);
static void op_mflo(Cpu* cpu, uint32_t instruction);
static void op_srl(Cpu* cpu, uint32_t instruction);
static void op_sltiu(Cpu* cpu, uint32_t instruction);
static void op_slt(Cpu* cpu, uint32_t instruction);
static void op_mfhi(Cpu* cpu, uint32_t instruction);
static void op_syscall(Cpu* cpu, uint32_t instruction);
static void op_nor(Cpu* cpu, uint32_t instruction);
static void op_mtlo(Cpu* cpu, uint32_t instruction);
static void op_mthi(Cpu* cpu, uint32_t instruction);
static void op_lhu(Cpu* cpu, uint32_t instruction);
static void op_lh(Cpu* cpu, uint32_t instruction);
static void op_sllv(Cpu* cpu, uint32_t instruction);
static void op_srav(Cpu* cpu, uint32_t instruction);
static void op_srlv(Cpu* cpu, uint32_t instruction);
static void op_multu(Cpu* cpu, uint32_t instruction);
static void op_xor(Cpu* cpu, uint32_t instruction);
static void op_break(Cpu* cpu, uint32_t instruction);
static void op_mult(Cpu* cpu, uint32_t instruction);
static void op_sub(Cpu* cpu, uint32_t instruction);
static void op_xori(Cpu* cpu, uint32_t instruction);
static void op_cop1(Cpu* cpu, uint32_t instruction);
static void op_cop2(Cpu* cpu, uint32_t instruction);
static void op_cop3(Cpu* cpu, uint32_t instruction);
static void op_lwl(Cpu* cpu, uint32_t instruction);
static void op_lwr(Cpu* cpu, uint32_t instruction);
static void op_swl(Cpu* cpu, uint32_t instruction);
static void op_swr(Cpu* cpu, uint32_t instruction);
static void op_lwc0(Cpu* cpu, uint32_t instruction);
static void op_lwc1(Cpu* cpu, uint32_t instruction);
static void op_lwc2(Cpu* cpu, uint32_t instruction);
static void op_lwc3(Cpu* cpu, uint32_t instruction);
static void op_swc0(Cpu* cpu, uint32_t instruction);
static void op_swc1(Cpu* cpu, uint32_t instruction);
static void op_swc2(Cpu* cpu, uint32_t instruction);
static void op_swc3(Cpu* cpu, uint32_t instruction);
static void op_illegal(Cpu* cpu, uint32_t instruction);

#endif // CPU_H