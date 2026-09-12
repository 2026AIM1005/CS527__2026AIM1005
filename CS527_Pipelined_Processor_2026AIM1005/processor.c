#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
#include "processor.h"
#include "memory.h"

int registers[NP][NUM_INT_REGS];
int vectors[NP][NUM_VECTOR_REGS][VECTOR_LANES];
int PC[NP];
int end_of_simulation[NP];

static int Z[NP], N[NP], C[NP], V[NP];
static FILE *fd_log[NP];

/*
 * A pipeline register contains the complete instruction identity plus the
 * values/calculation results needed by the following stage.
 */
typedef struct
{
    int valid;
    unsigned char op, d, s1, s2;
    int pc;
} PipeInst;

typedef struct
{
    int valid;
    unsigned char op, d, s1, s2;
    int pc;

    int32_t alu_result;
    int32_t store_value;
    int address;

    int32_t vec_value[VECTOR_LANES];

    int flag_valid;
    int flag_z, flag_n, flag_c, flag_v;

    int branch_taken;
    int branch_target;
    int halt;
} EXMEM;

typedef struct
{
    int valid;
    unsigned char op, d, s1, s2;
    int pc;

    int32_t result;
    int32_t store_value;
    int address;

    int vec_value[VECTOR_LANES];

    int flag_valid;
    int flag_z, flag_n, flag_c, flag_v;

    int halt;
} MEMWB;

typedef struct
{
    PipeInst ifid;
    PipeInst idex;
    EXMEM exmem;
    MEMWB memwb;

    int halt_seen;
    int faulted;
} PipelineState;

static PipelineState pipe_state[NP];

/* ---------- Utility: memory access through the Lab-5 MMU ---------- */

static int32_t read_word(int p, int addr)
{
    uint32_t u = 0;
    int i;

    if (addr < 0 || addr + 3 >= DATA_MEMORY_SIZE)
    {
        fprintf(stderr, "Processor %d: data read out of range: %d\n", p, addr);
        pipe_state[p].faulted = 1;
        return 0;
    }

    for (i = 0; i < 4; ++i)
    {
        int physical = getPhysicallAddress(p, 0, addr + i);
        u |= ((uint32_t)memory[physical]) << (8 * i);
    }

    return (int32_t)u;
}

static void write_word(int p, int addr, int32_t value)
{
    uint32_t u = (uint32_t)value;
    int i;

    if (addr < 0 || addr + 3 >= DATA_MEMORY_SIZE)
    {
        fprintf(stderr, "Processor %d: data write out of range: %d\n", p, addr);
        pipe_state[p].faulted = 1;
        return;
    }

    for (i = 0; i < 4; ++i)
    {
        int physical = getPhysicallAddress(p, 0, addr + i);
        memory[physical] = (unsigned char)((u >> (8 * i)) & 0xFF);
    }
}

static void set_flags_add_values(int32_t a, int32_t b, int32_t r,
                                 int *z, int *n, int *c, int *v)
{
    uint32_t ua = (uint32_t)a;
    uint32_t ur = (uint32_t)r;

    *z = (r == 0);
    *n = (r < 0);
    *c = (ur < ua);
    *v = (((a >= 0) && (b >= 0) && (r < 0)) ||
          ((a < 0) && (b < 0) && (r >= 0)));
}

static void set_flags_sub_values(int32_t a, int32_t b, int32_t r,
                                 int *z, int *n, int *c, int *v)
{
    *z = (r == 0);
    *n = (r < 0);
    *c = (a > b);
    *v = (((a < 0) != (b < 0)) && ((r < 0) == (b < 0)));
}

static int branch_taken(int p, int code)
{
    switch (code)
    {
    case 0: return 1; /* BAL */
    case 1: return Z[p]; /* BEQ */
    case 2: return !Z[p]; /* BNE */
    case 3: return N[p] == V[p]; /* BGE */
    case 4: return N[p] != V[p]; /* BLT */
    case 5: return !Z[p] && (N[p] == V[p]); /* BGT */
    case 6: return Z[p] || (N[p] != V[p]); /* BLE */
    default: return 0;
    }
}

