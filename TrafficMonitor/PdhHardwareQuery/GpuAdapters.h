#pragma once
#include <string>
#include <vector>
#include "GpuSelection.h"

struct GpuAdapter
{
    std::wstring device_id;       // Persistent PnP device instance ID, NOT a GPU number or boot-local LUID.
    std::wstring name;
    GpuSelection::Luid luid;
    unsigned long long dedicated_memory{};
};

std::vector<GpuAdapter> EnumerateGpuAdapters();
inline const GpuAdapter* SelectGpuAdapter(const std::vector<GpuAdapter>& adapters, const std::wstring& device_id)
{
    if (adapters.empty()) return nullptr;
    if (device_id.empty())
        return &*std::max_element(adapters.begin(), adapters.end(), [](const GpuAdapter& a, const GpuAdapter& b) {
            return a.dedicated_memory < b.dedicated_memory;
        });
    const auto identity = GpuSelection::NormalizeDeviceId(device_id);
    const auto found = std::find_if(adapters.begin(), adapters.end(), [&](const GpuAdapter& adapter) {
        return GpuSelection::NormalizeDeviceId(adapter.device_id) == identity;
    });
    return found == adapters.end() ? nullptr : &*found;
}
