#ifndef MEMORY_H
#define MEMORY_H

#include "processor.h"

/* Physical memory and paging configuration. */
#define MEMSIZE 8192
#define PAGESIZE 512

/* Logical address space:
 *   Instruction memory: 0 .. 1023  (2 pages)
 *   Data memory:        0 .. 4095 (8 pages), mapped after instruction pages.
 */
#define INSTRUCTION_MEMORY_SIZE 1024
#define DATA_MEMORY_SIZE 4096
#define NUM_LOGICAL_PAGES ((1024 / PAGESIZE) + (4096 / PAGESIZE))
#define NUM_PHYSICAL_PAGES (MEMSIZE / PAGESIZE)

/* Frame 0 is permanently reserved. */
#define RESERVED_FRAME 0

extern unsigned char memory[MEMSIZE];

/*
 * Takes a processor id, access type (1 = instruction fetch, 0 = data),
 * and a logical address. Returns the corresponding physical address.
 */
int getPhysicallAddress(int proc_id, int isFetch, int address);

/* Returns 1 on success, 0 on file/error failure, -1 when physical frames
 * are temporarily insufficient and the task should wait. */
int memory_initialize(int proc_id, const char *program_file, const char *data_file);

/* Writes the task's logical data memory, then releases all allocated frames. */
int memory_finalize(int proc_id, const char *data_file);

/* Reset all MMU/page-allocation state. */
void memory_system_init(void);

#endif