/* ---------- Instruction classification for conservative hazard handling ---------- */

static int int_reads_reg(unsigned char op, unsigned char d,
                         unsigned char s1, unsigned char s2, int reg)
{
    (void)d;

    switch (op)
    {
    case 0x01: case 0x02: case 0x03: case 0x04:
        return s1 == reg || s2 == reg;
    case 0x05:
        return s1 != 0 && s1 == reg; /* 0 means legacy immediate address */
    case 0x06:
        return d == reg || (s1 != 0 && s1 == reg);
    case 0x08:
        return s2 == reg;
    case 0x09: case 0x0A: case 0x0B: case 0x0C:
        return s1 == reg;
    case 0x10: case 0x11: case 0x12: case 0x13:
    case 0x14: case 0x15: case 0x16:
        return 0; /* branch reads flags, not integer registers */
    case 0x21: case 0x22: case 0x23:
        return 0;
    case 0x25:
        return s1 == reg;
    case 0x26:
        return d == reg;
    case 0x2B:
        return s2 == reg;
    default:
        return 0;
    }
}

static int int_writes_reg(unsigned char op, unsigned char d)
{
    switch (op)
    {
    case 0x01: case 0x02: case 0x03: case 0x04:
    case 0x05:
    case 0x07:
    case 0x09: case 0x0A: case 0x0B: case 0x0C:
    case 0x0F:
        return d != 0;
    default:
        return 0;
    }
}

static int vec_reads_vec(unsigned char op, unsigned char d,
                         unsigned char s1, unsigned char s2, int vec)
{
    (void)d;

    switch (op)
    {
    case 0x21: case 0x22: case 0x23:
        return s1 == vec || s2 == vec;
    case 0x25:
        return 0;
    case 0x26:
        return s2 == vec;
    case 0x29: case 0x2A: case 0x2B:
        return s1 == vec;
    case 0x2C:
        return 0;
    case 0x2E:
        return s2 == vec;
    default:
        return 0;
    }
}

static int vec_writes_vec(unsigned char op, unsigned char d)
{
    switch (op)
    {
    case 0x21: case 0x22: case 0x23:
    case 0x25:
    case 0x29: case 0x2A: case 0x2B:
    case 0x2C:
        return d < NUM_VECTOR_REGS;
    default:
        return 0;
    }
}

static int reads_flags(unsigned char op)
{
    return op >= 0x10 && op <= 0x16;
}

static int writes_flags(unsigned char op)
{
    return op == 0x01 || op == 0x02 ||
           op == 0x09 || op == 0x0A;
}

static int producer_writes_int(const PipeInst *x, int reg)
{
    return x->valid && int_writes_reg(x->op, x->d) && x->d == reg;
}

static int producer_writes_int_exmem(const EXMEM *x, int reg)
{
    return x->valid && int_writes_reg(x->op, x->d) && x->d == reg;
}

static int producer_writes_int_memwb(const MEMWB *x, int reg)
{
    return x->valid && int_writes_reg(x->op, x->d) && x->d == reg;
}

static int producer_writes_vec_idex(const PipeInst *x, int vec)
{
    return x->valid && vec_writes_vec(x->op, x->d) && x->d == vec;
}

static int producer_writes_vec_exmem(const EXMEM *x, int vec)
{
    return x->valid && vec_writes_vec(x->op, x->d) && x->d == vec;
}

static int producer_writes_vec_memwb(const MEMWB *x, int vec)
{
    return x->valid && vec_writes_vec(x->op, x->d) && x->d == vec;
}

static int producer_writes_flags_idex(const PipeInst *x)
{
    return x->valid && writes_flags(x->op);
}

static int producer_writes_flags_exmem(const EXMEM *x)
{
    return x->valid && writes_flags(x->op);
}

static int producer_writes_flags_memwb(const MEMWB *x)
{
    return x->valid && writes_flags(x->op);
}

