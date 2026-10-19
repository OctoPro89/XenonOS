#include <memory/heap.h>
#include <xlibc/xassert.h>

#define XIMAGE_IMPLEMENTATION
#define XIMAGE_MALLOC(sz) kmalloc(sz)
#define XIMAGE_REALLOC(p, newsz) krealloc((p), (newsz))
#define XIMAGE_FREE(p) kfree(p)
#define XIMAGE_ASSERT(x) xassert(x, "IMAGE ERROR")

// disable TLS variables
#define XIMAGE_NO_THREAD_LOCALS

// TODO: implement functions
#define XIMAGE_NO_HDR
#define XIMAGE_NO_LINEAR

// don't use ungetc, feof, or ferror
#define XIMAGE_NO_STDIO

#include <graphics/ximage.h>