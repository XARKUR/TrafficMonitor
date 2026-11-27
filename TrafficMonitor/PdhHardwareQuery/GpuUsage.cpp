#include "stdafx.h"
#include "GpuUsage.h"

CPdhGPUUsage::CPdhGPUUsage()
    : CPdhQuery(_T("\\GPU Engine(*)\\Utilization Percentage"))
{
}

CPdhGPUUsage::~CPdhGPUUsage() = default;

bool CPdhGPUUsage::GetGpuMemory(const std::wstring& resolved_device, uint64_t& used, uint64_t& total)
{
    const auto* adapter = SelectGpuAdapter(m_adapters, resolved_device);
    total = adapter ? adapter->dedicated_memory : 0;
    return adapter && m_memory_query.Read(adapter->luid, used);
}

bool CPdhGPUUsage::GetGpuUsage(int& usage, const std::wstring& selected_device, std::wstring& resolved_device)
{
    const auto now = GetTickCount();
    if (m_adapters.empty() || now - m_last_enumeration >= 5000)
    {
        m_adapters = EnumerateGpuAdapters();
        m_last_enumeration = now;
    }
    const auto* adapter = SelectGpuAdapter(m_adapters, selected_device);
    resolved_device = adapter ? adapter->device_id : (selected_device.empty() ? L"unavailable" : selected_device);
    if (!adapter || !isInitialized) return false;
    std::vector<CounterValueItem> values;
    if (!QueryValues(values)) return false;
    std::vector<GpuSelection::Sample> samples;
    samples.reserve(values.size());
    for (const auto& item : values) samples.push_back({item.name, item.value});
    return GpuSelection::Usage(samples, adapter->luid, usage);
}
