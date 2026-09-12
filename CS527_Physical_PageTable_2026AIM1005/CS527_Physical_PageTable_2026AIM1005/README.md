# CS527 Q3 — Page Table Stored in Physical Memory

**Student:** Avivardhan Kumar Singh  
**Roll No.:** 2026AIM1005  
**Course:** CS527  
**Extension:** Q3 — Store the page table in physical memory  
**Language:** C  
**Base:** Corrected Lab 5 architecture and ISA

## 1. Purpose

This project implements the Q3 extension on top of the Lab 1–5 mini-computer architecture.

The Lab 5 paging/MMU model is preserved:

- 256 integer registers of 32 bits
- 32 vector registers of 256 bits
- 8 integer lanes per vector register
- four processors (`NP = 4`)
- round-robin/time-sliced OS scheduling
- ready and waiting queues
- logical-to-physical address translation
- `MEMSIZE = 8192`
- `PAGESIZE = 512`
- frame 0 permanently reserved

The Q3 change is specifically that the page-table entries are no longer stored in a C array such as:

```c
pageTable[NP][NUM_LOGICAL_PAGES]
```

Instead, the page tables themselves are stored as bytes inside **reserved physical frame 0**.

## 2. Physical Page-Table Layout

With the required configuration:

```text
Physical memory       = 8192 bytes
Page/frame size       = 512 bytes
Physical frames       = 16
Reserved frame        = 0
Usable task frames    = 15

Instruction memory    = 1024 bytes = 2 pages
Data memory           = 4096 bytes = 8 pages
Total logical pages   = 10
```

Each page-table entry is one byte because the physical frame number is between 0 and 15.

Therefore one task needs only:

```text
10 page-table entries = 10 bytes
```

The Q3 implementation packs the tables into reserved frame 0:

```text
processor 0 -> memory[0  .. 9]
processor 1 -> memory[10 .. 19]
processor 2 -> memory[20 .. 29]
processor 3 -> memory[30 .. 39]
```

The rest of frame 0 remains reserved.

This is important because the page table does **not** consume an additional task frame.

## 3. Address Translation

For instruction access:

```text
logical_page = address / PAGESIZE
```

For data access:

```text
logical_page = address / PAGESIZE
             + INSTRUCTION_MEMORY_SIZE / PAGESIZE
```

Then:

```text
offset = address % PAGESIZE
frame  = physical_page_table[logical_page]

physical_address = frame * PAGESIZE + offset
```

Every byte of a four-byte instruction is translated. Data and vector accesses use the same MMU translation.

## 4. Frame Allocation

Frame 0 is never allocated to a program.

The MMU scans physical frames from frame 1 upward and returns the first free frame.

The implementation also checks the complete frame requirement before allocating a task. This prevents partially allocating a waiting task and avoids repeated allocation-error messages.

For example, the normal array-add test uses:

```text
1 program page + 8 data pages = 9 physical frames
```

If fewer than 9 task frames are available, the task remains in the OS waiting queue.

## 5. Waiting Queue

The OS retains the Lab 5 scheduling model.

If a task cannot obtain enough physical frames:

```text
new task
   |
   v
waiting queue
   |
   v
running task finishes
   |
   v
memory_finalize()
   |
   v
frames released
   |
   v
waiting task loaded
```

This was specifically kept so that multiple submitted tasks eventually execute instead of remaining permanently queued.

## 6. Finalization

When a process finishes:

1. Its logical data addresses are translated through its physical page table.
2. The logical data image is reconstructed.
3. The result is written back to the task's data file.
4. All task-owned frames are released.
5. The task's page-table slot in frame 0 is cleared.

## 7. Files

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

## 8. Build — Windows PowerShell

```powershell
gcc -Wall -Wextra -std=c11 -o lab5_q3.exe main.c compiler.c processor.c memory.c os.c
```

Run one test:

```powershell
.\lab5_q3.exe programs\array_add.txt tests\data_array.byte
```

Run the FIR test:

```powershell
.\lab5_q3.exe programs\fir.txt tests\data_fir.byte
```

Interactive mode:

```powershell
.\lab5_q3.exe
```

Then enter:

```text
programs\array_add.txt tests\data_array.byte
programs\array_add.txt tests\data_array.byte
programs\fir.txt tests\data_fir.byte
exit
```

## 9. Expected Output

For a normal single-task run, messages include:

```text
MMU: Q3 page tables stored in reserved physical frame 0
MMU: processor 0 -> program ... byte(s), ... page(s); data ... byte(s), ... page(s)
OS: loaded PID 1 on processor 0
OS: PID 1 finished on processor 0
MMU: processor 0 pages released; physical page table slot cleared.
Simulation completed successfully.
```

For multiple small tasks, different processors can be assigned simultaneously. For large tasks that require most physical frames, later tasks wait until frames are released.

## 10. Test Results

### Array Add

The supplied vector array-add program produces:

```text
11 22 33 44 55 66 77 88
```

### FIR

The supplied FIR program produces:

```text
2040 2400 2760 3120 3480 3840 4200 4560
```

## 11. Q3 Design Summary

The central Q3 modification is:

```text
Before:
C array
pageTable[NP][NUM_LOGICAL_PAGES]

Q3:
physical memory
+--------------------------------------------------+
| Frame 0: page tables                             |
| PT0 | PT1 | PT2 | PT3 | unused                  |
+--------------------------------------------------+
| Frame 1+ : task program/data pages               |
+--------------------------------------------------+
```

Thus the MMU page table is physically resident in the same physical memory abstraction used by the simulator, while frame 0 remains reserved and unavailable to program/data pages.

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