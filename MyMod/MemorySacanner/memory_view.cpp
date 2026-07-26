#include "SpecialK/stdafx.h"
#include "memory_view.h"
#include <psapi.h>

std::atomic<ChainIterationState> chain_iterations = ChainIterationState::Initializing;
std::atomic<size_t> number_of_addresses_in_search = 0;
std::atomic<size_t> total_addresses_found = 0;
std::atomic<long long unsigned int> bytes_read = 0;
size_t chains_created = 0;
std::atomic<size_t> snapshot_size = 0;
std::unordered_map<uintptr_t, LinksToValue> chain_links;
int data_type_size=1;

std::string Chain::to_string() const
{
    std::string result;
    auto permPtr = GetPermanentAddress(rootAddress);
    if (permPtr.moduleName != "")
    {
        result += permPtr.moduleName + " + 0x" + std::to_string(permPtr.relativeOffset);
    }
    else
    {
        result += "0x" + std::to_string(rootAddress);
    }

    for (const auto& offset : std::views::reverse(offsets))
    {
        result += " -> 0x" + std::to_string(offset);
    }
    return result;
}

PermanentPointer GetPermanentAddress(uintptr_t absoluteAddress)
{
    HMODULE hMod;
    char modulePath[MAX_PATH];

    // Find the module containing this address
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)absoluteAddress, &hMod))
    {
        GetModuleFileNameA(hMod, modulePath, MAX_PATH);
        std::string name = strrchr(modulePath, '\\') + 1; // Get just the filename

        return {name, absoluteAddress - (uintptr_t)hMod};
    }
    return {"", 0};
}

uintptr_t ResolvePermanentPointer(const PermanentPointer& pptr)
{
    // 1. Get the base address of the module by name
    // GetModuleHandle looks up the module in the current process's load list
    HMODULE hMod = GetModuleHandleA(pptr.moduleName.c_str());

    if (hMod == NULL)
    {
        // This can happen if the game hasn't loaded that specific DLL yet
        // (e.g., UnityPlayer.dll might not load until the splash screen ends)
        return 0;
    }

    // 2. Add the relative offset to the base address
    uintptr_t absoluteAddress = (uintptr_t)hMod + pptr.relativeOffset;

    return absoluteAddress;
}

bool canBePermanent(uintptr_t absoluteAddress)
{
    HMODULE hMod;
    return GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              (LPCSTR)absoluteAddress, &hMod) != 0;
}

static HMODULE ThisModule()
{
    HMODULE h = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&ThisModule, &h);
    return h;
}