static int has_hazard(int p, const PipeInst *inst)
{
    int r;

    if (!inst->valid)
        return 0;

    /* Integer register dependencies. */
    for (r = 1; r < NUM_INT_REGS; ++r)
    {
        if (!int_reads_reg(inst->op, inst->d, inst->s1, inst->s2, r))
            continue;

        if (producer_writes_int(&pipe_state[p].idex, r) ||
            producer_writes_int_exmem(&pipe_state[p].exmem, r) ||
            producer_writes_int_memwb(&pipe_state[p].memwb, r))
            return 1;
    }

    /*
     * Vector dependencies.  There is no vector forwarding in this simple
     * educational pipeline, so a consumer waits until the producer reaches WB.
     */
    for (r = 0; r < NUM_VECTOR_REGS; ++r)
    {
        if (vec_reads_vec(inst->op, inst->d, inst->s1, inst->s2, r))
        {
            if (producer_writes_vec_idex(&pipe_state[p].idex, r) ||
                producer_writes_vec_exmem(&pipe_state[p].exmem, r) ||
                producer_writes_vec_memwb(&pipe_state[p].memwb, r))
                return 1;
        }
    }

    /*
     * Branches consume the global flags.  Wait for every older arithmetic
     * instruction that updates flags.
     */
    if (reads_flags(inst->op))
    {
        if (producer_writes_flags_idex(&pipe_state[p].idex) ||
            producer_writes_flags_exmem(&pipe_state[p].exmem) ||
            producer_writes_flags_memwb(&pipe_state[p].memwb))
            return 1;
    }

    return 0;
}

/* ---------- Five pipeline stages ---------- */

static int fetch_instruction(int p, int pc, PipeInst *out)
{
    int i;
    unsigned char b[4];

    if (pc < 0 || pc + 3 >= INSTRUCTION_MEMORY_SIZE)
        return 0;

    for (i = 0; i < 4; ++i)
    {
        int physical = getPhysicallAddress(p, 1, pc + i);
        b[i] = memory[physical];
    }

    out->valid = 1;
    out->op = b[0];
    out->d = b[1];
    out->s1 = b[2];
    out->s2 = b[3];
    out->pc = pc;
    return 1;
}

static void stage_wb(int p)
{
    MEMWB *w = &pipe_state[p].memwb;

    if (!w->valid)
        return;

    if (w->flag_valid)
    {
        Z[p] = w->flag_z;
        N[p] = w->flag_n;
        C[p] = w->flag_c;
        V[p] = w->flag_v;
    }

    switch (w->op)
    {
    case 0x01: case 0x02: case 0x03: case 0x04:
    case 0x05:
    case 0x07:
    case 0x09: case 0x0A: case 0x0B: case 0x0C:
    case 0x0F:
        if (w->d != 0)
            registers[p][w->d] = w->result;
        break;

    case 0x21: case 0x22: case 0x23:
    case 0x25:
    case 0x29: case 0x2A: case 0x2B:
    case 0x2C:
        memcpy(vectors[p][w->d], w->vec_value,
               sizeof(w->vec_value));
        break;

    case 0x08:
        if (fd_log[p])
            fprintf(fd_log[p], "Process id: x%u : %08X\n",
                    w->s2, (unsigned int)registers[p][w->s2]);
        break;

    default:
        break;
    }

    registers[p][0] = 0;
}

