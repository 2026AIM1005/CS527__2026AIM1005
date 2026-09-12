# CS527  — Five-Stage Pipelined Processor

**Student:** Avivardhan Kumar Singh  
**Roll No.:** 2026AIM1005  
**Course:** CS527  
**Extension:**  — Implement pipelining in the processor  
**Language:** C  
**Base:** Corrected Lab 5 architecture and ISA

## 1. Purpose

This project implements a five-stage instruction pipeline while preserving the Lab 1–5 instruction set, compiler, MMU, multi-processing OS and waiting-queue model.

The pipeline is:

```text
IF -> ID -> EX -> MEM -> WB
```

The processor remains an instance of the Lab 5 mini-computer. The pipeline is added inside `processor.c`; the compiler and ISA are not replaced by a new architecture.

## 2. Lab 5 Features Preserved

The implementation retains:

- 256 integer registers (`x0`–`x255`)
- 32 vector registers (`v0`–`v31`)
- 256-bit vector registers
- 8 x 32-bit vector lanes
- integer arithmetic
- immediate arithmetic
- vector add/subtract/multiply
- integer and vector loads/stores
- branches and condition flags `Z N C V`
- `Print`
- logical-to-physical MMU translation
- paging and physical frames
- four processor instances
- PID-to-processor mapping
- ready and waiting queues
- time-sliced OS scheduling
- final data write-back

## 3. Pipeline Stages

### IF — Instruction Fetch

The next four instruction bytes are fetched from logical instruction memory.

Each byte is translated through:

```c
getPhysicallAddress(proc_id, 1, PC)
```

The fetched instruction is stored in the IF/ID pipeline register.

### ID — Instruction Decode

The instruction is classified and its dependencies are checked.

The implementation uses conservative hazard detection rather than speculative execution.

### EX — Execute

Arithmetic, vector operations, branch decisions and effective-address calculations are performed.

Branches use the condition flags defined by the Lab specification.

### MEM — Memory

Loads and stores access physical memory through the MMU.

Vector loads/stores perform eight 32-bit memory accesses.

### WB — Write Back

Results are committed to integer/vector registers and arithmetic flags.

`x0` is restored to zero after write-back.

## 4. Data Hazards

The pipeline uses a conservative no-forwarding policy.

For a RAW dependency:

```text
x1 = x2 + 5
x3 = x1 + 7
```

the second instruction waits until the first instruction has completed write-back.

The same idea is applied to:

- integer registers
- vector registers
- condition flags used by branches

This makes the pipelined implementation functionally equivalent to the sequential Lab 5 processor while explicitly modeling pipeline stalls.

## 5. Control Hazards

Branches are resolved in the EX stage.

When a branch is taken:

```text
PC <- branch target
IF/ID <- bubble
ID/EX <- bubble
```

This flushes younger instructions that were fetched from the wrong path.

The compiler's branch displacement is interpreted relative to the next sequential instruction.

## 6. HALT Handling

Opcode `0x00` is the HALT instruction.

HALT stops new instruction fetches, but older instructions already inside the pipeline are allowed to complete.

The processor reports:

```text
end_of_simulation = 1
```

only after the HALT has reached the end of the pipeline and all older instructions have drained.

This prevents a final store from being lost merely because HALT was fetched.

## 7. Interaction with the OS Time Slice

The original Lab 5 scheduler gives each active processor a fixed time slice.

For , the argument to:

```c
process_instructions(proc_id, instruction_count)
```

represents **pipeline clock cycles** rather than sequential instructions.

The pipeline state is retained between calls.

Therefore:

```text
OS scheduler
     |
     +-- 10 pipeline cycles for PID 1
     |
     +-- 10 pipeline cycles for PID 2
     |
     +-- ...
```

A task can be interrupted between slices without losing:

- PC
- integer registers
- vector registers
- flags
- IF/ID state
- ID/EX state
- EX/MEM state
- MEM/WB state
- HALT/drain state

This is necessary for the pipeline to work correctly with the Lab 5 time-division scheduler.

## 8. Memory and MMU

 keeps the Lab 5 paging model.

```text
MEMSIZE  = 8192 bytes
PAGESIZE = 512 bytes
```

There are:

```text
16 physical frames
1 reserved frame
15 allocatable frames
```

