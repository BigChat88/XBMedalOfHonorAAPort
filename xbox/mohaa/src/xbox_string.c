/*
 * xbox_string.c -- memory functions for the whole XBE.
 *
 * nxdk's libc (pdclib) implements these one byte at a time. They are all over
 * the engine, and pbgl copies 1+ MB of vertices per frame to uncached memory
 * with memcpy, so they showed up at the top of the profile. Being defined here,
 * the linker takes these instead of the library's.
 */
#include <stddef.h>
#include <xmmintrin.h>

#undef memcpy
#undef memset

// Sizes from here on go 16 bytes at a time
#define XBOX_COPY_SSE_MIN 48
// Without it the compiler can recognize a copy loop and turn it into a call to... this
#define XBOX_NOT_A_MEMCPY() __asm__ volatile("" : : : "memory")

void *memcpy(void *restrict dest, const void *restrict src, size_t n)
{
    void  *d = dest;
    size_t words, bytes;

    if (n >= XBOX_COPY_SSE_MIN) {
        char       *to   = dest;
        const char *from = src;
        size_t      head = (size_t)(-(ptrdiff_t)to) & 15;

        n -= head;
        while (head--) {
            *to++ = *from++;
        }
        if (((size_t)from & 15) == 0) {
            for (; n >= 16; n -= 16, to += 16, from += 16) {
                _mm_store_ps((float *)to, _mm_load_ps((const float *)from));
                XBOX_NOT_A_MEMCPY();
            }
        } else {
            for (; n >= 16; n -= 16, to += 16, from += 16) {
                _mm_store_ps((float *)to, _mm_loadu_ps((const float *)from));
                XBOX_NOT_A_MEMCPY();
            }
        }
        d   = to;
        src = from;
    }

    words = n >> 2;
    bytes = n & 3;

    __asm__ volatile("rep movsl\n\t"
                     "movl %[bytes], %%ecx\n\t"
                     "rep movsb"
                     : "+D"(d), "+S"(src), "+c"(words)
                     : [bytes] "r"(bytes)
                     : "memory");
    return dest;
}

void *memmove(void *dest, const void *src, size_t n)
{
    if ((const char *)dest <= (const char *)src || (const char *)dest >= (const char *)src + n)
        return memcpy(dest, src, n);

    // overlapping with dest after src: copy from the end
    {
        const char *s = (const char *)src + n;
        char       *d = (char *)dest + n;
        size_t      words = n >> 2, bytes = n & 3;

        while (bytes--)
            *--d = *--s;
        while (words--) {
            d -= 4;
            s -= 4;
            *(unsigned int *)d = *(const unsigned int *)s;
        }
    }
    return dest;
}

void *memset(void *dest, int c, size_t n)
{
    void        *d = dest;
    unsigned int value = (unsigned char)c * 0x01010101u;
    size_t       words = n >> 2, bytes = n & 3;

    __asm__ volatile("rep stosl\n\t"
                     "movl %[bytes], %%ecx\n\t"
                     "rep stosb"
                     : "+D"(d), "+c"(words)
                     : "a"(value), [bytes] "r"(bytes)
                     : "memory");
    return dest;
}
