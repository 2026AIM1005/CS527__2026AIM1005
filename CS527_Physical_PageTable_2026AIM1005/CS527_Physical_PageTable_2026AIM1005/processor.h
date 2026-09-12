#ifndef PROCESSOR_H
#define PROCESSOR_H

#include <stdio.h>

#define NP 4
#define NUM_INT_REGS 256
#define NUM_VECTOR_REGS 32
#define VECTOR_LANES 8

extern int registers[NP][NUM_INT_REGS];
extern int vectors[NP][NUM_VECTOR_REGS][VECTOR_LANES];
extern int PC[NP];
extern int end_of_simulation[NP];

void reset_processor(int proc_id);
void close_processor_log(int proc_id);
int process_instructions(int proc_id, int instruction_count);

#endif
