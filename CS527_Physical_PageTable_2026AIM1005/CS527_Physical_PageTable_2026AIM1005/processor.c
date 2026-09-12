#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "processor.h"
#include "memory.h"

int registers[NP][NUM_INT_REGS];
int vectors[NP][NUM_VECTOR_REGS][VECTOR_LANES];
int PC[NP];
int end_of_simulation[NP];

static int Z[NP], N[NP], C[NP], V[NP];
static FILE *fd_log[NP];
static unsigned char opcode[NP], dest[NP], src1[NP], src2[NP];

static int32_t read_word(int p, int addr)
{
    uint32_t u = 0;
    int i;

    if (addr < 0 || addr + 3 >= DATA_MEMORY_SIZE)
        return 0;

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
        return;

    for (i = 0; i < 4; ++i)
    {
        int physical = getPhysicallAddress(p, 0, addr + i);
        memory[physical] = (unsigned char)((u >> (8 * i)) & 255);
    }
}

static void set_flags_add(int p, int32_t a, int32_t b, int32_t r)
{
    uint32_t ua = (uint32_t)a, ur = (uint32_t)r;
    Z[p] = (r == 0);
    N[p] = (r < 0);
    C[p] = (ur < ua);
    V[p] = (((a >= 0) && (b >= 0) && (r < 0)) ||
            ((a < 0) && (b < 0) && (r >= 0)));
}

