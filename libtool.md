# libtool — Freestanding libc for Linux AArch64

A from-scratch libc for 64-bit ARM Linux and Toolbox. Zero glibc, zero `-lm`, zero `-lc`.
Compiled with `-ffreestanding -nostdlib -nostdinc`. For x86-64 Support, Consider cross-compiling

## Build & Run

```bash
make          # builds libc.a + crt0.o
make test     # compiles and runs the test suite
make demo     # compiles and runs examples/hello.c
```

## Use in your own program

```bash
gcc -ffreestanding -nostdlib -nostdinc -no-pie \
    -Iinclude your_program.c crt0.o libtool.a -o your_program
```

Your program just needs:
```c
#include "libtool.h"

int main(int argc, char **argv, char **envp) {
    printf("Hello, AArch64!\n");
    return 0;
}
```

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