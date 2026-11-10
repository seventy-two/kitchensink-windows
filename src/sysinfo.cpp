#include "sysinfo.h"

#include <windows.h>
#include <dxgi1_4.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <string>

#include "local.h"
#include "net.h"

namespace ks {

namespace {

// readCounter reads every value of a wildcard performance counter.
std::vector<std::pair<std::string, double>> readCounter(PDH_HCOUNTER counter) {
    std::vector<std::pair<std::string, double>> out;
    DWORD size = 0, count = 0;
    if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &size, &count, nullptr) !=
        PDH_MORE_DATA)
        return out;
    std::vector<unsigned char> buf(size);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data());
    if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &size, &count, items) !=
        ERROR_SUCCESS)
        return out;
    for (DWORD i = 0; i < count; i++) {
        if (items[i].FmtValue.CStatus != PDH_CSTATUS_VALID_DATA && items[i].FmtValue.CStatus != PDH_CSTATUS_NEW_DATA)
            continue;
        out.emplace_back(Narrow(items[i].szName), items[i].FmtValue.doubleValue);
    }
    return out;
}

// luidName is how the GPU counters name an adapter.
std::string luidName(LUID luid) {
    char buf[64];
    snprintf(buf, sizeof(buf), "luid_0x%08lX_0x%08lX", luid.HighPart, luid.LowPart);
    return buf;
}

// Gpu is the PC's main graphics card: the one with the most memory.
struct Gpu {
    std::string name, luid;
    uint64_t memory = 0;
};

Gpu findGpu() {
    Gpu best;
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return best;
    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++) {
        DXGI_ADAPTER_DESC1 d;
        if (SUCCEEDED(adapter->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
            d.DedicatedVideoMemory > best.memory) {
            best = {Narrow(d.Description), luidName(d.AdapterLuid), d.DedicatedVideoMemory};
        }
        adapter->Release();
    }
    factory->Release();
    return best;
}

// gpuTemperature reads the first NVIDIA GPU's temperature from the
// driver's NVML, loaded at run time; -1 without one. (NVML's fan speed
// isn't used: on the RTX 5070 Ti it flips between 0 and 100% while the
// fans hold 50%.)
float gpuTemperature() {
    static HMODULE nvml = LoadLibraryExW(L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!nvml) return -1;
    using InitFn = int (*)();
    using HandleFn = int (*)(unsigned, void**);
    using TempFn = int (*)(void*, int, unsigned*);
    static auto init = reinterpret_cast<InitFn>(GetProcAddress(nvml, "nvmlInit_v2"));
    static auto handle = reinterpret_cast<HandleFn>(GetProcAddress(nvml, "nvmlDeviceGetHandleByIndex_v2"));
    static auto temp = reinterpret_cast<TempFn>(GetProcAddress(nvml, "nvmlDeviceGetTemperature"));
    if (!init || !handle || !temp) return -1;
    static bool ok = init() == 0;
    static void* dev = nullptr;
    if (!ok || (!dev && handle(0, &dev) != 0)) return -1;
    unsigned c = 0;
    if (temp(dev, 0 /* NVML_TEMPERATURE_GPU */, &c) != 0) return -1;
    return float(c);
}

// Afterburner is what MSI Afterburner's monitoring shares while it runs:
// Windows has no CPU temperature of its own on this board (no thermal
// zones), and Afterburner's fan figure is the true one.
struct Afterburner {
    float cpuTemp = -1, gpuTemp = -1, gpuFan = -1;
};

// readAfterburner reads its "MAHMSharedMemory" (RivaTuner's shared memory
// format, v2: a header then fixed-size entries, each a source's name,
// units, value and IDs).
Afterburner readAfterburner() {
    Afterburner out;
    HANDLE map = OpenFileMappingW(FILE_MAP_READ, FALSE, L"MAHMSharedMemory");
    if (!map) return out;
    const auto* base = static_cast<const unsigned char*>(MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0));
    if (!base) {
        CloseHandle(map);
        return out;
    }
    MEMORY_BASIC_INFORMATION info{};
    VirtualQuery(base, &info, sizeof(info));
    auto u32 = [&](size_t at) {
        uint32_t v;
        std::memcpy(&v, base + at, 4);
        return v;
    };
    constexpr size_t kName = 260, kData = 5 * kName, kGpu = kData + 16, kSrc = kGpu + 4;
    // 'MAHM'; 0xDEAD once Afterburner has closed.
    if (info.RegionSize >= 32 && u32(0) == 0x4D41484D && u32(4) >= 0x20000) {
        size_t header = u32(8), count = u32(12), size = u32(16);
        bool cpuWhole = false;
        for (size_t i = 0; size >= kSrc + 4 && i < count && header + (i + 1) * size <= info.RegionSize; i++) {
            const unsigned char* e = base + header + i * size;
            float v;
            uint32_t gpu, src;
            std::memcpy(&v, e + kData, 4);
            std::memcpy(&gpu, e + kGpu, 4);
            std::memcpy(&src, e + kSrc, 4);
            if (!(v >= 0 && v < 1000)) continue;  // FLT_MAX: no reading
            if (src == 0x80) {
                // The CPU's own (gpu = ~0) beats per-core readings.
                if (gpu == 0xFFFFFFFF) out.cpuTemp = v, cpuWhole = true;
                else if (!cpuWhole) out.cpuTemp = std::max(out.cpuTemp, v);
            } else if (gpu == 0 && src == 0x00) {
                out.gpuTemp = v;
            } else if (gpu == 0 && (src == 0x10 || src == 0x12 || src == 0x14) && v <= 100) {
                // Fan speed, fan 2, fan 3: the fastest.
                out.gpuFan = std::max(out.gpuFan, v);
            }
        }
    }
    UnmapViewOfFile(base);
    CloseHandle(map);
    return out;
}