std::vector<ScanResult> InitialScan(InitFilter& filter)
{
    HMODULE currentModule = ThisModule();

    std::vector<ScanResult> candidates;

    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);

    uintptr_t currentAddress = (uintptr_t)sysInfo.lpMinimumApplicationAddress;
    uintptr_t maxAddress = (uintptr_t)sysInfo.lpMaximumApplicationAddress;

    MEMORY_BASIC_INFORMATION mbi;

    // 64KB chunks
    const size_t CHUNK_SIZE = 64 * 1024;
    std::vector<char> buffer(CHUNK_SIZE);

    // Loop through memory regions
    while (currentAddress < maxAddress)
    {
        if (VirtualQuery((LPCVOID)currentAddress, &mbi, sizeof(mbi)))
        {
            if ((HMODULE)mbi.AllocationBase == currentModule)
            {
                currentAddress += mbi.RegionSize; continue;
            }
            // Check if the memory is committed and we have read access
            if (mbi.State == MEM_COMMIT &&
                (mbi.Protect == PAGE_READONLY ||
                    mbi.Protect == PAGE_READWRITE
                    || mbi.Protect == PAGE_EXECUTE_READ
                    || mbi.Protect == PAGE_EXECUTE_READWRITE
                ))
            {
                // Iterate through the current region in chunks
                size_t regionOffset = 0;
                while (regionOffset < mbi.RegionSize)
                {
                    // Calculate how much to read.
                    // We want to read CHUNK_SIZE, but trim if it exceeds region bounds.
                    size_t bytesToRead = CHUNK_SIZE;
                    if (regionOffset + bytesToRead > mbi.RegionSize)
                    {
                        bytesToRead = mbi.RegionSize - regionOffset;
                    }

                    SIZE_T bytesRead;
                    uintptr_t readAddress = (uintptr_t)mbi.BaseAddress + regionOffset;

                    if (ReadProcessMemory(GetCurrentProcess(), (LPCVOID)readAddress, buffer.data(), bytesToRead,
                                          &bytesRead))
                    {
                        // Determine the safe loop limit.
                        // We can only check patterns that fully fit inside the buffer.
                        // So we stop at: buffer_end - pattern_size
                        // However, 'bytesRead' might be smaller than 'filter.number_of_bytes' if the region is tiny.
                        if (bytesRead >= (size_t)filter.number_of_bytes)
                        {
                            size_t loopLimit = bytesRead - filter.number_of_bytes;

                            for (size_t i = 0; i <= loopLimit; i += 4)
                            {

                                if (filter(&buffer[i]))
                                {
                                    std::vector<char> value(filter.number_of_bytes);
                                    std::memcpy(value.data(),buffer.data()+i,filter.number_of_bytes);

                                    candidates.push_back({readAddress + i, value});

                                    if (candidates.size() >= 100000)
                                    {
                                        return candidates;
                                    }
                                }
                            }
                        }
                    }

                    // Advance the offset.
                    // CRITICAL: To cover edges, we must overlap the next read.
                    // We advance by 'bytesRead', but back/overlap by 'filter.number_of_bytes' (aligned to 4 bytes).
                    // This ensures the pattern starting at the very end of this chunk is re-evaluated
                    // as the start of the next chunk.

                    if (bytesRead < bytesToRead)
                    {
                        // Read failed completely or partially at OS level, just skip strictly to avoid infinite loops
                        regionOffset += bytesToRead;
                    }
                    else
                    {
                        // Ensure we don't negative overflow if the chunk was tiny
                        size_t overlap = (bytesToRead > (size_t)filter.number_of_bytes) ? filter.number_of_bytes : 0;

                        // Align overlap to 4 bytes (since we step by 4) to keep alignment consistent
                        overlap = (overlap / 4) * 4;

                        regionOffset += (bytesToRead - overlap);
                    }
                }
            }
            // Move to the next memory region
            currentAddress += mbi.RegionSize;
        }
        else
        {
            break; // VirtualQuery failed, exit loop
        }
    }
    return candidates;
}


void FilterValues(std::vector<ScanResult>& candidates,const InitFilter& filter)
{
    std::vector<ScanResult> nextCandidates;
    float currentValue;
    std::vector<char> buffer(filter.number_of_bytes);
    SIZE_T bytesRead;
    for (const auto& candidate : candidates) {
        // Safely read the memory at our candidate address
        if (ReadProcessMemory(GetCurrentProcess(), (LPCVOID)candidate.address, buffer.data(), filter.number_of_bytes, &bytesRead)) {
            // Check if the value is in the specified range
            if (filter(buffer.data())) {
                currentValue = *(float*)buffer.data();
                // Keep it and update the previous value for the next scan
                nextCandidates.push_back(candidate);
            }
        }
    }
    // Overwrite the old list with the filtered list
    candidates = nextCandidates;
}

void SafeWriteFloat(uintptr_t address, float value)
{
    DWORD oldProtect;

    // 1. Change memory protection to READWRITE
    // We target 4 bytes because a float is 4 bytes
    if (VirtualProtect((LPVOID)address, sizeof(float), PAGE_READWRITE, &oldProtect))
    {
        // 2. Perform the write
        *(float*)address = value;

        // 3. Restore the original protection (critical for stability!)
        VirtualProtect((LPVOID)address, sizeof(float), oldProtect, &oldProtect);
    }
}

uintptr_t get_relative_address(uintptr_t absolute)
{
    return absolute - (uintptr_t)GetModuleHandle(NULL);
}

uintptr_t get_absolute_address(uintptr_t relative)
{
    return relative + (uintptr_t)GetModuleHandle(NULL);
}

ScanResult load_chain(const Chain& chain,int value_size)
{
    SIZE_T bytesRead;
    auto currentAddress = chain.rootAddress;
    for (int i = (int)chain.offsets.size() - 1; i >= 0; --i)
    {
        uintptr_t nextAddress;

        if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)currentAddress, &nextAddress, sizeof(uintptr_t),
                               &bytesRead))
        {
            return {0};
        }
        currentAddress = nextAddress + chain.offsets[i];
    }


    std::vector<char> value(value_size);
    if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)currentAddress, value.data(), value.size(), &bytesRead))
    {
        return {0};
    }

    return {currentAddress, value, std::nullopt, chain};
}