The processor never directly treats a logical address as a physical address.

Instruction fetch:

```c
getPhysicallAddress(proc_id, 1, logical_address)
```

Data access:

```c
getPhysicallAddress(proc_id, 0, logical_address)
```

## 9. Physical-Memory Allocation Improvement

The corrected Lab 5 memory loader first checks whether enough complete frames are available for the whole task.

This avoids partially allocating a task that must wait.

When memory is unavailable:

```text
OS: PID ... waiting for physical memory.
```

After another task terminates:

```text
MMU: processor ... pages released.
OS: PID ... assigned to processor ... from waiting queue.
```

## 10. Files

```text
main.c
compiler.c
compiler.h
processor.c
processor.h
memory.c
memory.h
os.c
os.h
Makefile
run_tests.ps1
README.md
programs/
    array_add.txt
    fir.txt
tests/
    data_array.byte
    data_fir.byte
    expected/
```

## 11. Build — Windows PowerShell

```powershell
gcc -Wall -Wextra -std=c11 -o lab5_.exe main.c compiler.c processor.c memory.c os.c
```

Run Array Add:

```powershell
.\lab5_.exe programs\array_add.txt tests\data_array.byte
```

Run FIR:

```powershell
.\lab5_.exe programs\fir.txt tests\data_fir.byte
```

Interactive mode:

```powershell
.\lab5_.exe
```

Then:

```text
programs\array_add.txt tests\data_array.byte
programs\fir.txt tests\data_fir.byte
exit
```

## 12. Expected Results

### Array Add

The supplied test produces:

```text
11 22 33 44 55 66 77 88
```

### FIR

The supplied test produces:

```text
2040 2400 2760 3120 3480 3840 4200 4560
```

### Multi-processor test

Multiple small tasks can occupy processors 0–3 simultaneously.

Example:

```text
OS: loaded PID 1 on processor 0
OS: loaded PID 2 on processor 1
OS: loaded PID 3 on processor 2
OS: loaded PID 4 on processor 3
```

Each task keeps its own pipeline state.

## 13.  Design Summary

```text
                +---------+
PC -----------> |   IF    |
                +----+----+
                     |
                     v
                +---------+
                |   ID    |  <-- hazard detection / stalls
                +----+----+
                     |
                     v
                +---------+
                |   EX    |  <-- ALU / vector / branch
                +----+----+
                     |
                     v
                +---------+
                |   MEM   |  <-- MMU + physical memory
                +----+----+
                     |
                     v
                +---------+
                |   WB    |  <-- registers / flags
                +---------+
```

The implementation is intentionally conservative: it prioritizes correctness of the Lab 5 ISA and OS semantics while explicitly modeling pipeline stages, hazards, stalls, branch flushing and pipeline draining.


Running Command For Windows:
dir 
gcc -Wall -Wextra -std=c11 -o executable.exe main.c compiler.c processor.c memory.c os.c
dir executable.exe 
.\executable.exe  
programs/mapping_test.txt tests/data_small.byte
programs/mapping_test.txt tests/data_small.byte
programs/mapping_test.txt tests/data_small.byte
programs/mapping_test.txt tests/data_small.byte
exit
Remove-Item *.log 
Remove-Item *.byte

Running command for linux:
cd CS527_Q3_Physical_PageTable_2026AIM1005
ls
gcc -Wall -Wextra -std=c11 -o executable main.c compiler.c processor.c memory.c os.c
ls -l executable
./executable
programs/mapping_test.txt tests/data_small.byte
programs/mapping_test.txt tests/data_small.byte
programs/mapping_test.txt tests/data_small.byte
programs/mapping_test.txt tests/data_small.byte
exit

cd ~/Downloads
unzip CS527_Q7_Pipelined_Processor_2026AIM1005_PID1to4.zip

cd CS527_Q7_Pipelined_Processor_2026AIM1005
ls
gcc -Wall -Wextra -std=c11 -o executable main.c compiler.c processor.c memory.c os.c
ls -l executable
./executable
programs/mapping_test.txt tests/data_small.byte
programs/mapping_test.txt tests/data_small.byte
programs/mapping_test.txt tests/data_small.byte
programs/mapping_test.txt tests/data_small.byte
exit