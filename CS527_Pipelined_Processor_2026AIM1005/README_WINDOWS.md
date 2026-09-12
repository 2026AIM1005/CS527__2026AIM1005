# Windows Build and PID Mapping

This project is based on **Lab 5** and implements the requested extension.

## Requested PID-to-processor mapping

The first four tasks are deterministically assigned as:

| PID | Processor |
|---:|---:|
| 1 | 0 |
| 2 | 1 |
| 3 | 2 |
| 4 | 3 |

If a preferred processor is unavailable, the OS falls back to the first free processor. Waiting tasks use the same preferred mapping when that processor becomes available.

## Executable name

The Windows executable is named **`executable.exe`**. The Makefile and build script already use this name.

Build on Windows with MSYS2/MinGW GCC:

```bash
gcc -Wall -Wextra -std=c11 -o executable.exe main.c compiler.c processor.c memory.c os.c
```

or run `BUILD_WINDOWS.bat`.

## Four-process mapping test

Run `executable.exe` and enter this command four times:

```text
programs\\mapping_test.txt tests\\data_small.byte
```

Then enter:

```text
exit
```

Expected loading lines:

```text
OS: loaded PID 1 on processor 0
OS: loaded PID 2 on processor 1
OS: loaded PID 3 on processor 2
OS: loaded PID 4 on processor 3
```

Expected completion:

```text
OS: PID 1 finished on processor 0
OS: PID 2 finished on processor 1
OS: PID 3 finished on processor 2
OS: PID 4 finished on processor 3
Simulation completed successfully.
```

> A native Windows `.exe` is not fabricated by renaming a Linux ELF binary. Compile the included C source with your Windows GCC to generate the real `executable.exe`.
