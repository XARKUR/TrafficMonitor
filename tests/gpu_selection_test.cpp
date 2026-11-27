#include "../TrafficMonitor/PdhHardwareQuery/GpuAdapters.h"
#include <cassert>
#include <iostream>
#include <limits>

static std::wstring Counter(int pid, int luid, int physical, int engine, const wchar_t* type = L"3D")
{
    return L"pid_" + std::to_wstring(pid) + L"_luid_0x00000000_0x0000000" + std::to_wstring(luid) +
        L"_phys_" + std::to_wstring(physical) + L"_eng_" + std::to_wstring(engine) + L"_engtype_" + type;
}

int main()
{
    using namespace GpuSelection;
    int usage = -1;
    const std::vector<Sample> simultaneous = {
        {Counter(1, 1, 0, 0), 8}, {Counter(2, 1, 0, 0), 5},
        {Counter(3, 1, 0, 1, L"VideoDecode"), 8},
        {Counter(4, 2, 0, 0), 61},
    };
    assert(Usage(simultaneous, {0,1}, usage) && usage == 13);
    assert(Usage(simultaneous, {0,2}, usage) && usage == 61);
    // Same-type engines and different physical adapters must not be added together.
    assert(Usage({{Counter(1,1,0,0),20}, {Counter(2,1,0,1),30}, {Counter(3,1,1,0),25}}, {0,1}, usage) && usage == 30);
    assert(Usage({{Counter(1,1,0,0),60}, {Counter(2,1,0,0),45}}, {0,1}, usage) && usage == 100);
    // Counter instance names, rather than engine-type labels, carry identity.
    assert(Usage({{Counter(1,1,0,0,L"Compute_0"),72}}, {0,1}, usage) && usage == 72);
    assert(!Usage({{L"_Total",90}, {L"pid_1_luid_0xINVALID_0x1_phys_0_eng_0_engtype_3D",50}}, {0,1}, usage));
    assert(!Usage({{Counter(1,1,0,0),std::numeric_limits<double>::quiet_NaN()}, {Counter(1,1,0,0),-2}}, {0,1}, usage));
    assert(Usage({{Counter(1,2,0,0),75}}, {0,1}, usage) && usage == 0);
    assert(!Usage({}, {0,1}, usage));
    Engine engine;
    assert(ParseEngine(L"pid_7_luid_0xabcdef01_0x12345678_phys_2_eng_3_engtype_Video Decode",engine));
    assert(engine.adapter.high==0xabcdef01 && engine.adapter.low==0x12345678 && engine.physical==2 && engine.engine==3);
    assert(!ParseEngine(L"luid_0x100000000_0x1_phys_0_eng_0_engtype_3D",engine));
    assert(!ParseEngine(L"luid_0x1_0x1_phys_-1_eng_0_engtype_3D",engine));
    assert(!ParseEngine(L"luid_0x1_0x1_phys_0_eng_0_engtype_",engine));

    uint64_t memory = 123;
    const std::wstring d = L"luid_0x00000000_0x00000001_phys_0";
    const std::wstring i = L"luid_0x00000000_0x00000002_phys_0";
    assert(DedicatedMemory({{d, 24.0 * 1024 * 1024 * 1024}, {i, 385.0 * 1024 * 1024}}, {0,1}, memory));
    assert(memory == 24ull * 1024 * 1024 * 1024);
    assert(DedicatedMemory({{d,0}}, {0,1}, memory) && memory == 0);
    assert(!DedicatedMemory({{i,100}}, {0,1}, memory));
    assert(!DedicatedMemory({}, {0,1}, memory));
    assert(!DedicatedMemory({{L"pid_4_" + d,100}, {L"_Total",100}, {d+L"_bad",100}}, {0,1}, memory));
    assert(!DedicatedMemory({{d,-1}}, {0,1}, memory));
    assert(!DedicatedMemory({{d,std::numeric_limits<double>::infinity()}}, {0,1}, memory));
    assert(!DedicatedMemory({{d,std::numeric_limits<double>::quiet_NaN()}}, {0,1}, memory));
    assert(!DedicatedMemory({{d,9223372036854775808.0}}, {0,1}, memory));
    assert(DedicatedMemory({{d,100}, {d,100}}, {0,1}, memory) && memory == 100);
    assert(!DedicatedMemory({{d,100}, {d,200}}, {0,1}, memory));
    assert(DedicatedMemory({{d,100}, {L"luid_0x0_0x1_phys_1",200}}, {0,1}, memory) && memory == 300);
    assert(!DedicatedMemory({{L"luid_0x100000000_0x1_phys_0",100}}, {0,1}, memory));
    assert(!DedicatedMemory({{L"luid_0x0_0x1_phys_-1",100}}, {0,1}, memory));

    const std::wstring id = L"PCI\\VEN_1234&DEV_ABCD\\4&AAAA&0&0000";
    assert(NormalizeDeviceId(id) == NormalizeDeviceId(L"\\\\?\\pci#ven_1234&dev_abcd#4&aaaa&0&0000#{1234-5678}"));
    std::vector<GpuAdapter> adapters = {
        {L"PCI\\VEN_1234&DEV_2222\\4&BBBB&0&0000",L"Integrated GPU",{0,2},512},
        {id,L"Discrete GPU",{0,1},24000},
        {L"PCI\\VEN_1234&DEV_ABCD\\4&CCCC&0&0000",L"Discrete GPU",{0,3},24000},
    };
    assert(SelectGpuAdapter(adapters,L"")->device_id == id);
    assert(SelectGpuAdapter(adapters,id)->luid.low == 1);
    assert(SelectGpuAdapter(adapters,adapters[2].device_id)->luid.low == 3);
    assert(!SelectGpuAdapter(adapters,L"PCI\\MISSING"));
    std::reverse(adapters.begin(),adapters.end());
    adapters[1].luid={0,9}; // A reboot changed its LUID and enumeration index.
    assert(SelectGpuAdapter(adapters,id)->luid.low == 9);
    assert(!SelectGpuAdapter({},L""));
    std::cout << "GPU selection, physical-engine aggregation and dedicated memory regression tests passed.\n";
}