static MEMWB stage_mem(int p, const EXMEM *x)
{
    MEMWB w;
    int i;

    memset(&w, 0, sizeof(w));
    if (!x->valid)
        return w;

    w.valid = 1;
    w.op = x->op;
    w.d = x->d;
    w.s1 = x->s1;
    w.s2 = x->s2;
    w.pc = x->pc;
    w.result = x->alu_result;
    w.address = x->address;
    w.store_value = x->store_value;
    w.flag_valid = x->flag_valid;
    w.flag_z = x->flag_z;
    w.flag_n = x->flag_n;
    w.flag_c = x->flag_c;
    w.flag_v = x->flag_v;
    w.halt = x->halt;

    memcpy(w.vec_value, x->vec_value, sizeof(w.vec_value));

    switch (x->op)
    {
    case 0x05:
        w.result = read_word(p, x->address);
        break;

    case 0x06:
        write_word(p, x->address, x->store_value);
        break;

    case 0x0E:
        write_word(p, x->address, x->store_value);
        break;

    case 0x25:
        for (i = 0; i < VECTOR_LANES; ++i)
            w.vec_value[i] = read_word(p, x->address + i * 4);
        break;

    case 0x26:
        for (i = 0; i < VECTOR_LANES; ++i)
            write_word(p, x->address + i * 4,
                       vectors[p][x->s2][i]);
        break;

    case 0x2C:
        for (i = 0; i < VECTOR_LANES; ++i)
            w.vec_value[i] = read_word(p, x->address + i * 4);
        break;

    case 0x2E:
        for (i = 0; i < VECTOR_LANES; ++i)
            write_word(p, x->address + i * 4,
                       vectors[p][x->s2][i]);
        break;

    default:
        break;
    }

    return w;
}

static EXMEM stage_ex(int p, const PipeInst *in)
{
    EXMEM x;
    int i;
    int32_t a, b, r;

    memset(&x, 0, sizeof(x));
    if (!in->valid)
        return x;

    x.valid = 1;
    x.op = in->op;
    x.d = in->d;
    x.s1 = in->s1;
    x.s2 = in->s2;
    x.pc = in->pc;

    switch (in->op)
    {
    case 0x00:
        x.halt = 1;
        pipe_state[p].halt_seen = 1;
        break;

    case 0x01:
        a = registers[p][in->s1];
        b = registers[p][in->s2];
        r = a + b;
        x.alu_result = r;
        x.flag_valid = 1;
        set_flags_add_values(a, b, r,
                             &x.flag_z, &x.flag_n,
                             &x.flag_c, &x.flag_v);
        break;

    case 0x02:
        a = registers[p][in->s1];
        b = registers[p][in->s2];
        r = a - b;
        x.alu_result = r;
        x.flag_valid = 1;
        set_flags_sub_values(a, b, r,
                             &x.flag_z, &x.flag_n,
                             &x.flag_c, &x.flag_v);
        break;

    case 0x03:
        x.alu_result =
            registers[p][in->s1] * registers[p][in->s2];
        break;

    case 0x04:
        if (registers[p][in->s2] == 0)
        {
            fprintf(stderr, "Processor %d: division by zero at PC %d\n",
                    p, in->pc);
            pipe_state[p].faulted = 1;
            x.valid = 0;
            break;
        }
        x.alu_result =
            registers[p][in->s1] / registers[p][in->s2];
        break;

    case 0x05:
        x.address = (in->s1 == 0) ? (int)in->s2
                                  : registers[p][in->s1];
        break;

    case 0x06:
        if (in->s1 == 0)
        {
            /* Legacy: Write xD, address */
            x.address = (int)in->s2;
            x.store_value = registers[p][in->d];
        }
        else
        {
            /* Bracket form: [xD] = xS1 */
            x.address = registers[p][in->d];
            x.store_value = registers[p][in->s1];
        }
        break;

    case 0x07:
        x.alu_result = (int)in->s2;
        break;

    case 0x08:
        /* Print is completed in WB after all older writes have committed. */
        break;

    case 0x09:
        a = registers[p][in->s1];
        b = (int32_t)in->s2;
        r = a + b;
        x.alu_result = r;
        x.flag_valid = 1;
        set_flags_add_values(a, b, r,
                             &x.flag_z, &x.flag_n,
                             &x.flag_c, &x.flag_v);
        break;

    case 0x0A:
        a = registers[p][in->s1];
        b = (int32_t)in->s2;
        r = a - b;
        x.alu_result = r;
        x.flag_valid = 1;
        set_flags_sub_values(a, b, r,
                             &x.flag_z, &x.flag_n,
                             &x.flag_c, &x.flag_v);
        break;

    case 0x0B:
        x.alu_result =
            registers[p][in->s1] * (int32_t)in->s2;
        break;

    case 0x0C:
        if (in->s2 == 0)
        {
            fprintf(stderr, "Processor %d: division by zero at PC %d\n",
                    p, in->pc);
            pipe_state[p].faulted = 1;
            x.valid = 0;
            break;
        }
        x.alu_result =
            registers[p][in->s1] / (int32_t)in->s2;
        break;

    case 0x0E:
        /* Legacy immediate-address write. */
        x.address = (int)in->s1;
        x.store_value = registers[p][in->d];
        break;

    case 0x0F:
        x.alu_result = (int)in->s2;
        break;

    case 0x10: case 0x11: case 0x12: case 0x13:
    case 0x14: case 0x15: case 0x16:
    {
        int code = in->op - 0x10;
        int taken = branch_taken(p, code);

        x.branch_taken = taken;
        x.branch_target =
            in->pc + 4 + ((int8_t)in->s2) * 4;
        break;
    }

    case 0x21:
    case 0x22:
    case 0x23:
        for (i = 0; i < VECTOR_LANES; ++i)
        {
            if (in->op == 0x21)
                x.vec_value[i] =
                    vectors[p][in->s1][i] +
                    vectors[p][in->s2][i];
            else if (in->op == 0x22)
                x.vec_value[i] =
                    vectors[p][in->s1][i] -
                    vectors[p][in->s2][i];
            else
                x.vec_value[i] =
                    vectors[p][in->s1][i] *
                    vectors[p][in->s2][i];
        }
        break;

    case 0x25:
        x.address = registers[p][in->s1];
        break;

    case 0x26:
        x.address = registers[p][in->d];
        break;

    case 0x29:
        for (i = 0; i < VECTOR_LANES; ++i)
            x.vec_value[i] =
                vectors[p][in->s1][i] + (int)in->s2;
        break;

    case 0x2A:
        for (i = 0; i < VECTOR_LANES; ++i)
            x.vec_value[i] =
                vectors[p][in->s1][i] - (int)in->s2;
        break;

    case 0x2B:
        for (i = 0; i < VECTOR_LANES; ++i)
            x.vec_value[i] =
                vectors[p][in->s1][i] *
                registers[p][in->s2];
        break;

    case 0x2C:
        x.address = (int)in->s2;
        break;

    case 0x2E:
        x.address = (int)in->s1;
        break;

    default:
        fprintf(stderr, "Processor %d: unknown opcode %02X at PC %d\n",
                p, in->op, in->pc);
        pipe_state[p].faulted = 1;
        x.valid = 0;
        break;
    }

    return x;
}

