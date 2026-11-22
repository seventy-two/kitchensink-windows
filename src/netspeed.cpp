#include "netspeed.h"

#include <winsock2.h>
#include <ws2ipdef.h>
#include <windows.h>
#include <iphlpapi.h>

namespace ks {

namespace {

std::string narrow(const wchar_t* w) {
    if (!w || !*w) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

}  // namespace

void NetSpeed::Sample(double t) {
    if (t - sampled_ < 1) return;
    sampled_ = t;
    MIB_IF_TABLE2* table = nullptr;
    if (GetIfTable2(&table) != NO_ERROR || !table) {
        available_ = false;
        return;
    }
    // Only real adapters that are up: not loopback, tunnels (Tailscale)
    // or the filter layers Windows stacks on each adapter, which would
    // count the same bytes again.
    uint64_t in = 0, out = 0;
    std::string names;
    bool any = false;
    for (ULONG i = 0; i < table->NumEntries; i++) {
        const MIB_IF_ROW2& r = table->Table[i];
        if (!r.InterfaceAndOperStatusFlags.HardwareInterface || r.InterfaceAndOperStatusFlags.FilterInterface) continue;
        if (r.OperStatus != IfOperStatusUp || r.Type == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        in += r.InOctets;
        out += r.OutOctets;
        if (!names.empty()) names += ", ";
        names += narrow(r.Alias);
        any = true;
    }
    FreeMibTable(table);
    available_ = any;
    adapters_ = names;
    if (any) history_.Push(t, in, out);
}

}  // namespace ks
