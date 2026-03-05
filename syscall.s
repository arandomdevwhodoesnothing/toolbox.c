/*
 * libc/src/arch/aarch64/syscall.s
 * Raw Linux AArch64 syscall wrappers + program entry point
 *
 * AArch64 Linux syscall convention:
 *   x8  = syscall number
 *   x0–x5 = arguments (up to 6)
 *   x0  = return value (negative = error)
 *   All other registers preserved by kernel
 *
 * C calling convention (AAPCS64):
 *   x0–x7  = integer arguments / return values
 *   x8      = indirect result location (we hijack only inside syscall)
 *   x9–x15  = temporary (caller-saved)
 *   x16–x17 = intra-procedure scratch (IP0/IP1)
 *   x18     = platform register (avoid)
 *   x19–x28 = callee-saved
 *   x29     = frame pointer
 *   x30     = link register (lr)
 *   sp      = stack pointer (16-byte aligned at calls)
 */

.text

/* ── syscall0(nr) ──────────────────────────────────────────────────── */
.global __syscall0
.type   __syscall0, %function
__syscall0:
    mov  x8, x0          /* syscall number */
    svc  #0
    ret

/* ── syscall1(nr, a1) ─────────────────────────────────────────────── */
.global __syscall1
.type   __syscall1, %function
__syscall1:
    mov  x8, x0
    mov  x0, x1
    svc  #0
    ret

/* ── syscall2(nr, a1, a2) ─────────────────────────────────────────── */
.global __syscall2
.type   __syscall2, %function
__syscall2:
    mov  x8, x0
    mov  x0, x1
    mov  x1, x2
    svc  #0
    ret

/* ── syscall3(nr, a1, a2, a3) ────────────────────────────────────── */
.global __syscall3
.type   __syscall3, %function
__syscall3:
    mov  x8, x0
    mov  x0, x1
    mov  x1, x2
    mov  x2, x3
    svc  #0
    ret

/* ── syscall4(nr, a1, a2, a3, a4) ───────────────────────────────── */
.global __syscall4
.type   __syscall4, %function
__syscall4:
    mov  x8, x0
    mov  x0, x1
    mov  x1, x2
    mov  x2, x3
    mov  x3, x4
    svc  #0
    ret

/* ── syscall5(nr, a1, a2, a3, a4, a5) ──────────────────────────── */
.global __syscall5
.type   __syscall5, %function
__syscall5:
    mov  x8, x0
    mov  x0, x1
    mov  x1, x2
    mov  x2, x3
    mov  x3, x4
    mov  x4, x5
    svc  #0
    ret

/* ── syscall6(nr, a1, a2, a3, a4, a5, a6) ──────────────────────── */
.global __syscall6
.type   __syscall6, %function
__syscall6:
    mov  x8, x0
    mov  x0, x1
    mov  x1, x2
    mov  x2, x3
    mov  x3, x4
    mov  x4, x5
    mov  x5, x6
    svc  #0
    ret

/* ══════════════════════════════════════════════════════════════════
 * _start — AArch64 Linux program entry point
 *
 * When the kernel executes our binary, the initial sp points to:
 *   [sp+0]          argc        (8 bytes)
 *   [sp+8]          argv[0]
 *   [sp+8+argc*8]   NULL
 *   [sp+8+argc*8+8] envp[0]
 *   ...             NULL
 * ══════════════════════════════════════════════════════════════════ */
.global _start
.type   _start, %function
_start:
    /* Clear frame pointer (ABI requirement) */
    mov  x29, #0
    mov  x30, #0

    /* Load argc */
    ldr  x0, [sp]

    /* argv = sp + 8 */
    add  x1, sp, #8

    /* envp = sp + 8 + (argc+1)*8 */
    add  x2, x1, x0, lsl #3   /* x1 + argc*8 */
    add  x2, x2, #8           /* skip NULL terminator */

    /* Align stack to 16 bytes before the call */
    ldr  x0, [sp]
    add  x1, sp, #8
    add  x2, x1, x0, lsl #3
    add  x2, x2, #8

    /* Call our C startup */
    bl   __libc_start_main

    /* Should never return — but call exit(0) defensively */
    mov  x8, #93               /* SYS_exit */
    mov  x0, #0
    svc  #0

/* Mark stack as non-executable (linker note) */
.section .note.GNU-stack, "", %progbits