void reset_processor(int p)
{
    if (p < 0 || p >= NP)
        return;

    memset(registers[p], 0, sizeof(registers[p]));
    memset(vectors[p], 0, sizeof(vectors[p]));

    PC[p] = 0;
    end_of_simulation[p] = 0;
    Z[p] = N[p] = C[p] = V[p] = 0;

    memset(&pipe_state[p], 0, sizeof(pipe_state[p]));

    if (fd_log[p])
        fclose(fd_log[p]);

    {
        char name[64];
        snprintf(name, sizeof(name), "processor_%d.log", p);
        fd_log[p] = fopen(name, "a");
    }
}

void close_processor_log(int p)
{
    if (p >= 0 && p < NP && fd_log[p])
    {
        fclose(fd_log[p]);
        fd_log[p] = NULL;
    }
}

static int pipeline_empty(int p)
{
    return !pipe_state[p].ifid.valid &&
           !pipe_state[p].idex.valid &&
           !pipe_state[p].exmem.valid &&
           !pipe_state[p].memwb.valid;
}

/*
 * Advance one pipeline clock:
 *
 *   WB(old MEM/WB)
 *   MEM(old EX/MEM)
 *   EX(old ID/EX)
 *   ID(old IF/ID)
 *   IF
 *
 * The implementation uses conservative stalling rather than forwarding.
 * This makes RAW dependencies easy to reason about and keeps the ISA
 * semantics identical to the non-pipelined Lab-5 processor.
 */
