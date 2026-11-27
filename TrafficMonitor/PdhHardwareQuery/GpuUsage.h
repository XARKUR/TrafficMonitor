#pragma once
#include "PdhQuery.h"
#include "GpuAdapters.h"

// GPU利用率监控类
class CPdhGPUUsage : public CPdhQuery
{
public:
    CPdhGPUUsage();
    ~CPdhGPUUsage();

    // 获取GPU利用率（0-100）
    // 返回值: true=成功, false=失败
    // 注意：首次调用通常返回false（PDH需要预热），建议间隔200ms以上再次调用
    bool GetGpuUsage(int& usage, const std::wstring& selected_device, std::wstring& resolved_device);
    bool GetGpuMemory(const std::wstring& resolved_device, uint64_t& used, uint64_t& total);

private:
    // A separate PDH query keeps memory sampling independent of engine availability.
    class MemoryQuery : public CPdhQuery
    {
    public:
        MemoryQuery() : CPdhQuery(_T("\\GPU Adapter Memory(*)\\Dedicated Usage")) {}
        bool Read(const GpuSelection::Luid& luid, uint64_t& bytes)
        {
            if (!isInitialized && !Initialize()) return false;
            std::vector<CounterValueItem> values;
            if (!QueryValues(values)) return false;
            std::vector<GpuSelection::Sample> samples;
            for (const auto& item : values) samples.push_back({item.name, item.value});
            return GpuSelection::DedicatedMemory(samples, luid, bytes);
        }
    } m_memory_query;
    std::vector<GpuAdapter> m_adapters;
    DWORD m_last_enumeration{};

};
