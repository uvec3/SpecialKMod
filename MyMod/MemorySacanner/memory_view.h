#pragma once

#include "Value.h"
#include "../json.hpp"

enum class ChainIterationState : int
{
    Finished = -1,
    SortingPointers = -2,
    SnapshotPhase = -3,
    Initializing = -4,
    BuildingChains = -5,
    SearchingChains = 0 // 0 to max_depth for actual depth values
};

void SafeWriteFloat(uintptr_t address, float value);


struct PermanentPointer
{
    std::string moduleName;
    uintptr_t relativeOffset;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PermanentPointer, moduleName, relativeOffset)

struct Chain
{
    uintptr_t rootAddress;
    std::vector<uintptr_t> offsets;


    std::string to_string() const;
};


inline std::vector<char> Xor(const std::vector<char>& input)
{
    uint32_t key = 0x12456789u;

    std::vector<char> output(input.size());
    for (int i = 0; i < static_cast<int>(input.size()); i += 4)
    {
        if (input.size() - i < 4)
        {
            output[i]=static_cast<char>(input[i]^key);
        }

        uint32_t v;
        std::memcpy(&v, input.data() + i, 4);
        v ^= key;
        std::memcpy(output.data() + i, &v, 4);
    }

    return output;
}

struct ScanResult
{
private:
    std::vector<char> m_previous_value;

public:
    ScanResult () = default;
    ScanResult (uintptr_t addr, const std::vector<char>& prev_value={}, std::optional<float> forced_val = std::nullopt,
                std::optional<Chain> chain = std::nullopt)
        : address(addr), forcedValue(forced_val), attachedChain(chain)
    {
        set_previous_value(prev_value);
    }

    uintptr_t address = 0;
    std::optional<float> forcedValue;
    std::optional<Chain> attachedChain;

    inline bool isValid() const
    {
        return address != 0;
    }

    void set_previous_value(const std::vector<char>& previous_value)
    {
        m_previous_value=Xor(previous_value);
    }

    std::vector<char> previous_value() const
    {
        return Xor(m_previous_value);
    }

    size_t previous_value_size() const
    {
        return m_previous_value.size();
    }

    void setValue(const Value& value)
    {
        SafeWriteFloat(address, value.value);
    }

    void resetValue()
    {
        if (previous_value_size()<sizeof(float))
            return;
        float fv;
        auto buff=previous_value();
        memcpy(&fv,buff.data(),sizeof(fv));
        SafeWriteFloat(address, fv);
    }
};

struct SavedResult
{
    std::string label;
    uintptr_t address = 0;
    Chain attachedChain;
    std::optional<float> forcedValue;
};

struct Link
{
    uintptr_t ptr;
    uintptr_t offset;
};

struct LinksToValue
{
    std::vector<Link> links;
};

struct InitFilter
{
    int number_of_bytes;
    virtual bool operator()(void* data) const = 0;

    virtual ~InitFilter()
    {
    };
};

struct Filter
{
    int number_of_bytes;
    virtual bool operator()(const std::vector<char>& oldData, const std::vector<char>& newData) const = 0;

    virtual ~Filter()
    {
    };
};

template <typename INIT_FILTER>
struct FilterFromInitFilterAdapter : Filter
{
    INIT_FILTER* m_init_filter;

    FilterFromInitFilterAdapter(INIT_FILTER& init_filter)
    {
        number_of_bytes = init_filter.number_of_bytes;
        m_init_filter = &init_filter;
    }

    bool operator()(const std::vector<char>& oldData, const std::vector<char>& newData) const override
    {
        return true;
        oldData;
        return (*m_init_filter)((void*)newData.data());
    }
};


struct ProjectionMatFilter : InitFilter
{
    float m_ratio = 1;
    ProjectionMatFilter(float ratio);

    virtual bool operator()(void* data) const;
};


PermanentPointer GetPermanentAddress(uintptr_t absoluteAddress);
uintptr_t ResolvePermanentPointer(const PermanentPointer& pptr);
bool canBePermanent(uintptr_t absoluteAddress);


std::vector<ScanResult> InitialScan(InitFilter& filter);

void FilterValues(std::vector<ScanResult>& candidates, const InitFilter& filter);




uintptr_t get_relative_address(uintptr_t absolute);

uintptr_t get_absolute_address(uintptr_t relative);


ScanResult load_chain(const Chain& chain, int value_size);

void load_chain_recursive(std::vector<ScanResult>& candidates, const Chain& chain, int max_depth);

std::vector<ScanResult> SearchForChains(uintptr_t initialAddress, int max_depth, int max_offset,int type_size);

extern std::atomic<ChainIterationState> chain_iterations;
extern std::atomic<size_t> number_of_addresses_in_search;
extern std::atomic<size_t> total_addresses_found;
extern std::atomic<long long unsigned int> bytes_read;
extern size_t chains_created;
extern std::atomic<size_t> snapshot_size;
