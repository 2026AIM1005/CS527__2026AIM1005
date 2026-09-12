$ErrorActionPreference = 'Stop'
Write-Host 'Compiling CS527 Lab 5 ...'
gcc -Wall -Wextra -std=c11 -o lab5_.exe main.c compiler.c processor.c memory.c os.c
Write-Host 'Build successful.'
Write-Host ''
Write-Host 'Test 1: Array Add'
.\lab5_.exe programs\array_add.txt tests\data_array.byte
Write-Host ''
Write-Host 'Test 2: FIR'
.\lab5_.exe programs\fir.txt tests\data_fir.byte
Write-Host ''
Write-Host 'All tests completed successfully.'
