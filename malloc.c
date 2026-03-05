/*
 * libc/src/malloc.c
 * Heap allocator built directly on mmap() syscall.
 * Uses a free-list allocator with block coalescing.
 * Zero dependency on any external malloc.
 */

#include "../include/libtool.h"
#include "../include/syscall.h"

/* ── Block header ────────────────────────────────────────────────────── */
/*
 * Each allocation is preceded by a header:
 *   [size | flags] [canary] [user data ...]
 *
 * size includes the header itself.
 * Bit 0 of size field = "in use" flag (size is always >=8 and aligned).
 */

#define HEAP_ALIGN      16UL
#define MIN_BLOCK       (sizeof(block_t) + HEAP_ALIGN)
#define CHUNK_SIZE      MB(2)          /* mmap granularity              */
#define CANARY_VAL      0xFEEDFACECAFEBABEULL

typedef struct block {
    size_t          size;     /* total block size (header + data), bit0=used */
    uint64_t        canary;
    struct block   *prev;     /* free list prev (only valid when free) */
    struct block   *next;     /* free list next (only valid when free) */
} block_t;

#define BLOCK_USED(b)     ((b)->size & 1UL)
#define BLOCK_SIZE(b)     ((b)->size & ~1UL)
#define BLOCK_SET_USED(b) ((b)->size |=  1UL)
#define BLOCK_SET_FREE(b) ((b)->size &= ~1UL)
#define BLOCK_DATA(b)     ((void*)((char*)(b) + sizeof(block_t)))
#define DATA_BLOCK(p)     ((block_t*)((char*)(p) - sizeof(block_t)))
#define NEXT_BLOCK(b)     ((block_t*)((char*)(b) + BLOCK_SIZE(b)))

/* ── Free list ───────────────────────────────────────────────────────── */
static block_t *__free_list = NULL;

/* ── Chunk list (for munmap on free) ────────────────────────────────── */
typedef struct chunk { void *addr; size_t size; struct chunk *next; } chunk_t;
static chunk_t *__chunks = NULL;

static void fl_remove(block_t *b) {
    if (b->prev) b->prev->next = b->next;
    else         __free_list   = b->next;
    if (b->next) b->next->prev = b->prev;
    b->prev = b->next = NULL;
}

static void fl_insert(block_t *b) {
    b->next = __free_list;
    b->prev = NULL;
    if (__free_list) __free_list->prev = b;
    __free_list = b;
}

/* ── Grow heap via mmap ──────────────────────────────────────────────── */
static block_t *heap_grow(size_t need) {
    size_t sz = ALIGN_UP(need + sizeof(block_t) + sizeof(chunk_t), CHUNK_SIZE);
    void *mem = mmap(NULL, sz, PROT_READ|PROT_WRITE,
                     MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) return NULL;

    /* Store chunk metadata at the end of the region */
    chunk_t *ch = (chunk_t *)((char *)mem + sz - sizeof(chunk_t));
    ch->addr = mem;
    ch->size = sz;
    ch->next = __chunks;
    __chunks = ch;

    size_t usable = sz - sizeof(chunk_t);
    block_t *b    = (block_t *)mem;
    b->size       = usable;
    b->canary     = CANARY_VAL;
    b->prev = b->next = NULL;
    BLOCK_SET_FREE(b);
    fl_insert(b);
    return b;
}

/* ── malloc ──────────────────────────────────────────────────────────── */
void *malloc(size_t size) {
    if (!size) return NULL;
    /* Round up to alignment, add header */
    size_t need = ALIGN_UP(size, HEAP_ALIGN) + sizeof(block_t);
    if (need < MIN_BLOCK) need = MIN_BLOCK;

    /* First-fit search */
    for (block_t *b = __free_list; b; b = b->next) {
        size_t bsz = BLOCK_SIZE(b);
        if (bsz < need) continue;

        fl_remove(b);

        /* Split if remainder is large enough */
        if (bsz >= need + MIN_BLOCK) {
            block_t *rest   = (block_t *)((char *)b + need);
            rest->size      = bsz - need;
            rest->canary    = CANARY_VAL;
            rest->prev = rest->next = NULL;
            BLOCK_SET_FREE(rest);
            fl_insert(rest);
            b->size = need;
        }

        b->canary = CANARY_VAL;
        BLOCK_SET_USED(b);
        return BLOCK_DATA(b);
    }

    /* No fit — grow heap */
    block_t *b = heap_grow(need);
    if (!b) { errno = ENOMEM; return NULL; }

    /* Try again after growing */
    return malloc(size);
}

/* ── calloc ──────────────────────────────────────────────────────────── */
void *calloc(size_t nmemb, size_t size) {
    if (!nmemb || !size) return NULL;
    /* Overflow check */
    if (size > SIZE_MAX / nmemb) { errno = ENOMEM; return NULL; }
    size_t total = nmemb * size;
    void *p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

/* ── free ────────────────────────────────────────────────────────────── */
void free(void *ptr) {
    if (!ptr) return;
    block_t *b = DATA_BLOCK(ptr);

    /* Corruption check */
    assert(b->canary == CANARY_VAL);
    assert(BLOCK_USED(b));

    b->canary = CANARY_VAL;   /* keep canary valid */
    BLOCK_SET_FREE(b);

    /* Poison freed memory */
    memset(ptr, 0xCD, BLOCK_SIZE(b) - sizeof(block_t));

    fl_insert(b);

    /* Coalesce: scan free list for adjacent blocks */
    /* Simple approach: try to merge with blocks at adjacent addresses */
    block_t *cur = __free_list;
    while (cur) {
        block_t *nx = (block_t *)((char *)cur + BLOCK_SIZE(cur));
        /* Check if nx is also in the free list */
        if (!BLOCK_USED(nx) && nx->canary == CANARY_VAL) {
            /* Merge cur + nx */
            fl_remove(nx);
            cur->size = BLOCK_SIZE(cur) + BLOCK_SIZE(nx);
            /* restart scan */
            cur = __free_list;
        } else {
            cur = cur->next;
        }
    }
}

/* ── realloc ─────────────────────────────────────────────────────────── */
void *realloc(void *ptr, size_t size) {
    if (!ptr)  return malloc(size);
    if (!size) { free(ptr); return NULL; }

    block_t *b    = DATA_BLOCK(ptr);
    assert(b->canary == CANARY_VAL);
    size_t oldsz  = BLOCK_SIZE(b) - sizeof(block_t);
    size_t newsz  = ALIGN_UP(size, HEAP_ALIGN);

    if (newsz <= oldsz) return ptr;  /* fits in place */

    void *np = malloc(size);
    if (!np) return NULL;
    memcpy(np, ptr, oldsz);
    free(ptr);
    return np;
}

/* ── aligned_alloc ───────────────────────────────────────────────────── */
void *aligned_alloc(size_t align, size_t size) {
    if (!IS_POW2(align) || align < sizeof(void *)) return NULL;
    /* Over-allocate and store original pointer before aligned address */
    size_t total = size + align + sizeof(void *);
    void *raw = malloc(total);
    if (!raw) return NULL;
    uintptr_t addr  = (uintptr_t)raw + sizeof(void *);
    uintptr_t aaddr = ALIGN_UP(addr, align);
    ((void **)aaddr)[-1] = raw;
    return (void *)aaddr;
}