static void pipeline_cycle(int p)
{
    PipelineState *s = &pipe_state[p];
    MEMWB next_memwb;
    EXMEM next_exmem;
    PipeInst next_idex;
    PipeInst next_ifid;
    int branch_taken_now = 0;
    int branch_target = 0;
    int id_stall = 0;

    memset(&next_memwb, 0, sizeof(next_memwb));
    memset(&next_exmem, 0, sizeof(next_exmem));
    memset(&next_idex, 0, sizeof(next_idex));
    memset(&next_ifid, 0, sizeof(next_ifid));

    /* 1. WB */
    stage_wb(p);

    /* 2. MEM */
    next_memwb = stage_mem(p, &s->exmem);

    /* 3. EX */
    next_exmem = stage_ex(p, &s->idex);

    if (s->idex.valid &&
        s->idex.op >= 0x10 && s->idex.op <= 0x16)
    {
        branch_taken_now = next_exmem.branch_taken;
        branch_target = next_exmem.branch_target;
    }

    /* A HALT has reached EX. No younger instruction may be fetched. */
    if (next_exmem.halt)
        s->halt_seen = 1;

    /* 4. ID */
    if (s->ifid.valid)
    {
        id_stall = has_hazard(p, &s->ifid);

        if (!id_stall && !branch_taken_now && !next_exmem.halt)
            next_idex = s->ifid;
    }

    /*
     * 5. IF
     *
     * On a taken branch, discard both younger instructions and redirect PC.
     * We intentionally insert a bubble on the branch cycle.
     */
    if (branch_taken_now)
    {
        PC[p] = branch_target;
        memset(&next_ifid, 0, sizeof(next_ifid));
        memset(&next_idex, 0, sizeof(next_idex));
    }
    else if (next_exmem.halt || s->halt_seen)
    {
        memset(&next_ifid, 0, sizeof(next_ifid));
        if (id_stall)
            memset(&next_idex, 0, sizeof(next_idex));
    }
    else if (id_stall)
    {
        /* Hold IF/ID and PC; insert a bubble into ID/EX. */
        next_ifid = s->ifid;
        memset(&next_idex, 0, sizeof(next_idex));
    }
    else
    {
        if (fetch_instruction(p, PC[p], &next_ifid))
        {
            PC[p] += 4;

            /*
             * HALT is allowed into the pipeline, but prevents another
             * instruction from being fetched on the following cycle.
             */
            if (next_ifid.op == 0x00)
                s->halt_seen = 1;
        }
        else
        {
            fprintf(stderr, "Processor %d: PC out of range: %d\n", p, PC[p]);
            s->faulted = 1;
            memset(&next_ifid, 0, sizeof(next_ifid));
        }
    }

    s->memwb = next_memwb;
    s->exmem = next_exmem;
    s->idex = next_idex;
    s->ifid = next_ifid;

    if (s->faulted)
    {
        s->halt_seen = 1;
        if (pipeline_empty(p))
            end_of_simulation[p] = 1;
    }
    else if (s->halt_seen && pipeline_empty(p))
    {
        end_of_simulation[p] = 1;
    }
}

int process_instructions(int p, int instruction_count)
{
    int cycle;

    if (p < 0 || p >= NP || instruction_count < 0)
        return 0;

    if (end_of_simulation[p])
        return 1;

    for (cycle = 0;
         cycle < instruction_count && !end_of_simulation[p];
         ++cycle)
    {
        pipeline_cycle(p);
    }

#ifdef _WIN32
    {
        LARGE_INTEGER freq, start, now;
        if (QueryPerformanceFrequency(&freq) &&
            QueryPerformanceCounter(&start))
        {
            const LONGLONG ticks = freq.QuadPart / 100000;
            do {
                QueryPerformanceCounter(&now);
            } while ((now.QuadPart - start.QuadPart) < ticks);
        }
    }
#else
    {
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 10000;
        nanosleep(&ts, NULL);
    }
#endif

    return 1;
}
