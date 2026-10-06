/* Host objects the guest sees (thread handles and TLS blocks, mutex/cond/rwlock handles,
 * module TLS) must lie below 1 TiB: the guest packs pointers into 40 bits. On Linux the
 * non-PIE brk heap is already there (probe.c keeps malloc on it), so this is the libc
 * allocator. macOS malloc returns addresses near 0x6000'0000'0000: there these objects come
 * from size classes carved out of runtime_low_map chunks, or whole low mappings when large. */
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __APPLE__
#include <pthread.h>
#include <sys/mman.h>
#define CLASSES 13 /* 16 bytes .. 64 KiB */
#define CHUNK (UINT64_C(4)<<20)
#define LOW_PAGE UINT64_C(16384)
enum { SMALL=0x534d4c4c, LARGE=0x4c524745, ALIGNED=0x414c4e44 };
/* 16 bytes before every block: size is the class capacity, the mapping size (LARGE) or the
 * distance back to the underlying block (ALIGNED). */
typedef struct { uint32_t magic, size_class; uint64_t size; } Header;
typedef struct Free { struct Free *next; } Free;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static Free *free_lists[CLASSES];
static unsigned char *chunk_next, *chunk_end;

void *runtime_guest_malloc(size_t size) {
    if (!size) size=1;
    if (size>(size_t)16<<(CLASSES-1)) {
        if (size>SIZE_MAX-sizeof(Header)-LOW_PAGE) return NULL;
        size_t total=(size+sizeof(Header)+LOW_PAGE-1)&~(size_t)(LOW_PAGE-1);
        Header *h=runtime_low_map(total,PROT_READ|PROT_WRITE);
        if (!h) return NULL;
        *h=(Header){LARGE,0,total};
        return h+1;
    }
    unsigned c=0;
    while ((size_t)16<<c<size) ++c;
    size_t block=sizeof(Header)+((size_t)16<<c);
    pthread_mutex_lock(&lock);
    Header *h=(Header *)free_lists[c];
    if (h) free_lists[c]=free_lists[c]->next;
    else {
        if ((uintptr_t)chunk_end-(uintptr_t)chunk_next<block) {
            unsigned char *chunk=runtime_low_map(CHUNK,PROT_READ|PROT_WRITE);
            if (!chunk) { pthread_mutex_unlock(&lock); return NULL; }
            chunk_next=chunk; chunk_end=chunk+CHUNK;
        }
        h=(Header *)chunk_next; chunk_next+=block;
    }
    pthread_mutex_unlock(&lock);
    *h=(Header){SMALL,c,(size_t)16<<c};
    return h+1;
}
void *runtime_guest_calloc(size_t count, size_t size) {
    if (size && count>SIZE_MAX/size) return NULL;
    void *p=runtime_guest_malloc(count*size);
    if (p) memset(p,0,count*size);
    return p;
}
void *runtime_guest_aligned_alloc(size_t alignment, size_t size) {
    if (alignment<=16) return runtime_guest_malloc(size);
    if (alignment&(alignment-1) || size>SIZE_MAX-alignment-sizeof(Header)) return NULL;
    unsigned char *raw=runtime_guest_malloc(size+alignment+sizeof(Header));
    if (!raw) return NULL;
    uintptr_t at=((uintptr_t)raw+sizeof(Header)+alignment-1)&~(uintptr_t)(alignment-1);
    ((Header *)at)[-1]=(Header){ALIGNED,0,at-(uintptr_t)raw};
    return (void *)at;
}
void runtime_guest_free(void *p) {
    if (!p) return;
    Header *h=(Header *)p-1;
    if (h->magic==ALIGNED) { p=(unsigned char *)p-h->size; h=(Header *)p-1; }
    if (h->magic==LARGE) { runtime_low_unmap(h,h->size); return; }
    if (h->magic!=SMALL || h->size_class>=CLASSES) { fputs("STOP: invalid guest heap pointer\n",stderr); exit(21); }
    unsigned c=h->size_class;
    pthread_mutex_lock(&lock);
    ((Free *)h)->next=free_lists[c]; free_lists[c]=(Free *)h;
    pthread_mutex_unlock(&lock);
}
#else
void *runtime_guest_malloc(size_t size) { return malloc(size); }
void *runtime_guest_calloc(size_t count, size_t size) { return calloc(count,size); }
#ifndef _WIN32
void *runtime_guest_aligned_alloc(size_t alignment, size_t size) { return aligned_alloc(alignment,size); }
#endif
void runtime_guest_free(void *p) { free(p); }
#endif
