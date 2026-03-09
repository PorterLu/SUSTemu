#ifndef __CPU_IR_H__
#define __CPU_IR_H__

#include <common.h>
#include <reg.h>

/*
 * IRInstType — broad functional classification of a decoded instruction.
 * Used by the pipeline (Phase 2+) for hazard detection and issue logic.
 */
typedef enum {
    ITYPE_ALU,      /* Integer ALU: add/sub/and/or/xor/shift/compare/mul/div/rem */
    ITYPE_LOAD,     /* Memory load: lb/lh/lw/ld and unsigned variants */
    ITYPE_STORE,    /* Memory store: sb/sh/sw/sd */
    ITYPE_BRANCH,   /* Conditional branch: beq/bne/blt/bge/bltu/bgeu */
    ITYPE_JAL,      /* Unconditional jump-and-link */
    ITYPE_JALR,     /* Indirect jump-and-link */
    ITYPE_LUI,      /* Load upper immediate */
    ITYPE_AUIPC,    /* Add upper immediate to PC */
    ITYPE_CSR,      /* CSR read/write: csrrw/csrrs/csrrc */
    ITYPE_SYSTEM,   /* System: ecall / ebreak / mret */
    ITYPE_FENCE,    /* Memory/instruction fence */
    ITYPE_INVALID,  /* Unrecognised encoding */
} IRInstType;

/*
 * IR_Inst — intermediate representation of one decoded RISC-V instruction.
 *
 * Produced by ir_decode(), consumed by ir_execute() and ir_writeback().
 * All pipeline stages (Phase 2+) pass IR_Inst through their registers.
 *
 * Field conventions:
 *   rd  == -1  → no architectural register write (stores, branches, system)
 *   src1_val   → mirrors the `src1` local in the old decode_exec()
 *   src2_val   → mirrors the `src2` local in the old decode_exec()
 *   imm        → branch offset (B-type) or store address offset (S-type)
 *   result     → computed ALU result or loaded value; written to GPR[rd]
 *   dnpc       → resolved next PC after execution
 *   taken      → 1 if a branch was taken, 0 otherwise
 */
typedef struct IR_Inst {
    /* ── Instruction identity ─────────────────────────────── */
    vaddr_t     pc;         /* PC of this instruction                    */
    vaddr_t     snpc;       /* Static next PC = pc + 4                   */
    uint32_t    raw;        /* Raw 32-bit instruction word               */
    IRInstType  type;       /* Functional class (set by exec_fn)         */
    const char *name;       /* Mnemonic string, e.g. "add" (for trace)  */

    /* ── Architectural register indices ──────────────────── */
    int         rd;         /* Destination reg index; -1 = no writeback */
    int         rs1;        /* Source reg 1 index                        */
    int         rs2;        /* Source reg 2 index                        */

    /* ── Decoded operand values ───────────────────────────── */
    word_t      src1_val;   /* R[rs1] or immediate (mirrors old src1)   */
    word_t      src2_val;   /* R[rs2] or immediate (mirrors old src2)   */
    word_t      imm;        /* Branch offset or store offset             */

    /* ── MEM stage fields (Phase 2, computed by exec_fn) ──── */
    vaddr_t     mem_addr;   /* Effective address for load/store          */
    int         mem_width;  /* Access width in bytes: 1/2/4/8            */
    int         mem_sign;   /* 1=signed-extend (SEXT), 0=zero-extend     */

    /* ── Execute results (filled by exec_fn / ir_mem_access) ─ */
    word_t      result;     /* ALU result or loaded memory value         */
    vaddr_t     dnpc;       /* Dynamic next PC after execution           */
    int         taken;      /* Branch: 1=taken, 0=not-taken              */

    /* ── Branch prediction annotation (Phase 3) ──────────── */
    int         bp_predict_taken;
    vaddr_t     bp_predicted_pc;

    /* ── Pipeline control flags (Phase 4+) ───────────────── */
    int         serializing; /* 1 = must drain ROB before issue (CSR/ecall/mret) */

    /* ── Polymorphic executor ─────────────────────────────── */
    void (*exec_fn)(struct IR_Inst *, CPU_state *);
} IR_Inst;

/*
 * ir_decode  — decode a raw instruction word into an IR_Inst.
 *              Reads current register values from the global cpu state.
 *              Sets ir->exec_fn but does NOT execute it yet.
 *
 * ir_execute — call ir->exec_fn(ir, cpu) to compute ir->result and ir->dnpc.
 *              May have memory side effects (loads, stores, ecall).
 *
 * ir_writeback — commit ir->result to cpu->gpr[ir->rd] (if rd != -1),
 *                then enforce cpu->gpr[0] == 0.
 */
void ir_decode(uint32_t raw, vaddr_t pc, vaddr_t snpc, IR_Inst *ir);
void ir_execute(IR_Inst *ir, CPU_state *cpu);
void ir_mem_access(IR_Inst *ir);
void ir_writeback(IR_Inst *ir, CPU_state *cpu);

#endif /* __CPU_IR_H__ */