bool try_load_chain(const Chain& chain)
{
    SIZE_T bytesRead;
    auto currentAddress = chain.rootAddress;
    for (int i = (int)chain.offsets.size() - 1; i >= 0; --i)
    {
        uintptr_t nextAddress;

        if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)currentAddress, &nextAddress, sizeof(uintptr_t),
                               &bytesRead))
        {
            return false;
        }
        currentAddress = nextAddress + chain.offsets[i];
    }


    float value;
    if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)currentAddress, &value, sizeof(float), &bytesRead))
    {
        return false;
    }

    return true;
}


void load_chain_recursive(std::vector<ScanResult>& candidates, const Chain& chain, int max_depth)
{
    if (canBePermanent(chain.rootAddress))
    {
        auto scan = load_chain(chain,1);
        if (scan.isValid())
        {
            candidates.push_back(scan);
        }
    }
    chains_created++;
    auto address = chain.rootAddress;
    if (chain.offsets.size() >= (size_t)max_depth)
    {
        return;
    }
    for (const auto& l : chain_links[address].links)
    {
        Chain new_chain = chain;
        new_chain.rootAddress = l.ptr;
        new_chain.offsets.push_back(l.offset);
        load_chain_recursive(candidates, new_chain, max_depth);
    }
}

template<typename T>
inline bool pointer_outside_vector(const std::vector<T>& vector, uintptr_t ptr)
{
    auto vptr_begin=reinterpret_cast<uintptr_t>(vector.data());
    auto vptr_end=reinterpret_cast<uintptr_t>(vector.data())+vector.capacity()*sizeof(T);

    return ptr < vptr_begin || ptr>vptr_end;
}

