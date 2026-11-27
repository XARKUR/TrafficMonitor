#include "stdafx.h"
#include "GpuAdapters.h"
#include <atlbase.h>
#include <dxgi1_2.h>
#include <winternl.h>
#include <d3dkmthk.h>
#include <setupapi.h>
#pragma comment(lib, "setupapi.lib")

namespace
{
    struct PciDevice { std::wstring id; DWORD bus{}, device{}, function{}; };
    const GUID display_class = {0x4d36e968, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};

    bool ReadProperty(HDEVINFO list, SP_DEVINFO_DATA& item, DWORD property, DWORD& value)
    {
        DWORD type{}, size{};
        return SetupDiGetDeviceRegistryPropertyW(list, &item, property, &type,
            reinterpret_cast<PBYTE>(&value), sizeof(value), &size) && type == REG_DWORD;
    }

    std::vector<PciDevice> EnumeratePciDevices()
    {
        std::vector<PciDevice> devices;
        auto list = SetupDiGetClassDevsW(&display_class, nullptr, nullptr, DIGCF_PRESENT);
        if (list == INVALID_HANDLE_VALUE) return devices;
        SP_DEVINFO_DATA item{};
        item.cbSize = sizeof(item);
        for (DWORD i = 0; SetupDiEnumDeviceInfo(list, i, &item); ++i)
        {
            DWORD bus{}, address{}, needed{};
            if (!ReadProperty(list, item, SPDRP_BUSNUMBER, bus) || !ReadProperty(list, item, SPDRP_ADDRESS, address)) continue;
            SetupDiGetDeviceInstanceIdW(list, &item, nullptr, 0, &needed);
            if (needed == 0 || needed > 4096) continue;
            std::vector<wchar_t> id(needed);
            if (!SetupDiGetDeviceInstanceIdW(list, &item, id.data(), needed, nullptr)) continue;
            if (std::wstring(id.data()).find(L"PCI\\") != 0) continue;
            devices.push_back({id.data(), bus, address >> 16, address & 0xffff});
        }
        SetupDiDestroyDeviceInfoList(list);
        return devices;
    }

    bool AdapterAddress(const LUID& luid, D3DKMT_ADAPTERADDRESS& address)
    {
        const auto gdi = GetModuleHandleW(L"gdi32.dll");
        if (!gdi) return false;
        const auto open = reinterpret_cast<NTSTATUS (APIENTRY*)(D3DKMT_OPENADAPTERFROMLUID*)>(GetProcAddress(gdi, "D3DKMTOpenAdapterFromLuid"));
        const auto query = reinterpret_cast<NTSTATUS (APIENTRY*)(const D3DKMT_QUERYADAPTERINFO*)>(GetProcAddress(gdi, "D3DKMTQueryAdapterInfo"));
        const auto close = reinterpret_cast<NTSTATUS (APIENTRY*)(const D3DKMT_CLOSEADAPTER*)>(GetProcAddress(gdi, "D3DKMTCloseAdapter"));
        if (!open || !query || !close) return false;
        D3DKMT_OPENADAPTERFROMLUID adapter{};
        adapter.AdapterLuid = luid;
        if (open(&adapter) != 0) return false;
        D3DKMT_QUERYADAPTERINFO info{};
        info.hAdapter = adapter.hAdapter;
        info.Type = KMTQAITYPE_ADAPTERADDRESS;
        info.pPrivateDriverData = &address;
        info.PrivateDriverDataSize = sizeof(address);
        const bool success = query(&info) == 0;
        D3DKMT_CLOSEADAPTER closing{};
        closing.hAdapter = adapter.hAdapter;
        close(&closing);
        return success;
    }
}

std::vector<GpuAdapter> EnumerateGpuAdapters()
{
    std::vector<GpuAdapter> result;
    const auto devices = EnumeratePciDevices();
    wchar_t system[MAX_PATH]{};
    if (!GetSystemDirectoryW(system, MAX_PATH)) return result;
    struct Module { HMODULE handle; ~Module() { if (handle) FreeLibrary(handle); } };
    Module dxgi{LoadLibraryW((std::wstring(system) + L"\\dxgi.dll").c_str())};
    if (!dxgi.handle) return result;
    const auto create = reinterpret_cast<HRESULT (WINAPI*)(REFIID, void**)>(GetProcAddress(dxgi.handle, "CreateDXGIFactory1"));
    if (!create) return result;
    CComPtr<IDXGIFactory1> factory;
    if (FAILED(create(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) return result;
    for (UINT i = 0; ; ++i)
    {
        CComPtr<IDXGIAdapter1> adapter;
        const auto status = factory->EnumAdapters1(i, &adapter);
        if (status == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(status)) break;
        DXGI_ADAPTER_DESC1 description{};
        if (FAILED(adapter->GetDesc1(&description)) || (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) continue;
        D3DKMT_ADAPTERADDRESS address{};
        if (!AdapterAddress(description.AdapterLuid, address)) continue;
        const auto found = std::find_if(devices.begin(), devices.end(), [&](const PciDevice& device) {
            return device.bus == address.BusNumber && device.device == address.DeviceNumber && device.function == address.FunctionNumber;
        });
        if (found == devices.end()) continue; // No identity match: never substitute a different GPU.
        result.push_back({found->id, description.Description,
            {static_cast<uint32_t>(description.AdapterLuid.HighPart), description.AdapterLuid.LowPart},
            static_cast<unsigned long long>(description.DedicatedVideoMemory)});
    }
    return result;
}

