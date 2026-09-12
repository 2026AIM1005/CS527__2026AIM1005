#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "memory.h"

/*
 * Lab-5 paging implementation used by .
 *
 *  changes the processor, not the logical/physical memory model:
 * the MMU still owns the physical byte array and a page table per
 * processor/task, and every instruction/data access is translated.
 *
 * Frame 0 is reserved and never allocated to a task.
 */
unsigned char memory[MEMSIZE];

static unsigned char pageTable[NP][NUM_LOGICAL_PAGES];
static unsigned char freePages[NUM_PHYSICAL_PAGES];
static int program_size[NP];
static int data_size[NP];

static int pages_needed(int bytes)
{
    return (bytes + PAGESIZE - 1) / PAGESIZE;
}

static int count_free_frames(void)
{
    int frame, count = 0;

    for (frame = 1; frame < NUM_PHYSICAL_PAGES; ++frame)
        if (!freePages[frame])
            ++count;

    return count;
}

static int getFreePage(void)
{
    int frame;

    for (frame = 1; frame < NUM_PHYSICAL_PAGES; ++frame)
    {
        if (!freePages[frame])
        {
            freePages[frame] = 1;
            return frame;
        }
    }

    return -1;
}

static void release_frame(int frame)
{
    if (frame > RESERVED_FRAME && frame < NUM_PHYSICAL_PAGES)
    {
        freePages[frame] = 0;
        memset(memory + frame * PAGESIZE, 0, PAGESIZE);
    }
}

static void release_task_pages(int p)
{
    int i;

    if (p < 0 || p >= NP)
        return;

    for (i = 0; i < NUM_LOGICAL_PAGES; ++i)
    {
        int frame = pageTable[p][i];

        if (frame != RESERVED_FRAME)
            release_frame(frame);

        pageTable[p][i] = 0;
    }

    program_size[p] = 0;
    data_size[p] = 0;
}

void memory_system_init(void)
{
    memset(memory, 0, sizeof(memory));
    memset(pageTable, 0, sizeof(pageTable));
    memset(freePages, 0, sizeof(freePages));
    memset(program_size, 0, sizeof(program_size));
    memset(data_size, 0, sizeof(data_size));

    freePages[RESERVED_FRAME] = 1;
}

static int read_hex_file(const char *name, unsigned char *buffer, int max_size)
{
    FILE *fp;
    unsigned int b;
    int pos = 0;

    fp = fopen(name, "r");
    if (!fp)
    {
        perror(name);
        return -1;
    }

    while (pos < max_size && fscanf(fp, "%x", &b) == 1)
        buffer[pos++] = (unsigned char)(b & 0xFF);

    fclose(fp);
    return pos;
}

static int write_hex_file(const char *name, const unsigned char *buffer, int size)
{
    FILE *fp;
    int i;

    fp = fopen(name, "w");
    if (!fp)
    {
        perror(name);
        return 0;
    }

    for (i = 0; i < size; i += 4)
    {
        fprintf(fp, "%02X", buffer[i]);
        if (i + 1 < size) fprintf(fp, " %02X", buffer[i + 1]);
        if (i + 2 < size) fprintf(fp, " %02X", buffer[i + 2]);
        if (i + 3 < size) fprintf(fp, " %02X", buffer[i + 3]);
        fputc('\n', fp);
    }

    fclose(fp);
    return 1;
}

static int allocate_and_load_pages(int p,
                                   const unsigned char *source,
                                   int size,
                                   int first_logical_page,
                                   int max_logical_pages)
{
    int needed, i, frame, logical_page;

    needed = pages_needed(size);

    if (needed > max_logical_pages)
    {
        fprintf(stderr, "OS/MMU: task memory image is too large.\n");
        return 0;
    }

    if (count_free_frames() < needed)
        return -1;

    for (i = 0; i < needed; ++i)
    {
        int remaining, bytes;

        frame = getFreePage();
        if (frame < 0)
            return -1;

        logical_page = first_logical_page + i;
        pageTable[p][logical_page] = (unsigned char)frame;

        memset(memory + frame * PAGESIZE, 0, PAGESIZE);

        remaining = size - i * PAGESIZE;
        bytes = remaining > PAGESIZE ? PAGESIZE : remaining;

        memcpy(memory + frame * PAGESIZE,
               source + i * PAGESIZE,
               (size_t)bytes);
    }

    return needed;
}