std::vector<ScanResult> SearchForChains(uintptr_t initialAddress, int max_depth, int max_offset,int type_size)
{
    data_type_size=type_size;

    chain_iterations = ChainIterationState::SnapshotPhase;

    // 1. Snapshot Phase: Read all valid memory and find *all* pointer-like values once
    std::vector<std::pair<uintptr_t, uintptr_t>> pointer_map; // <value_pointed_to, address_of_pointer>

    // Reserve estimation to prevent reallocations
    pointer_map.reserve(1000000);

    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);

    uintptr_t minAppAddr = (uintptr_t)sysInfo.lpMinimumApplicationAddress;
    uintptr_t maxAppAddr = (uintptr_t)sysInfo.lpMaximumApplicationAddress;
    uintptr_t currentAddress = minAppAddr;
    MEMORY_BASIC_INFORMATION mbi;

    bytes_read = 0;

    // Buffer size for chunked reading (e.g., 256KB)
    const size_t CHUNK_SIZE = 256 * 1024;
    std::vector<char> buffer(CHUNK_SIZE);

    while (currentAddress < maxAppAddr)
    {
        if (VirtualQuery((LPCVOID)currentAddress, &mbi, sizeof(mbi)))
        {
            if (mbi.State == MEM_COMMIT &&
                (mbi.Protect == PAGE_READONLY || mbi.Protect == PAGE_READWRITE ||
                    mbi.Protect == PAGE_EXECUTE_READ || mbi.Protect == PAGE_EXECUTE_READWRITE))
            {
                // Iterate over the region in chunks
                for (uintptr_t chunkAddr = (uintptr_t)mbi.BaseAddress;
                     chunkAddr < (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
                     chunkAddr += CHUNK_SIZE)
                {
                    size_t currentReadSize = std::min(
                        CHUNK_SIZE, (size_t)(mbi.RegionSize - (chunkAddr - (uintptr_t)mbi.BaseAddress)));
                    SIZE_T bytesReadSize;

                    // Read only a small chunk
                    if (ReadProcessMemory(GetCurrentProcess(), (LPCVOID)chunkAddr, buffer.data(), currentReadSize,
                                          &bytesReadSize))
                    {
                        bytes_read += bytesReadSize;

                        uintptr_t* ptrs = (uintptr_t*)buffer.data();
                        size_t count = bytesReadSize / sizeof(uintptr_t);

                        for (size_t i = 0; i < count; i++)
                        {
                            uintptr_t val = ptrs[i];
                            if (val >= minAppAddr && val < maxAppAddr)
                            {
                                // Determine actual address of this pointer
                                uintptr_t ptrLocation = chunkAddr + i * sizeof(uintptr_t);
                                if (pointer_outside_vector(pointer_map,val)&&pointer_outside_vector(pointer_map,ptrLocation))
                                    pointer_map.emplace_back(val, ptrLocation);
                            }
                        }
                    }
                }
            }
            currentAddress += mbi.RegionSize;
        }
        else
        {
            break;
        }

        snapshot_size = pointer_map.size();
    }

    chain_iterations = ChainIterationState::SortingPointers;

    // 2. Sort the map to allow binary search (making lookups O(log N) instead of O(N))
    std::sort(pointer_map.begin(), pointer_map.end());


    // 3. Resolve Chains
    std::unordered_set<uintptr_t> nodes_to_expand = {initialAddress};
    std::unordered_set<uintptr_t> visited_nodes;

    total_addresses_found = 0;
    chain_links.clear();

    for (int depth = 0; depth < max_depth; ++depth)
    {
        chain_iterations = static_cast<ChainIterationState>(depth);
        number_of_addresses_in_search = nodes_to_expand.size();
        std::unordered_set<uintptr_t> next_nodes;

        for (uintptr_t target : nodes_to_expand)
        {
            uintptr_t search_min = target > (uintptr_t)max_offset ? target - max_offset : 0;
            uintptr_t search_max = target;

            // Binary search for the first pointer value >= search_min
            auto it = std::lower_bound(pointer_map.begin(), pointer_map.end(),
                                       std::make_pair(search_min, (uintptr_t)0));

            while (it != pointer_map.end() && it->first <= search_max)
            {
                uintptr_t pointer_val = it->first;
                uintptr_t pointer_loc = it->second;

                int offset = (int)(target - pointer_val);

                // Avoid loops/cycles
                if (visited_nodes.find(pointer_loc) == visited_nodes.end())
                {
                    next_nodes.insert(pointer_loc);
                    visited_nodes.insert(pointer_loc);

                    chain_links[target].links.push_back({pointer_loc, (uintptr_t)offset});

                    total_addresses_found++;
                }

                ++it;
            }
        }

        if (next_nodes.empty())
            break;
        nodes_to_expand = std::move(next_nodes);
    }

    pointer_map.clear(); // Free memory

    //Last sage - load chains to candidates
    chain_iterations = ChainIterationState::BuildingChains;
    chains_created = 0;

    std::vector<ScanResult> candidates;

    //load chains to candidates
    const Chain chain(initialAddress);
    load_chain_recursive(candidates, chain, max_depth);

    chain_iterations = ChainIterationState::Finished;

    return candidates;
}

ProjectionMatFilter::ProjectionMatFilter(float ratio)
{
    number_of_bytes = 64;
    m_ratio = ratio;
}


bool ProjectionMatFilter::operator()(void* data) const
{
    float eps = 0.001f;
    float* data_f = static_cast<float*>(data);
    float x = data_f[0];
    float y = data_f[5];

    //if (abs(y - 2.414174556732) > eps)
    //  return false;


    float ratio = y / x;
    if (abs(abs(ratio) - m_ratio) < eps)
    {
        bool res = true;
        res = res && (data_f[1] == 0.0f);
        res = res && (data_f[2] == 0.0f);
        res = res && (data_f[3] == 0.0f);

        res = res && (data_f[4] == 0.0f);
        res = res && (data_f[6] == 0.0f);
        res = res && (data_f[7] == 0.0f);

        res = res && (data_f[8] == 0.0f);
        res = res && (data_f[9] == 0.0f);
        res = res && (data_f[10] != 0.0f);
        res = res && (data_f[11] != 0.0f);

        res = res && (data_f[12] == 0.0f);
        res = res && (data_f[13] == 0.0f);
        //res =res && (abs(data_f[14]-(-0.2f)) < eps);
        res = res && (data_f[14] != 0.0f);
        res = res && (data_f[15] == 0.0f);

        return res;
    }
    else
    {
        return false;
    }
}
