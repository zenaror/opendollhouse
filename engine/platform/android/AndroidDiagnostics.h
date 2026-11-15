/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include <cstdio>
#include <malloc.h>
#include <unistd.h>

namespace odh::android {
// Android's Linux process resident/virtual pages are separate from allocator
// statistics; an allocator free block is not free system memory.
inline void MemoryText(char* buffer, size_t size) {
    const auto heap=mallinfo();
    unsigned long virtualPages=0,residentPages=0;
    const long pageSize=sysconf(_SC_PAGESIZE);
    int processStatus=-1;
    if(FILE* status=std::fopen("/proc/self/statm","r")){
        if(std::fscanf(status,"%lu %lu",&virtualPages,&residentPages)==2 && pageSize>0)
            processStatus=0;
        std::fclose(status);
    }
    const auto bytes=[&](unsigned long pages){
        return processStatus==0 ? static_cast<unsigned long long>(pages)*static_cast<unsigned long long>(pageSize) : 0ull;
    };
    std::snprintf(buffer,size,
        "heap_used=%llu heap_arena=%llu heap_free_blocks=%llu process_virtual_bytes=%llu process_rss_bytes=%llu process_memory_status=%d",
        static_cast<unsigned long long>(heap.uordblks),static_cast<unsigned long long>(heap.arena),
        static_cast<unsigned long long>(heap.fordblks),bytes(virtualPages),bytes(residentPages),processStatus);
}
}
