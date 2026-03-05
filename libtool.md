# libtool — Freestanding libc for Linux AArch64

A from-scratch libc for 64-bit ARM Linux and Toolbox. Zero glibc, zero `-lm`, zero `-lc`.
Compiled with `-ffreestanding -nostdlib -nostdinc`.

## Build & Run

```bash
make          # builds libc.a + crt0.o
make test     # compiles and runs the test suite
make demo     # compiles and runs examples/hello.c
```

## Use in your own program

```bash
gcc -ffreestanding -nostdlib -nostdinc -no-pie \
    -Iinclude your_program.c crt0.o libc.a -o your_program
```

Your program just needs:
```c
#include "libc.h"

int main(int argc, char **argv, char **envp) {
    printf("Hello, AArch64!\n");
    return 0;
}
```.

## What's implemented

- **Startup**: `_start` in AArch64 assembly → `__libc_start_main` → `main`
- **Heap**: `malloc/calloc/realloc/free` via `mmap(MAP_ANONYMOUS)` — free-list with coalescing and canary corruption detection
- **stdio**: Buffered `FILE*` I/O — `fopen/fclose/fread/fwrite/fgets/fprintf/printf`
- **Strings**: All standard `str*` and `mem*` functions
- **Math**: `sqrt/pow/exp/log/log2/log10/sin/cos/tan/atan2/floor/ceil/round` — pure C, no `-lm`
- **Numbers**: `atoi/atof/strtol/strtoul/strtod`
- **Sort**: `qsort` (introsort) + `bsearch`
- **Time**: `clock_gettime/gettimeofday/nanosleep`
- **Random**: `getrandom` syscall + `rand/srand`
- **Process**: `exit/_exit/abort/atexit/getpid/getuid/getgid`
- **Env**: `getenv`
- **Assert**: `assert()` macro with file/line reporting

## File Structure

```
libc-arm64/
├── include/
│   ├── libc.h              # Single header — all declarations
│   └── syscall.h           # AArch64 syscall numbers + raw wrappers
├── src/
│   ├── arch/aarch64/
│   │   └── syscall.s       # _start entry + svc #0 wrappers
│   ├── start.c             # __libc_start_main, process, file syscalls
│   ├── malloc.c            # Heap allocator on mmap
│   ├── stdio.c             # Buffered FILE* I/O
│   ├── printf.c            # printf/vsnprintf/fprintf/snprintf
│   └── string.c            # mem*, str*, math, qsort
├── tests/test_all.c        # Test suite
├── examples/hello.c        # Demo program
└── Makefile
```
