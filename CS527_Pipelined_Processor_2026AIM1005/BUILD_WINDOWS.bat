@echo off
setlocal
where gcc >nul 2>nul
if errorlevel 1 (
  echo GCC not found. Run this from MSYS2 UCRT64/MinGW terminal.
  exit /b 1
)
gcc -Wall -Wextra -std=c11 -o executable.exe main.c compiler.c processor.c memory.c os.c
if errorlevel 1 exit /b 1
echo Build successful: executable.exe