static void set_flags_sub(int p, int32_t a, int32_t b, int32_t r)
{
    Z[p] = (r == 0);
    N[p] = (r < 0);
    C[p] = (a > b);
    V[p] = (((a < 0) != (b < 0)) && ((r < 0) == (b < 0)));
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

static void fetch(int p)
{
    int i;
    unsigned char *fields[4] = { &opcode[p], &dest[p], &src1[p], &src2[p] };

    for (i = 0; i < 4; ++i)
    {
        int physical = getPhysicallAddress(p, 1, PC[p] + i);
        *fields[i] = memory[physical];
    }
}

static void vector_load(int p, int d, int address)
{
    int i;
    for (i = 0; i < VECTOR_LANES; ++i)
        vectors[p][d][i] = read_word(p, address + i * 4);
}

static void vector_store(int p, int s, int address)
{
    int i;
    for (i = 0; i < VECTOR_LANES; ++i)
        write_word(p, address + i * 4, vectors[p][s][i]);
}

static void vector_binary(int p, int d, int s1, int s2, int op)
{
    int i;
    for (i = 0; i < VECTOR_LANES; ++i)
    {
        if (op == 0)
            vectors[p][d][i] = vectors[p][s1][i] + vectors[p][s2][i];
        else if (op == 1)
            vectors[p][d][i] = vectors[p][s1][i] - vectors[p][s2][i];
        else
            vectors[p][d][i] = vectors[p][s1][i] * vectors[p][s2][i];
    }
}

static void vector_scalar(int p, int d, int s, int value, int op)
{
    int i;
    for (i = 0; i < VECTOR_LANES; ++i)
    {
        if (op == 0)
            vectors[p][d][i] = vectors[p][s][i] + value;
        else if (op == 1)
            vectors[p][d][i] = vectors[p][s][i] - value;
        else
            vectors[p][d][i] = vectors[p][s][i] * value;
    }
}

static int branch_taken(int p, int code)
{
    switch (code)
    {
    case 0:
        return 1; /* BAL */
    case 1:
        return Z[p];
    case 2:
        return !Z[p];
    case 3:
        return N[p] == V[p];
    case 4:
        return N[p] != V[p];
    case 5:
        return !Z[p] && (N[p] == V[p]);
    case 6:
        return Z[p] || (N[p] != V[p]);
    default:
        return 0;
    }
}

static void execute(int p)
{
    int32_t a, b, r;
    switch (opcode[p])
    {
    case 0x00:
        end_of_simulation[p] = 1;
        break;
    case 0x01:
        a = registers[p][src1[p]];
        b = registers[p][src2[p]];
        r = a + b;
        registers[p][dest[p]] = r;
        set_flags_add(p, a, b, r);
        break;
    case 0x02:
        a = registers[p][src1[p]];
        b = registers[p][src2[p]];
        r = a - b;
        registers[p][dest[p]] = r;
        set_flags_sub(p, a, b, r);
        break;
    case 0x03:
        registers[p][dest[p]] = registers[p][src1[p]] * registers[p][src2[p]];
        break;
    case 0x04:
        if (!registers[p][src2[p]])
        {
            fprintf(stderr, "PID/processor %d: division by zero\n", p);
            end_of_simulation[p] = 1;
            break;
        }
        registers[p][dest[p]] = registers[p][src1[p]] / registers[p][src2[p]];
        break;
    case 0x05:
        {
            int address = (src1[p] == 0) ? (int)src2[p]
                                         : registers[p][src1[p]];
            registers[p][dest[p]] = read_word(p, address);
        }
        break;
    case 0x06:
        {
            int address, value;
            if (src1[p] == 0)
            {
                /* Legacy: Write xD, address */
                address = (int)src2[p];
                value = registers[p][dest[p]];
            }
            else
            {
                /* Bracket form: [xD] = xS1 */
                address = registers[p][dest[p]];
                value = registers[p][src1[p]];
            }
            write_word(p, address, value);
        }
        break;
    case 0x07:
        registers[p][dest[p]] = (int)src2[p];
        break;
    case 0x08:
        if (fd_log[p])
            fprintf(fd_log[p], "Process id: x%u : %08X\n", src2[p], (unsigned int)registers[p][src2[p]]);
        break;
    case 0x09:
        a = registers[p][src1[p]];
        b = src2[p];
        r = a + b;
        registers[p][dest[p]] = r;
        set_flags_add(p, a, b, r);
        break;
    case 0x0A:
        a = registers[p][src1[p]];
        b = src2[p];
        r = a - b;
        registers[p][dest[p]] = r;
        set_flags_sub(p, a, b, r);
        break;
    case 0x0B:
        registers[p][dest[p]] = registers[p][src1[p]] * (int)src2[p];
        break;
    case 0x0C:
        if (!src2[p])
        {
            fprintf(stderr, "PID/processor %d: division by zero\n", p);
            end_of_simulation[p] = 1;
            break;
        }
        registers[p][dest[p]] = registers[p][src1[p]] / (int)src2[p];
        break;
    case 0x0E:
        write_word(p, registers[p][dest[p]], (int32_t)src1[p]);
        break;
    case 0x0F:
        registers[p][dest[p]] = (int)src2[p];
        break;
    default:
        if (opcode[p] >= 0x10 && opcode[p] <= 0x16)
        {
            int code = opcode[p] - 0x10;
            if (branch_taken(p, code))
                PC[p] += ((int8_t)src2[p]) * 4;
        }
        else if (opcode[p] == 0x21)
            vector_binary(p, dest[p], src1[p], src2[p], 0);
        else if (opcode[p] == 0x22)
            vector_binary(p, dest[p], src1[p], src2[p], 1);
        else if (opcode[p] == 0x23)
            vector_binary(p, dest[p], src1[p], src2[p], 2);
        else if (opcode[p] == 0x25)
            vector_load(p, dest[p], registers[p][src1[p]]);
        else if (opcode[p] == 0x26)
            vector_store(p, src2[p], registers[p][dest[p]]);
        else if (opcode[p] == 0x29)
            vector_scalar(p, dest[p], src1[p], src2[p], 0);
        else if (opcode[p] == 0x2A)
            vector_scalar(p, dest[p], src1[p], src2[p], 1);
        else if (opcode[p] == 0x2B)
            vector_scalar(p, dest[p], src1[p], registers[p][src2[p]], 2);
        else if (opcode[p] == 0x2C)
            vector_load(p, dest[p], (int)src2[p]);
        else if (opcode[p] == 0x2E)
            vector_store(p, src2[p], (int)src1[p]);
        else
        {
            fprintf(stderr, "Processor %d: unknown opcode %02X at PC %d\n", p, opcode[p], PC[p] - 4);
            end_of_simulation[p] = 1;
        }
        break;
    }
    registers[p][0] = 0;
}

int process_instructions(int p, int instruction_count)
{
    int i;
    if (p < 0 || p >= NP || instruction_count < 0)
        return 0;
    for (i = 0; i < instruction_count && !end_of_simulation[p]; ++i)
    {
        if (PC[p] < 0 || PC[p] + 3 >= INSTRUCTION_MEMORY_SIZE)
        {
            fprintf(stderr, "Processor %d: PC out of range: %d\n", p, PC[p]);
            end_of_simulation[p] = 1;
            return 0;
        }
        fetch(p);
        PC[p] += 4;
        execute(p);
    }

    /* Lab specification: approximately 10 usec delay after each slice. */
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
