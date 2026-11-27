#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <limits>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace GpuSelection
{
    // SetupAPI uses PCI\..., while driver APIs may return \\?\PCI#...#{GUID}.
    inline std::wstring NormalizeDeviceId(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), towupper);
        const auto pci = value.find(L"PCI");
        if (pci != std::wstring::npos) value.erase(0, pci);
        const auto interface_guid = value.find(L'{');
        if (interface_guid != std::wstring::npos) value.resize(interface_guid);
        value.erase(std::remove_if(value.begin(), value.end(), [](wchar_t c) {
            return !iswalnum(c);
        }), value.end());
        return value;
    }

    struct Luid
    {
        uint32_t high{};
        uint32_t low{};
        bool operator==(const Luid& other) const { return high == other.high && low == other.low; }
    };

    struct Engine
    {
        Luid adapter;
        uint32_t physical{};
        uint32_t engine{};
    };

    inline bool ReadNumber(const std::wstring& text, size_t& at, const wchar_t* separator, int base, uint32_t& value)
    {
        const auto end = text.find(separator, at);
        if (end == std::wstring::npos || end == at) return false;
        const auto token = text.substr(at, end - at);
        if (token.find_first_not_of(base == 16 ? L"0123456789abcdefABCDEF" : L"0123456789") != std::wstring::npos)
            return false;
        try
        {
            const auto number = std::stoull(token, nullptr, base);
            if (number > (std::numeric_limits<uint32_t>::max)()) return false;
            value = static_cast<uint32_t>(number);
        }
        catch (...) { return false; }
        at = end + std::wstring(separator).size();
        return true;
    }

    inline bool ParseEngine(const std::wstring& name, Engine& result)
    {
        const auto start = name.find(L"luid_0x");
        if (start == std::wstring::npos || (start && name[start - 1] != L'_')) return false;
        size_t at = start + 7;
        if (!ReadNumber(name, at, L"_0x", 16, result.adapter.high) ||
            !ReadNumber(name, at, L"_phys_", 16, result.adapter.low) ||
            !ReadNumber(name, at, L"_eng_", 10, result.physical) ||
            !ReadNumber(name, at, L"_engtype_", 10, result.engine)) return false;
        return at < name.size();
    }

    struct Sample { std::wstring name; double value; };

    // Adapter-level counters only: process counters may count shared allocations twice.
    inline bool DedicatedMemory(const std::vector<Sample>& samples, const Luid& selected, uint64_t& bytes)
    {
        std::map<uint32_t, uint64_t> physical_adapters;
        for (const auto& sample : samples)
        {
            if (sample.name.compare(0, 7, L"luid_0x") != 0) continue;
            size_t at = 7;
            Luid adapter;
            uint32_t physical;
            const auto name = sample.name + L"_end";
            if (!ReadNumber(name, at, L"_0x", 16, adapter.high) ||
                !ReadNumber(name, at, L"_phys_", 16, adapter.low) ||
                !ReadNumber(name, at, L"_end", 10, physical) || at != name.size() ||
                !(adapter == selected)) continue;
            if (!std::isfinite(sample.value) || sample.value < 0 || sample.value >= 9223372036854775808.0)
                return false;
            const auto value = static_cast<uint64_t>(sample.value);
            const auto inserted = physical_adapters.emplace(physical, value);
            if (!inserted.second && inserted.first->second != value) return false;
        }
        if (physical_adapters.empty()) return false; // Missing is not zero usage.
        uint64_t total = 0;
        for (const auto& physical : physical_adapters)
        {
            if (physical.second > static_cast<uint64_t>((std::numeric_limits<int64_t>::max)()) - total)
                return false;
            total += physical.second;
        }
        bytes = total;
        return true;
    }

    inline bool Usage(const std::vector<Sample>& samples, const Luid& selected, int& usage)
    {
        std::map<std::tuple<uint32_t, uint32_t, uint32_t, uint32_t>, double> engines;
        bool has_valid_counters = false;
        for (const auto& sample : samples)
        {
            Engine engine;
            if (!std::isfinite(sample.value) || sample.value < 0 || !ParseEngine(sample.name, engine)) continue;
            has_valid_counters = true;
            if (!(engine.adapter == selected)) continue;
            // Sum processes sharing ONE physical engine, never different GPUs or engines.
            engines[std::make_tuple(engine.adapter.high, engine.adapter.low, engine.physical, engine.engine)] += sample.value;
        }
        if (!has_valid_counters) return false;
        double busiest = 0;
        for (const auto& engine : engines) busiest = (std::max)(busiest, engine.second);
        usage = static_cast<int>((std::min)(100.0, busiest) + 0.5);
        return true;
    }
}