int memory_initialize(int p, const char *program_file, const char *data_file)
{
    unsigned char program[INSTRUCTION_MEMORY_SIZE];
    unsigned char data[DATA_MEMORY_SIZE];
    int psize, dsize;
    int program_pages, data_pages;

    if (p < 0 || p >= NP)
        return 0;

    release_task_pages(p);

    memset(program, 0, sizeof(program));
    memset(data, 0, sizeof(data));

    psize = read_hex_file(program_file, program, INSTRUCTION_MEMORY_SIZE);
    if (psize < 0)
        return 0;

    dsize = read_hex_file(data_file, data, DATA_MEMORY_SIZE);
    if (dsize < 0)
        return 0;

    if (psize == 0)
    {
        fprintf(stderr, "OS/MMU: program contains no bytes: %s\n", program_file);
        return 0;
    }

    if (dsize == 0)
    {
        fprintf(stderr, "OS/MMU: data file contains no bytes: %s\n", data_file);
        return 0;
    }

    program_pages = pages_needed(psize);
    data_pages = pages_needed(dsize);

    if (program_pages > INSTRUCTION_MEMORY_SIZE / PAGESIZE ||
        data_pages > DATA_MEMORY_SIZE / PAGESIZE)
    {
        fprintf(stderr, "OS/MMU: task memory image is too large.\n");
        return 0;
    }

    /*
     * Check the complete requirement before taking any frame.  This makes
     * a memory-starved task wait cleanly instead of partially allocating
     * frames and printing repeated allocation errors.
     */
    if (count_free_frames() < program_pages + data_pages)
        return -1;

    if (allocate_and_load_pages(
            p, program, psize, 0, INSTRUCTION_MEMORY_SIZE / PAGESIZE) < 0)
    {
        release_task_pages(p);
        return -1;
    }

    if (allocate_and_load_pages(
            p, data, dsize, INSTRUCTION_MEMORY_SIZE / PAGESIZE,
            DATA_MEMORY_SIZE / PAGESIZE) < 0)
    {
        release_task_pages(p);
        return -1;
    }

    program_size[p] = psize;
    data_size[p] = dsize;

    printf("MMU: processor %d -> program %d byte(s), %d page(s); "
           "data %d byte(s), %d page(s)\n",
           p, psize, program_pages, dsize, data_pages);

    return 1;
}

int getPhysicallAddress(int proc_id, int isFetch, int address)
{
    int logical_page;
    int offset;
    int frame;

    if (proc_id < 0 || proc_id >= NP)
    {
        fprintf(stderr, "MMU: invalid processor id %d\n", proc_id);
        exit(EXIT_FAILURE);
    }

    if (isFetch)
    {
        if (address < 0 || address >= INSTRUCTION_MEMORY_SIZE)
        {
            fprintf(stderr,
                    "MMU: instruction logical address out of range: %d\n",
                    address);
            exit(EXIT_FAILURE);
        }

        logical_page = address / PAGESIZE;
    }
    else
    {
        if (address < 0 || address >= DATA_MEMORY_SIZE)
        {
            fprintf(stderr,
                    "MMU: data logical address out of range: %d\n",
                    address);
            exit(EXIT_FAILURE);
        }

        logical_page = address / PAGESIZE +
                       INSTRUCTION_MEMORY_SIZE / PAGESIZE;
    }

    if (logical_page < 0 || logical_page >= NUM_LOGICAL_PAGES)
    {
        fprintf(stderr, "MMU: invalid logical page %d\n", logical_page);
        exit(EXIT_FAILURE);
    }

    frame = pageTable[proc_id][logical_page];

    if (frame == RESERVED_FRAME ||
        frame < 0 || frame >= NUM_PHYSICAL_PAGES)
    {
        fprintf(stderr,
                "MMU: page fault/unmapped page: processor %d, "
                "logical address %d, %s\n",
                proc_id, address, isFetch ? "fetch" : "data");
        exit(EXIT_FAILURE);
    }

    offset = address % PAGESIZE;
    return frame * PAGESIZE + offset;
}

int memory_finalize(int p, const char *data_file)
{
    unsigned char logical_data[DATA_MEMORY_SIZE];
    int i, physical;
    int size;

    if (p < 0 || p >= NP)
        return 0;

    size = data_size[p];
    memset(logical_data, 0, sizeof(logical_data));

    for (i = 0; i < size; ++i)
    {
        physical = getPhysicallAddress(p, 0, i);
        logical_data[i] = memory[physical];
    }

    if (!write_hex_file(data_file, logical_data, size))
        return 0;

    release_task_pages(p);

    printf("MMU: processor %d pages released.\n", p);
    return 1;
}