std::vector<DriveInfo> readDrives() {
    std::vector<DriveInfo> out;
    wchar_t roots[512];
    DWORD n = GetLogicalDriveStringsW(ARRAYSIZE(roots), roots);
    if (n == 0 || n > ARRAYSIZE(roots)) return out;
    for (const wchar_t* r = roots; *r; r += wcslen(r) + 1) {
        // Fixed drives only: network drives can stall, and removable ones
        // come and go.
        if (GetDriveTypeW(r) != DRIVE_FIXED) continue;
        ULARGE_INTEGER avail, total, free;
        if (!GetDiskFreeSpaceExW(r, &avail, &total, &free)) continue;
        wchar_t label[MAX_PATH + 1] = {};
        GetVolumeInformationW(r, label, ARRAYSIZE(label), nullptr, nullptr, nullptr, nullptr, 0);
        out.push_back({Narrow(r), Narrow(label), total.QuadPart, free.QuadPart});
    }
    return out;
}

}  // namespace

LocalStats::LocalStats(std::function<void()> wake) : wake_(std::move(wake)) {
    thread_ = std::thread([this] { run(); });
}

LocalStats::~LocalStats() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        stop_ = true;
    }
    cv_.notify_all();
    thread_.join();
}

void LocalStats::Want() {
    unsigned long long before = wanted_.exchange(GetTickCount64());
    // Wake the thread if it had gone idle. Taking the lock first means it
    // can't miss this between checking and waiting.
    if (GetTickCount64() - before > 3000) {
        { std::lock_guard<std::mutex> lock(mu_); }
        cv_.notify_all();
    }
}

bool LocalStats::Take(std::vector<Snapshot>& out) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!fresh_) return false;
    out = latest_;
    fresh_ = false;
    return true;
}

void LocalStats::run() {
    // Counters are added by their English names so they work whatever the
    // Windows display language. A missing one (e.g. no GPU counters on an
    // old driver) just reads as unknown.
    PDH_HQUERY query = nullptr;
    PDH_HCOUNTER cpu = nullptr, gpu = nullptr, vram = nullptr;
    if (PdhOpenQueryW(nullptr, 0, &query) == ERROR_SUCCESS) {
        // Processor Utility is what Task Manager shows; it can pass 100%
        // when the CPU boosts, so it's capped.
        if (PdhAddEnglishCounterW(query, L"\\Processor Information(_Total)\\% Processor Utility", 0, &cpu) !=
            ERROR_SUCCESS)
            PdhAddEnglishCounterW(query, L"\\Processor(_Total)\\% Processor Time", 0, &cpu);
        PdhAddEnglishCounterW(query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &gpu);
        PdhAddEnglishCounterW(query, L"\\GPU Adapter Memory(*)\\Dedicated Usage", 0, &vram);
        PdhCollectQueryData(query);
    }
    Gpu card = findGpu();
    PcHistory history;
    std::vector<DriveInfo> drives;
    unsigned long long drivesAt = 0;

    std::unique_lock<std::mutex> lock(mu_);
    while (!stop_) {
        bool wanted = GetTickCount64() - wanted_.load() < 3000;
        if (!wanted) {
            // Idle until Want wakes us; the gap would spoil the sparklines.
            history = PcHistory();
            cv_.wait(lock, [&] { return stop_ || GetTickCount64() - wanted_.load() < 3000; });
            if (query) PdhCollectQueryData(query);  // a fresh baseline for rates
            cv_.wait_for(lock, std::chrono::seconds(1), [&] { return stop_; });
            continue;
        }
        lock.unlock();

        PcSample s;
        s.gpuName = card.name;
        s.vramTotal = card.memory;
        if (query && PdhCollectQueryData(query) == ERROR_SUCCESS) {
            PDH_FMT_COUNTERVALUE v;
            if (cpu && PdhGetFormattedCounterValue(cpu, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, nullptr, &v) ==
                           ERROR_SUCCESS)
                s.cpu = float(std::min(v.doubleValue, 100.0));
            if (gpu) s.gpu = GpuUtilisation(readCounter(gpu));
            if (vram) {
                // The card's own memory; the busiest adapter if it isn't
                // found by name.
                double used = 0, any = 0;
                for (const auto& [name, value] : readCounter(vram)) {
                    if (!card.luid.empty() && name.rfind(card.luid, 0) == 0) used += value;
                    any = std::max(any, value);
                }
                s.vramUsed = uint64_t(used > 0 ? used : any);
            }
        }
        Afterburner ab = readAfterburner();
        s.cpuTemp = ab.cpuTemp;
        s.gpuTemp = gpuTemperature();
        if (s.gpuTemp < 0) s.gpuTemp = ab.gpuTemp;
        s.gpuFan = ab.gpuFan;
        MEMORYSTATUSEX mem{sizeof(mem)};
        if (GlobalMemoryStatusEx(&mem)) {
            s.ramTotal = mem.ullTotalPhys;
            s.ramUsed = mem.ullTotalPhys - mem.ullAvailPhys;
        }
        history.Add(std::move(s));
        // Drives change slowly.
        if (GetTickCount64() - drivesAt > 30000) {
            drives = readDrives();
            drivesAt = GetTickCount64();
        }
        int64_t now = int64_t(std::time(nullptr));
        std::vector<Snapshot> panels = {WithDrives(history.Build(now), DrivesSnapshot(drives, now))};

        lock.lock();
        latest_ = std::move(panels);
        fresh_ = true;
        lock.unlock();
        wake_();
        lock.lock();
        cv_.wait_for(lock, std::chrono::seconds(1), [&] { return stop_; });
    }
    lock.unlock();
    if (query) PdhCloseQuery(query);
}

}  // namespace ks
