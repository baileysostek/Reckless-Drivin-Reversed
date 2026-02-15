#include "mac_compat.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Handle memory system:
 * A Handle is a pointer to a pointer (char**).
 * We allocate: [4 bytes size][data...]
 * The Handle points to a malloc'd pointer that points to (data start).
 * Size is stored at (data - 4).
 */

static OSErr gLastMemErr = noErr;

typedef struct {
    long size;
    char data[1]; /* variable length */
} HandleBlock;

#define HANDLE_BLOCK(dataPtr) ((HandleBlock*)((char*)(dataPtr) - offsetof(HandleBlock, data)))

Handle NewHandle(long size)
{
    Handle h;
    HandleBlock *block;

    h = (Handle)malloc(sizeof(char*));
    if (!h) { gLastMemErr = memFullErr; return NULL; }

    block = (HandleBlock*)malloc(offsetof(HandleBlock, data) + size);
    if (!block) { free(h); gLastMemErr = memFullErr; return NULL; }

    block->size = size;
    *h = block->data;
    gLastMemErr = noErr;
    return h;
}

void DisposeHandle(Handle h)
{
    if (h) {
        if (*h) {
            HandleBlock *block = HANDLE_BLOCK(*h);
            free(block);
        }
        free(h);
    }
}

long GetHandleSize(Handle h)
{
    if (h && *h) {
        HandleBlock *block = HANDLE_BLOCK(*h);
        return block->size;
    }
    return 0;
}

void SetHandleSize(Handle h, long newSize)
{
    if (h && *h) {
        HandleBlock *block = HANDLE_BLOCK(*h);
        HandleBlock *newBlock = (HandleBlock*)realloc(block, offsetof(HandleBlock, data) + newSize);
        if (newBlock) {
            newBlock->size = newSize;
            *h = newBlock->data;
            gLastMemErr = noErr;
        } else {
            gLastMemErr = memFullErr;
        }
    }
}

void HLock(Handle h) { (void)h; /* no-op */ }
void HLockHi(Handle h) { (void)h; /* no-op */ }
void HUnlock(Handle h) { (void)h; /* no-op */ }

OSErr HandToHand(Handle *h)
{
    Handle src, dst;
    long size;

    if (!h || !*h) return -1;
    src = *h;
    size = GetHandleSize(src);
    dst = NewHandle(size);
    if (!dst) return memFullErr;
    memcpy(*dst, *src, size);
    *h = dst;
    return noErr;
}

OSErr PtrToHand(const void *srcPtr, Handle *dstHndl, long size)
{
    Handle h;
    if (!srcPtr || !dstHndl) return -1;
    h = NewHandle(size);
    if (!h) return memFullErr;
    memcpy(*h, srcPtr, size);
    *dstHndl = h;
    return noErr;
}

Ptr NewPtr(long size)
{
    Ptr p = (Ptr)malloc(size);
    gLastMemErr = p ? noErr : memFullErr;
    return p;
}

Ptr NewPtrClear(long size)
{
    Ptr p = (Ptr)calloc(1, size);
    gLastMemErr = p ? noErr : memFullErr;
    return p;
}

void DisposePtr(Ptr p)
{
    free(p);
}

void BlockMoveData(const void *srcPtr, void *destPtr, long byteCount)
{
    if (srcPtr && destPtr && byteCount > 0)
        memmove(destPtr, srcPtr, byteCount);
}

OSErr MemError(void)
{
    return gLastMemErr;
}

void NumToString(long theNum, Str255 theString)
{
    char buf[32];
    int len;
    snprintf(buf, sizeof(buf), "%ld", theNum);
    len = (int)strlen(buf);
    if (len > 255) len = 255;
    theString[0] = (unsigned char)len;
    memcpy(theString + 1, buf, len);
}

void StringToNum(const Str255 theString, long *theNum)
{
    char buf[256];
    int len = theString[0];
    memcpy(buf, theString + 1, len);
    buf[len] = '\0';
    *theNum = atol(buf);
}

void MacSetRect(Rect *r, short left, short top, short right, short bottom)
{
    r->left = left;
    r->top = top;
    r->right = right;
    r->bottom = bottom;
}

unsigned long TickCountCompat(void)
{
    return (unsigned long)SDL_GetTicks();
}
