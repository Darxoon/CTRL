/**
 * Boost Software License - Version 1.0 - August 17th, 2003
 * Copyright (c) 2024-2026 Kynex7510
 * See the LICENSE file for more info.
 */

#include <CTRL/RingAllocator.h>
#include <CTRL/Memory.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define ERR_NO_MEM MAKERESULT(RL_STATUS, RS_OUTOFRESOURCE, RM_OS, 0x0A)

static void logResult(const char* msg, Result res) {
    char buf[0x100];
    snprintf(buf, sizeof(buf), "findFreeRange: %s (%d %d)\n", msg, R_SUMMARY(res), R_DESCRIPTION(res));
    svcOutputDebugString(buf, strlen(buf));
}

static void logArgs(const char* msg, ...) {
    va_list args;
    va_start(args, msg);
    
    char buf[0x100];
    vsnprintf(buf, sizeof(buf), msg, args);
    
    va_end(args);
    svcOutputDebugString(buf, strlen(buf));
}


static Result findFreeRange(Handle proc, size_t numPages, size_t curIndex, size_t maxIndex, size_t* outPageIndex) {
    u32 curAddr = ctrlPageIndexToAddr(curIndex);

    while (curAddr < ctrlPageIndexToAddr(maxIndex)) {
        MemInfo memInfo;
        Result ret = ctrlQueryMemoryRegion(proc, curAddr, &memInfo);
        if (R_FAILED(ret)) {
            logResult("ctrlQueryMemoryRegion failed", ret);
            return ret;
        }

        logArgs("findFreeRange: %d %#x %#x\n", memInfo.state, memInfo.base_addr, memInfo.size);
        
        if (memInfo.state == MEMSTATE_FREE && ctrlSizeToNumPages(memInfo.size) >= numPages) {
            *outPageIndex = ctrlAddrToPageIndex(memInfo.base_addr);
            return 0;
        }

        curAddr = memInfo.base_addr + memInfo.size;
    }

    logResult("no memory", ERR_NO_MEM);
    return ERR_NO_MEM;
}

void ctrlRingAllocatorInit(CTRLRingAllocator* a, Handle proc, u32 base, size_t size) {
    a->proc = proc;
    a->base = ctrlAddrToPageIndex(base);
    a->max = ctrlAddrToPageIndex(ctrlAlignUp(base + size, CTRL_PAGE_SIZE));
    a->offset = 0;
}

Result ctrlRingAllocatorReservePages(CTRLRingAllocator* a, size_t numPages, size_t* outPageIndex) {
     Result ret = findFreeRange(a->proc, numPages, a->base + a->offset, a->max, outPageIndex);

    if (R_FAILED(ret))
        ret = findFreeRange(a->proc, numPages, a->base, a->base + a->offset, outPageIndex);

    if (R_SUCCEEDED(ret))
        a->offset = (*outPageIndex + numPages) - a->base;

    return ret;
}