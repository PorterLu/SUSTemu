/*
 * src/cpu/ir.c — IR (Intermediate Representation) decode/execute/writeback
 *
 * This file implements the three-step instruction lifecycle introduced by
 * Phase 1 of the SUSTemu architectural simulator refactor:
 *
 *   ir_decode()    — pattern-match raw bits → fill IR_Inst + set exec_fn
 *   ir_execute()   — call exec_fn → compute result / dnpc / side effects
 *   ir_writeback() — commit result to architectural register file
 *
 * The shared instruction pattern table (include/cpu/inst_table.inc) is
 * reused here with locally-overridden INSTPAT_INST / INSTPAT_MATCH macros
 * so that the same encoding strings drive both functional simulation (exec.c)
 * and IR-based simulation (this file) without duplication.
 */

#include <ir.h>
#include <decode.h>
#include <reg.h>
#include <csr.h>
#include <vmem.h>
#include <state.h>
#include <elftl.h>
#include <string.h>
#include <exec_cfg.h>

/* ── Forward declarations of per-instruction executor functions ─────────── */

static void ir_exec_auipc (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_lui   (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_add   (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_mul   (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_sub   (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_or    (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_xor   (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_and   (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_slt   (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_sltu  (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_sll   (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_srl   (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_sra   (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_div   (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_divu  (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_rem   (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_remu  (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_addw  (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_mulw  (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_subw  (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_sllw  (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_srlw  (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_sraw  (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_divw  (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_divuw (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_remw  (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_remuw (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_addi  (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_ori   (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_xori  (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_andi  (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_slti  (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_sltiu (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_slli  (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_srli  (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_srai  (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_addiw (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_slliw (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_srliw (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_sraiw (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_jal   (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_jalr  (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_beq   (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_bne   (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_blt   (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_bge   (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_bltu  (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_bgeu  (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_ld    (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_lw    (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_lh    (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_lb    (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_lwu   (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_lhu   (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_lbu   (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_sd    (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_sw    (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_sh    (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_sb    (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_csrrw (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_csrrs (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_csrrc (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_ecall (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_mret  (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_ebreak(IR_Inst *ir, CPU_state *cpu);
static void ir_exec_fence_i(IR_Inst *ir, CPU_state *cpu);
static void ir_exec_fence  (IR_Inst *ir, CPU_state *cpu);
static void ir_exec_inv   (IR_Inst *ir, CPU_state *cpu);

/* ── ir_fill_operands ───────────────────────────────────────────────────── */
/*
 * Mirrors decode_operand() from decode.c, but populates IR_Inst fields
 * instead of raw local variables.  Register values are read from the global
 * `cpu` state (same as the functional simulator).
 *
 * Conventions (same as the old local variables in decode_exec):
 *   src1_val  ← old src1
 *   src2_val  ← old src2
 *   imm       ← old dest for B/S types (branch offset / store offset)
 *   rd        ← destination register index (–1 for S/B/N types)
 */
static void ir_fill_operands(IR_Inst *ir, uint32_t i, int type)
{
    ir->rd  = BITS(i, 11,  7);
    ir->rs1 = BITS(i, 19, 15);
    ir->rs2 = BITS(i, 24, 20);

    switch (type) {
    case TYPE_I:
        ir->src1_val = cpu.gpr[ir->rs1];
        ir->src2_val = immI(i);
        ir->rs2      = -1;   /* rs2 field is part of imm for I-type, not a reg source */
        break;
    case TYPE_U:
        ir->src1_val = immU(i);
        ir->rs1      = -1;
        ir->rs2      = -1;
        break;
    case TYPE_S:
        ir->rd       = -1;           /* stores have no register destination */
        ir->imm      = immS(i);      /* store address offset                */
        ir->src1_val = cpu.gpr[ir->rs1];
        ir->src2_val = cpu.gpr[ir->rs2];
        break;
    case TYPE_J:
        ir->src1_val = immJ(i);      /* jump offset                         */
        ir->rs1      = -1;
        ir->rs2      = -1;
        break;
    case TYPE_R:
        ir->src1_val = cpu.gpr[ir->rs1];
        ir->src2_val = cpu.gpr[ir->rs2];
        break;
    case TYPE_B:
        ir->rd       = -1;           /* branches have no register destination */
        ir->src1_val = cpu.gpr[ir->rs1];
        ir->src2_val = cpu.gpr[ir->rs2];
        ir->imm      = immB(i);      /* branch offset                        */
        break;
    case TYPE_N:
        ir->rd  = -1;
        ir->rs1 = -1;
        ir->rs2 = -1;
        break;
    default:
        ir->rd  = -1;
        ir->rs1 = -1;
        ir->rs2 = -1;
        break;
    }
}

/* ── ir_decode ──────────────────────────────────────────────────────────── */
/*
 * Decode `raw` into `*ir`.  The instruction pattern table from
 * include/cpu/inst_table.inc is included with locally-redefined macros so
 * that INSTPAT_MATCH fills IR_Inst fields and sets exec_fn, without running
 * the execution body (__VA_ARGS__ is captured but not expanded here).
 */

void ir_decode(uint32_t raw, vaddr_t pc, vaddr_t snpc, IR_Inst *ir)
{
    ir->pc              = pc;
    ir->snpc            = snpc;
    ir->raw             = raw;
    ir->type            = ITYPE_INVALID;
    ir->name            = "???";
    ir->rd              = -1;
    ir->rs1             = -1;
    ir->rs2             = -1;
    ir->dnpc            = snpc;
    ir->taken           = 0;
    ir->mem_addr        = 0;
    ir->mem_width       = 0;
    ir->serializing     = 0;
    ir->fault           = 0;
    ir->phys_rd         = -1;
    ir->rob_idx         = -1;
    ir->cycles_rem      = 0;
    ir->exec_fn         = ir_exec_inv;

    /* Direct opcode dispatch — O(1) vs the original O(n) linear INSTPAT scan.
     * opcode = bits[6:2] (bits[1:0] are always 11 for RV32/64 instructions).
     * Within each opcode group, funct3 (bits[14:12]) and funct7 (bits[31:25])
     * disambiguate. */
    uint8_t opcode = (raw >> 2) & 0x1f;
    uint8_t funct3 = (raw >> 12) & 0x7;
    uint8_t funct7 = (raw >> 25) & 0x7f;

    switch (opcode) {

    case 0x04: /* I-type ALU: addi ori xori andi slti sltiu slli srli srai */
        ir_fill_operands(ir, raw, TYPE_I);
        switch (funct3) {
        case 0: ir->name = "addi";  ir->exec_fn = ir_exec_addi;  break;
        case 6: ir->name = "ori";   ir->exec_fn = ir_exec_ori;   break;
        case 4: ir->name = "xori";  ir->exec_fn = ir_exec_xori;  break;
        case 7: ir->name = "andi";  ir->exec_fn = ir_exec_andi;  break;
        case 2: ir->name = "slti";  ir->exec_fn = ir_exec_slti;  break;
        case 3: ir->name = "sltiu"; ir->exec_fn = ir_exec_sltiu; break;
        case 1: ir->name = "slli";  ir->exec_fn = ir_exec_slli;  break;
        case 5:
            if ((funct7 >> 1) == 0x20) { ir->name = "srai"; ir->exec_fn = ir_exec_srai; }
            else                        { ir->name = "srli"; ir->exec_fn = ir_exec_srli; }
            break;
        default: break;
        }
        break;

    case 0x00: /* Loads: lb lh lw ld lbu lhu lwu */
        ir_fill_operands(ir, raw, TYPE_I);
        switch (funct3) {
        case 4: ir->name = "lbu"; ir->exec_fn = ir_exec_lbu; break;
        case 0: ir->name = "lb";  ir->exec_fn = ir_exec_lb;  break;
        case 2: ir->name = "lw";  ir->exec_fn = ir_exec_lw;  break;
        case 3: ir->name = "ld";  ir->exec_fn = ir_exec_ld;  break;
        case 1: ir->name = "lh";  ir->exec_fn = ir_exec_lh;  break;
        case 5: ir->name = "lhu"; ir->exec_fn = ir_exec_lhu; break;
        case 6: ir->name = "lwu"; ir->exec_fn = ir_exec_lwu; break;
        default: break;
        }
        break;

    case 0x08: /* Stores: sb sh sw sd */
        ir_fill_operands(ir, raw, TYPE_S);
        switch (funct3) {
        case 0: ir->name = "sb"; ir->exec_fn = ir_exec_sb; break;
        case 1: ir->name = "sh"; ir->exec_fn = ir_exec_sh; break;
        case 2: ir->name = "sw"; ir->exec_fn = ir_exec_sw; break;
        case 3: ir->name = "sd"; ir->exec_fn = ir_exec_sd; break;
        default: break;
        }
        break;

    case 0x18: /* Branches: beq bne blt bge bltu bgeu */
        ir_fill_operands(ir, raw, TYPE_B);
        switch (funct3) {
        case 0: ir->name = "beq";  ir->exec_fn = ir_exec_beq;  break;
        case 1: ir->name = "bne";  ir->exec_fn = ir_exec_bne;  break;
        case 4: ir->name = "blt";  ir->exec_fn = ir_exec_blt;  break;
        case 5: ir->name = "bge";  ir->exec_fn = ir_exec_bge;  break;
        case 6: ir->name = "bltu"; ir->exec_fn = ir_exec_bltu; break;
        case 7: ir->name = "bgeu"; ir->exec_fn = ir_exec_bgeu; break;
        default: break;
        }
        break;

    case 0x1b: /* JAL */
        ir_fill_operands(ir, raw, TYPE_J);
        ir->name = "jal"; ir->exec_fn = ir_exec_jal;
        break;

    case 0x19: /* JALR */
        ir_fill_operands(ir, raw, TYPE_I);
        ir->name = "jalr"; ir->exec_fn = ir_exec_jalr;
        break;

    case 0x05: /* AUIPC */
        ir_fill_operands(ir, raw, TYPE_U);
        ir->name = "auipc"; ir->exec_fn = ir_exec_auipc;
        break;

    case 0x0d: /* LUI */
        ir_fill_operands(ir, raw, TYPE_U);
        ir->name = "lui"; ir->exec_fn = ir_exec_lui;
        break;

    case 0x06: /* I-type W: addiw slliw srliw sraiw */
        ir_fill_operands(ir, raw, TYPE_I);
        switch (funct3) {
        case 0: ir->name = "addiw"; ir->exec_fn = ir_exec_addiw; break;
        case 1: ir->name = "slliw"; ir->exec_fn = ir_exec_slliw; break;
        case 5:
            if (funct7 == 0x20) { ir->name = "sraiw"; ir->exec_fn = ir_exec_sraiw; }
            else                 { ir->name = "srliw"; ir->exec_fn = ir_exec_srliw; }
            break;
        default: break;
        }
        break;

    case 0x0c: /* R-type: add sub mul or xor and slt sltu sll srl sra div divu rem remu */
        ir_fill_operands(ir, raw, TYPE_R);
        switch (funct3) {
        case 0:
            if      (funct7 == 0x00) { ir->name = "add";  ir->exec_fn = ir_exec_add;  }
            else if (funct7 == 0x20) { ir->name = "sub";  ir->exec_fn = ir_exec_sub;  }
            else if (funct7 == 0x01) { ir->name = "mul";  ir->exec_fn = ir_exec_mul;  }
            break;
        case 6:
            if      (funct7 == 0x00) { ir->name = "or";   ir->exec_fn = ir_exec_or;   }
            else if (funct7 == 0x01) { ir->name = "rem";  ir->exec_fn = ir_exec_rem;  }
            break;
        case 4:
            if      (funct7 == 0x00) { ir->name = "xor";  ir->exec_fn = ir_exec_xor;  }
            else if (funct7 == 0x01) { ir->name = "div";  ir->exec_fn = ir_exec_div;  }
            break;
        case 7:
            if      (funct7 == 0x00) { ir->name = "and";  ir->exec_fn = ir_exec_and;  }
            else if (funct7 == 0x01) { ir->name = "remu"; ir->exec_fn = ir_exec_remu; }
            break;
        case 2:
            if      (funct7 == 0x00) { ir->name = "slt";  ir->exec_fn = ir_exec_slt;  }
            break;
        case 3:
            if      (funct7 == 0x00) { ir->name = "sltu"; ir->exec_fn = ir_exec_sltu; }
            else if (funct7 == 0x01) { ir->name = "divu"; ir->exec_fn = ir_exec_divu; }
            break;
        case 1:
            if      (funct7 == 0x00) { ir->name = "sll";  ir->exec_fn = ir_exec_sll;  }
            break;
        case 5:
            if      (funct7 == 0x00) { ir->name = "srl";  ir->exec_fn = ir_exec_srl;  }
            else if (funct7 == 0x20) { ir->name = "sra";  ir->exec_fn = ir_exec_sra;  }
            else if (funct7 == 0x01) { ir->name = "divu"; ir->exec_fn = ir_exec_divu; }
            break;
        default: break;
        }
        break;

    case 0x0e: /* R-type W: addw subw mulw sllw srlw sraw divw divuw remw remuw */
        ir_fill_operands(ir, raw, TYPE_R);
        switch (funct3) {
        case 0:
            if      (funct7 == 0x00) { ir->name = "addw";  ir->exec_fn = ir_exec_addw;  }
            else if (funct7 == 0x20) { ir->name = "subw";  ir->exec_fn = ir_exec_subw;  }
            else if (funct7 == 0x01) { ir->name = "mulw";  ir->exec_fn = ir_exec_mulw;  }
            break;
        case 1:
            if      (funct7 == 0x00) { ir->name = "sllw";  ir->exec_fn = ir_exec_sllw;  }
            break;
        case 5:
            if      (funct7 == 0x00) { ir->name = "srlw";  ir->exec_fn = ir_exec_srlw;  }
            else if (funct7 == 0x20) { ir->name = "sraw";  ir->exec_fn = ir_exec_sraw;  }
            else if (funct7 == 0x01) { ir->name = "divuw"; ir->exec_fn = ir_exec_divuw; }
            break;
        case 4:
            if      (funct7 == 0x01) { ir->name = "divw";  ir->exec_fn = ir_exec_divw;  }
            break;
        case 6:
            if      (funct7 == 0x01) { ir->name = "remw";  ir->exec_fn = ir_exec_remw;  }
            break;
        case 7:
            if      (funct7 == 0x01) { ir->name = "remuw"; ir->exec_fn = ir_exec_remuw; }
            break;
        default: break;
        }
        break;

    case 0x1c: /* System: csrrw csrrs csrrc ecall mret ebreak */
        switch (funct3) {
        case 1: ir_fill_operands(ir, raw, TYPE_I); ir->name = "csrrw";  ir->exec_fn = ir_exec_csrrw;  ir->serializing = 1; break;
        case 2: ir_fill_operands(ir, raw, TYPE_I); ir->name = "csrrs";  ir->exec_fn = ir_exec_csrrs;  ir->serializing = 1; break;
        case 3: ir_fill_operands(ir, raw, TYPE_I); ir->name = "csrrc";  ir->exec_fn = ir_exec_csrrc;  ir->serializing = 1; break;
        case 0:
            ir_fill_operands(ir, raw, TYPE_N);
            if      (raw == 0x00000073) { ir->name = "ecall";  ir->exec_fn = ir_exec_ecall;  ir->serializing = 1; }
            else if (raw == 0x30200073) { ir->name = "mret";   ir->exec_fn = ir_exec_mret;   ir->serializing = 1; }
            else if (raw == 0x00100073) { ir->name = "ebreak"; ir->exec_fn = ir_exec_ebreak; ir->serializing = 1; }
            break;
        default: break;
        }
        break;

    case 0x03: /* Fence */
        ir_fill_operands(ir, raw, TYPE_N);
        ir->name = "fence"; ir->exec_fn = ir_exec_fence; ir->serializing = 1;
        break;

    default:
        break;
    }
}

/* ── ir_execute ─────────────────────────────────────────────────────────── */

void ir_execute(IR_Inst *ir, CPU_state *cpu)
{
    if (ir->exec_fn)
        ir->exec_fn(ir, cpu);
}

/* ── ir_writeback ───────────────────────────────────────────────────────── */

void ir_writeback(IR_Inst *ir, CPU_state *cpu)
{
    if (ir->rd != -1)
        cpu->gpr[ir->rd] = ir->result;
    cpu->gpr[0] = 0;   /* x0 is always zero */
}

/* ── ir_mem_access ──────────────────────────────────────────────────────── */
/*
 * MEM stage: perform the actual memory read or write deferred from EX.
 * For ITYPE_LOAD: reads mem_width bytes at mem_addr, applies sign/zero
 *                 extension, and writes the result into ir->result.
 * For ITYPE_STORE: writes src2_val to mem_addr with width mem_width.
 * All other instruction types are no-ops.
 */
void ir_mem_access(IR_Inst *ir, int *out_lat)
{
    if (ir->type == ITYPE_LOAD) {
        word_t raw;
        if (out_lat) {
            /* OOO mode: probe cache level for pipeline latency accounting */
            static const int level_to_lat[3] = { LAT_L1_HIT, LAT_L2_HIT, LAT_DRAM };
            int level = vaddr_read_level(ir->mem_addr, ir->mem_width, &raw);
            *out_lat = level_to_lat[level];
        } else {
            raw = vaddr_read(ir->mem_addr, ir->mem_width);
        }
        if (ir->mem_sign) {
            /* Signed load: sign-extend to 64 bits */
            switch (ir->mem_width) {
            case 1: ir->result = SEXT(raw,  8); break;
            case 2: ir->result = SEXT(raw, 16); break;
            case 4: ir->result = SEXT(raw, 32); break;
            default: ir->result = raw; break;   /* 8-byte: no extension */
            }
        } else {
            /* Unsigned/full load: zero-extend to 64 bits */
            switch (ir->mem_width) {
            case 1: ir->result = UEXT(raw,  8); break;
            case 2: ir->result = UEXT(raw, 16); break;
            case 4: ir->result = UEXT(raw, 32); break;
            default: ir->result = raw; break;   /* 8-byte: no extension */
            }
        }
    } else if (ir->type == ITYPE_STORE) {
        vaddr_write(ir->mem_addr, ir->mem_width, ir->src2_val);
        if (out_lat) *out_lat = LAT_L1_HIT;   /* Stores: assume L1 hit for now */
    } else {
        if (out_lat) *out_lat = 0;
    }
    /* ALU/branch/jump/system: nothing to do */
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Per-instruction executor functions
 *
 * Each function mirrors the body of the corresponding INSTPAT in the old
 * decode_exec(), re-expressed in terms of IR_Inst fields:
 *
 *   old  src1    → ir->src1_val
 *   old  src2    → ir->src2_val
 *   old  dest    → ir->rd  (for R/I/U/J types)
 *                  ir->imm (for B/S types — branch/store offset)
 *   old  s->pc   → ir->pc
 *   old  s->snpc → ir->snpc
 *   old  s->dnpc → ir->dnpc  (written by exec_fn, read by exec_once)
 * ══════════════════════════════════════════════════════════════════════════*/

/* ── U / J type ─────────────────────────────────────────── */

static void ir_exec_auipc(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_AUIPC;
    ir->result = ir->src1_val + ir->pc;
    ir->dnpc   = ir->snpc;
}

static void ir_exec_lui(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_LUI;
    /* immU already shifts by 12; lower 12 bits are zero — mask is redundant
     * but preserved for exact parity with the original. */
    ir->result = ir->src1_val & (word_t)0xfffffffffffff000ULL;
    ir->dnpc   = ir->snpc;
}

/* ── ALU R-type ──────────────────────────────────────────── */

static void ir_exec_add(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_ALU;
    ir->result = ir->src1_val + ir->src2_val;
    ir->dnpc   = ir->snpc;
}

static void ir_exec_mul(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_ALU;
    ir->result = ir->src1_val * ir->src2_val;
    ir->dnpc   = ir->snpc;
}

static void ir_exec_sub(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_ALU;
    ir->result = ir->src1_val - ir->src2_val;
    ir->dnpc   = ir->snpc;
}

static void ir_exec_or(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_ALU;
    ir->result = ir->src1_val | ir->src2_val;
    ir->dnpc   = ir->snpc;
}

static void ir_exec_xor(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_ALU;
    ir->result = ir->src1_val ^ ir->src2_val;
    ir->dnpc   = ir->snpc;
}

static void ir_exec_and(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_ALU;
    ir->result = ir->src1_val & ir->src2_val;
    ir->dnpc   = ir->snpc;
}

static void ir_exec_slt(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_ALU;
    ir->result = ((int64_t)ir->src1_val < (int64_t)ir->src2_val) ? 1 : 0;
    ir->dnpc   = ir->snpc;
}

static void ir_exec_sltu(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_ALU;
    ir->result = (ir->src1_val < ir->src2_val) ? 1 : 0;
    ir->dnpc   = ir->snpc;
}

static void ir_exec_sll(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_ALU;
    ir->result = ir->src1_val << (ir->src2_val & 0x3f);
    ir->dnpc   = ir->snpc;
}

static void ir_exec_srl(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_ALU;
    ir->result = ir->src1_val >> (ir->src2_val & 0x3f);
    ir->dnpc   = ir->snpc;
}

static void ir_exec_sra(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_ALU;
    ir->result = (word_t)((int64_t)ir->src1_val >> (ir->src2_val & 0x3f));
    ir->dnpc   = ir->snpc;
}

static void ir_exec_div(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_ALU;
    ir->result = (word_t)((int64_t)ir->src1_val / (int64_t)ir->src2_val);
    ir->dnpc   = ir->snpc;
}

static void ir_exec_divu(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_ALU;
    ir->result = ir->src1_val / ir->src2_val;
    ir->dnpc   = ir->snpc;
}

static void ir_exec_rem(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_ALU;
    ir->result = (word_t)((int64_t)ir->src1_val % (int64_t)ir->src2_val);
    ir->dnpc   = ir->snpc;
}

static void ir_exec_remu(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_ALU;
    ir->result = ir->src1_val % ir->src2_val;
    ir->dnpc   = ir->snpc;
}

/* ── ALU W (32-bit) R-type ───────────────────────────────── */

static void ir_exec_addw(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_ALU;
    ir->result = SEXT((ir->src1_val + ir->src2_val) & 0xffffffff, 32);
    ir->dnpc   = ir->snpc;
}

static void ir_exec_mulw(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_ALU;
    ir->result = SEXT((word_t)((int32_t)(ir->src1_val & 0xffffffff)
                               * (int32_t)(ir->src2_val & 0xffffffff)), 32);
    ir->dnpc   = ir->snpc;
}

static void ir_exec_subw(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_ALU;
    ir->result = SEXT((ir->src1_val - ir->src2_val) & 0xffffffff, 32);
    ir->dnpc   = ir->snpc;
}

static void ir_exec_sllw(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_ALU;
    ir->result = SEXT(((ir->src1_val & 0xffffffff) << (ir->src2_val & 0x1f))
                       & 0xffffffff, 32);
    ir->dnpc   = ir->snpc;
}

static void ir_exec_srlw(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_ALU;
    ir->result = SEXT(((ir->src1_val & 0xffffffff) >> (ir->src2_val & 0x1f))
                       & 0xffffffff, 32);
    ir->dnpc   = ir->snpc;
}

static void ir_exec_sraw(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_ALU;
    ir->result = SEXT((word_t)((int32_t)(ir->src1_val & 0xffffffff)
                               >> (ir->src2_val & 0x1f)), 32);
    ir->dnpc   = ir->snpc;
}

static void ir_exec_divw(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_ALU;
    ir->result = SEXT((word_t)((int32_t)(ir->src1_val & 0xffffffff)
                               / (int32_t)(ir->src2_val & 0xffffffff)), 32);
    ir->dnpc   = ir->snpc;
}

static void ir_exec_divuw(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_ALU;
    ir->result = SEXT((ir->src1_val & 0xffffffff) / (ir->src2_val & 0xffffffff),
                      32);
    ir->dnpc   = ir->snpc;
}

static void ir_exec_remw(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_ALU;
    ir->result = SEXT((word_t)((int32_t)(ir->src1_val & 0xffffffff)
                               % (int32_t)(ir->src2_val & 0xffffffff))
                       & 0xffffffff, 32);
    ir->dnpc   = ir->snpc;
}

static void ir_exec_remuw(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_ALU;
    ir->result = SEXT(((ir->src1_val & 0xffffffff) % (ir->src2_val & 0xffffffff))
                       & 0xffffffff, 32);
    ir->dnpc   = ir->snpc;
}

/* ── ALU I-type ──────────────────────────────────────────── */

static void ir_exec_addi(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_ALU;
    ir->result = ir->src1_val + ir->src2_val;
    ir->dnpc   = ir->snpc;
}

static void ir_exec_ori(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_ALU;
    ir->result = ir->src1_val | ir->src2_val;
    ir->dnpc   = ir->snpc;
}

static void ir_exec_xori(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_ALU;
    ir->result = ir->src1_val ^ ir->src2_val;
    ir->dnpc   = ir->snpc;
}

static void ir_exec_andi(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_ALU;
    ir->result = ir->src1_val & ir->src2_val;
    ir->dnpc   = ir->snpc;
}

static void ir_exec_slti(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_ALU;
    ir->result = ((int64_t)ir->src1_val < (int64_t)ir->src2_val) ? 1 : 0;
    ir->dnpc   = ir->snpc;
}

static void ir_exec_sltiu(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_ALU;
    ir->result = (ir->src1_val < ir->src2_val) ? 1 : 0;
    ir->dnpc   = ir->snpc;
}

static void ir_exec_slli(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_ALU;
    ir->result = ir->src1_val << ir->src2_val;
    ir->dnpc   = ir->snpc;
}

static void ir_exec_srli(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_ALU;
    ir->result = ir->src1_val >> ir->src2_val;
    ir->dnpc   = ir->snpc;
}

static void ir_exec_srai(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_ALU;
    ir->result = (word_t)((int64_t)ir->src1_val >> (ir->src2_val & 0x3f));
    ir->dnpc   = ir->snpc;
}

/* ── ALU IW (32-bit) I-type ──────────────────────────────── */

static void ir_exec_addiw(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_ALU;
    ir->result = SEXT((ir->src1_val + ir->src2_val) & 0xffffffff, 32);
    ir->dnpc   = ir->snpc;
}

static void ir_exec_slliw(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_ALU;
    ir->result = SEXT(((ir->src1_val & 0xffffffff) << ir->src2_val) & 0xffffffff,
                      32);
    ir->dnpc   = ir->snpc;
}

static void ir_exec_srliw(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_ALU;
    ir->result = SEXT(((ir->src1_val & 0xffffffff) >> ir->src2_val) & 0xffffffff,
                      32);
    ir->dnpc   = ir->snpc;
}

static void ir_exec_sraiw(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_ALU;
    ir->result = SEXT((word_t)((int32_t)(ir->src1_val & 0xffffffff)
                               >> (ir->src2_val & 0x1f)), 32);
    ir->dnpc   = ir->snpc;
}

/* ── Control flow ────────────────────────────────────────── */

static void ir_exec_jal(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_JAL;
    ir->result = ir->snpc;                   /* return address */
    ir->dnpc   = ir->pc + ir->src1_val;      /* jump target    */
    if (ir->rd == 1 || ir->rd == 5)
        add_ftrace(ir->dnpc, 1);
}

static void ir_exec_jalr(IR_Inst *ir, CPU_state *cpu)
{
    ir->type   = ITYPE_JALR;
    ir->result = ir->snpc;
    ir->dnpc   = (ir->src1_val + ir->src2_val) & ~(vaddr_t)1;
    /* ftrace: ret = jalr x0, ra, 0 */
    if (ir->rd == 0 && ir->rs1 == 1)
        add_ftrace(ir->dnpc, 0);
    else if (ir->rd == 1 || ir->rd == 5)
        add_ftrace(ir->dnpc, 1);
}

/* ── Branches ────────────────────────────────────────────── */

static void ir_exec_beq(IR_Inst *ir, CPU_state *cpu)
{
    ir->type  = ITYPE_BRANCH;
    ir->taken = (ir->src1_val == ir->src2_val);
    ir->dnpc  = ir->taken ? ir->pc + ir->imm : ir->snpc;
}

static void ir_exec_bne(IR_Inst *ir, CPU_state *cpu)
{
    ir->type  = ITYPE_BRANCH;
    ir->taken = (ir->src1_val != ir->src2_val);
    ir->dnpc  = ir->taken ? ir->pc + ir->imm : ir->snpc;
}

static void ir_exec_blt(IR_Inst *ir, CPU_state *cpu)
{
    ir->type  = ITYPE_BRANCH;
    ir->taken = ((int64_t)ir->src1_val < (int64_t)ir->src2_val);
    ir->dnpc  = ir->taken ? ir->pc + ir->imm : ir->snpc;
}

static void ir_exec_bge(IR_Inst *ir, CPU_state *cpu)
{
    ir->type  = ITYPE_BRANCH;
    ir->taken = ((int64_t)ir->src1_val >= (int64_t)ir->src2_val);
    ir->dnpc  = ir->taken ? ir->pc + ir->imm : ir->snpc;
}

static void ir_exec_bltu(IR_Inst *ir, CPU_state *cpu)
{
    ir->type  = ITYPE_BRANCH;
    ir->taken = (ir->src1_val < ir->src2_val);
    ir->dnpc  = ir->taken ? ir->pc + ir->imm : ir->snpc;
}

static void ir_exec_bgeu(IR_Inst *ir, CPU_state *cpu)
{
    ir->type  = ITYPE_BRANCH;
    ir->taken = (ir->src1_val >= ir->src2_val);
    ir->dnpc  = ir->taken ? ir->pc + ir->imm : ir->snpc;
}

/* ── Loads ───────────────────────────────────────────────── */

static void ir_exec_ld(IR_Inst *ir, CPU_state *cpu)
{
    ir->type      = ITYPE_LOAD;
    ir->mem_addr  = ir->src1_val + ir->src2_val;
    ir->mem_width = 8;
    ir->mem_sign  = 0;   /* no extension needed for 64-bit load */
    ir->dnpc      = ir->snpc;
}

static void ir_exec_lw(IR_Inst *ir, CPU_state *cpu)
{
    ir->type      = ITYPE_LOAD;
    ir->mem_addr  = ir->src1_val + ir->src2_val;
    ir->mem_width = 4;
    ir->mem_sign  = 1;   /* sign-extend 32→64 */
    ir->dnpc      = ir->snpc;
}

static void ir_exec_lh(IR_Inst *ir, CPU_state *cpu)
{
    ir->type      = ITYPE_LOAD;
    ir->mem_addr  = ir->src1_val + ir->src2_val;
    ir->mem_width = 2;
    ir->mem_sign  = 1;
    ir->dnpc      = ir->snpc;
}

static void ir_exec_lb(IR_Inst *ir, CPU_state *cpu)
{
    ir->type      = ITYPE_LOAD;
    ir->mem_addr  = ir->src1_val + ir->src2_val;
    ir->mem_width = 1;
    ir->mem_sign  = 1;
    ir->dnpc      = ir->snpc;
}

static void ir_exec_lwu(IR_Inst *ir, CPU_state *cpu)
{
    ir->type      = ITYPE_LOAD;
    ir->mem_addr  = ir->src1_val + ir->src2_val;
    ir->mem_width = 4;
    ir->mem_sign  = 0;   /* zero-extend */
    ir->dnpc      = ir->snpc;
}

static void ir_exec_lhu(IR_Inst *ir, CPU_state *cpu)
{
    ir->type      = ITYPE_LOAD;
    ir->mem_addr  = ir->src1_val + ir->src2_val;
    ir->mem_width = 2;
    ir->mem_sign  = 0;
    ir->dnpc      = ir->snpc;
}

static void ir_exec_lbu(IR_Inst *ir, CPU_state *cpu)
{
    ir->type      = ITYPE_LOAD;
    ir->mem_addr  = ir->src1_val + ir->src2_val;
    ir->mem_width = 1;
    ir->mem_sign  = 0;
    ir->dnpc      = ir->snpc;
}

/* ── Stores ──────────────────────────────────────────────── */

static void ir_exec_sd(IR_Inst *ir, CPU_state *cpu)
{
    ir->type      = ITYPE_STORE;
    ir->mem_addr  = ir->src1_val + ir->imm;
    ir->mem_width = 8;
    ir->dnpc      = ir->snpc;
}

static void ir_exec_sw(IR_Inst *ir, CPU_state *cpu)
{
    ir->type      = ITYPE_STORE;
    ir->mem_addr  = ir->src1_val + ir->imm;
    ir->mem_width = 4;
    ir->dnpc      = ir->snpc;
}

static void ir_exec_sh(IR_Inst *ir, CPU_state *cpu)
{
    ir->type      = ITYPE_STORE;
    ir->mem_addr  = ir->src1_val + ir->imm;
    ir->mem_width = 2;
    ir->dnpc      = ir->snpc;
}

static void ir_exec_sb(IR_Inst *ir, CPU_state *cpu)
{
    ir->type      = ITYPE_STORE;
    ir->mem_addr  = ir->src1_val + ir->imm;
    ir->mem_width = 1;
    ir->dnpc      = ir->snpc;
}

/* ── CSR operations ──────────────────────────────────────── */

static void ir_exec_csrrw(IR_Inst *ir, CPU_state *cpu)
{
    ir->type         = ITYPE_CSR;
    ir->serializing  = 1;
    uint32_t csr_no  = (uint32_t)(ir->src2_val & 0xfff);
    /* Preserve original behaviour: only read if src2 (CSR#) != 0 */
    if (ir->src2_val != 0)
        ir->result = read_csr(csr_no);
    set_csr(csr_no, ir->src1_val);
    ir->dnpc = ir->snpc;
}

static void ir_exec_csrrs(IR_Inst *ir, CPU_state *cpu)
{
    ir->type         = ITYPE_CSR;
    ir->serializing  = 1;
    uint32_t csr_no  = (uint32_t)(ir->src2_val & 0xfff);
    ir->result       = read_csr(csr_no);
    if (ir->src1_val != 0)
        set_csr(csr_no, ir->result | ir->src1_val);
    ir->dnpc = ir->snpc;
}

static void ir_exec_csrrc(IR_Inst *ir, CPU_state *cpu)
{
    ir->type         = ITYPE_CSR;
    ir->serializing  = 1;
    uint32_t csr_no  = (uint32_t)(ir->src2_val & 0xfff);
    ir->result       = read_csr(csr_no);
    if (ir->src1_val != 0)
        set_csr(csr_no, ir->result & ~ir->src1_val);
    ir->dnpc = ir->snpc;
}

/* ── System instructions ─────────────────────────────────── */

static void ir_exec_ecall(IR_Inst *ir, CPU_state *cpu)
{
    ir->type        = ITYPE_SYSTEM;
    ir->serializing = 1;
    ir->rd          = -1;   /* ecall writes no GPR */
    ir->dnpc        = raise_intr(11, ir->pc, cpu->gpr[17]);
    exception_priv_transfer();
}

static void ir_exec_mret(IR_Inst *ir, CPU_state *cpu)
{
    ir->type        = ITYPE_SYSTEM;
    ir->serializing = 1;
    ir->rd          = -1;
    ir->dnpc        = read_csr(MEPC & 0xfff);
    mret_priv_transfer();
}

static void ir_exec_ebreak(IR_Inst *ir, CPU_state *cpu)
{
    ir->type        = ITYPE_SYSTEM;
    ir->serializing = 1;
    ir->rd          = -1;
    state           = NEMU_END;
    ir->dnpc        = ir->snpc;
}

/* ── Misc ────────────────────────────────────────────────── */

static void ir_exec_fence_i(IR_Inst *ir, CPU_state *cpu)
{
    /* No real I-cache invalidation needed in this simulator */
    ir->type   = ITYPE_FENCE;
    ir->result = 0;
    ir->dnpc   = ir->snpc;
}

static void ir_exec_fence(IR_Inst *ir, CPU_state *cpu)
{
    /*
     * FENCE (data memory barrier).
     *
     * Correctness: vmem.c already uses eager write-invalidate snooping so all
     * stores are globally visible at commit time — the SC ordering guarantee
     * holds without any extra action here.
     *
     * OOO ordering: marking serializing=1 forces the OOO engine to drain the
     * ROB before this instruction enters, ensuring all preceding stores have
     * committed (and been snooped to other L1Ds) before any instruction after
     * the FENCE can issue.
     */
    ir->type        = ITYPE_FENCE;
    ir->serializing = 1;
    ir->rd          = -1;
    ir->result      = 0;
    ir->dnpc        = ir->snpc;
    (void)cpu;
}

static void ir_exec_inv(IR_Inst *ir, CPU_state *cpu)
{
    ir->type  = ITYPE_INVALID;
    ir->fault = 1;          /* deferred fault: OOO commit or in-order EX raises INV */
    ir->dnpc  = ir->snpc;   /* fallthrough PC so pipeline has a safe next-PC */
    (void)cpu;
}
