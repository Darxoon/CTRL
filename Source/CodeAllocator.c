#include <CTRL/CodeAllocator.h>
#include <CTRL/Memory.h>
#include <CTRL/App.h>

#define HEAP_SPLIT_SIZE_CAP (24 << 20)
#define LINEAR_HEAP_SIZE_CAP (32 << 20)

#define ERR_NO_MEM MAKERESULT(RL_STATUS, RS_OUTOFRESOURCE, RM_OS, 0x0A)

// Defined in libctru.
extern char* fake_heap_start;
extern char* fake_heap_end;

extern u32 __ctru_heap;
extern u32 __ctru_linear_heap;
extern u32 __ctru_heap_size;
extern u32 __ctru_linear_heap_size;

__attribute__((weak)) size_t __ctrl_code_allocator_pages = 0;

static u32 detectCodeRegionStart(void) {
    const CTRLAppSectionInfo* secInfo = ctrlAppSectionInfo();
    return secInfo->textAddr + secInfo->textSize + secInfo->rodataSize + secInfo->dataSize;
}

Result ctrlNextCodeAllocAddress(size_t numPages, u32* outAddr) {
    const u32 codeAllocBegin = OS_HEAP_AREA_BEGIN;
    const u32 codeAllocEnd = codeAllocBegin + ctrlNumPagesToSize(__ctrl_code_allocator_pages);
    const size_t size = ctrlNumPagesToSize(numPages);

    if (!codeAllocBegin || codeAllocBegin == codeAllocEnd)
        return ERR_NO_MEM;

    // Find free space in the region.
    MemInfo info;
    u32 base = codeAllocBegin;

    while (base < codeAllocEnd) {
        Result ret = ctrlQueryMemoryRegion(base, &info);
        if (R_FAILED(ret))
            return ret;

        if (info.state == MEMSTATE_FREE && info.size >= size) {
            *outAddr = base;
            return 0;
        }

        base = info.base_addr + info.size;
    }

    return ERR_NO_MEM;
}

Result ctrlAllocCodePages(size_t numPages, u32* outAddr) {
    u32 base;
    Result ret = ctrlNextCodeAllocAddress(numPages, &base);
    if (R_FAILED(ret))
        return ret;

    u32 dummy;
    ret = svcControlMemory(&dummy, base, 0, ctrlNumPagesToSize(numPages), MEMOP_ALLOC, MEMPERM_READWRITE);
    if (R_SUCCEEDED(ret))
        *outAddr = base;

    return ret;
}

Result ctrlFreeCodePages(u32 allocAddr, size_t numPages) {
    u32 dummy;
    return svcControlMemory(&dummy, allocAddr, 0, ctrlNumPagesToSize(numPages), MEMOP_FREE, 0);
}

Result ctrlCommitCodePages(u32 allocAddr, size_t numPages, u32* outCommitAddr) {
    const u32 codeAllocBase = OS_HEAP_AREA_BEGIN;
    const u32 commitAddr = allocAddr - codeAllocBase + detectCodeRegionStart();
    const size_t size = ctrlNumPagesToSize(numPages);

    // Alias code into code region.
    Result ret = ctrlMapAliasMemory(allocAddr, commitAddr, size);
    if (R_FAILED(ret))
        return ret;

    // Make it executable.
    ret = ctrlChangeMemoryPerms(commitAddr, size, MEMPERM_READEXECUTE);
    if (R_FAILED(ret)) {
        ctrlUnmapAliasMemory(allocAddr, commitAddr, size);
        return ret;
    }

    // Handle cache.
    ctrlFlushDataCache();
    ctrlInvalidateInstructionCache();

    *outCommitAddr = commitAddr;
    return 0;
}

Result ctrlReleaseCodePages(u32 allocAddr, u32 commitAddr, size_t numPages) {
    return ctrlUnmapAliasMemory(allocAddr, commitAddr, ctrlNumPagesToSize(numPages));
}