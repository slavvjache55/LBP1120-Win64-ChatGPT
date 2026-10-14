#include <windows.h>
#include <setupapi.h>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cwctype>

static std::wstring ReadProperty(HDEVINFO set, SP_DEVINFO_DATA& data, DWORD property) {
    DWORD type = 0, required = 0;
    SetupDiGetDeviceRegistryPropertyW(set, &data, property, &type, nullptr, 0, &required);
    if (!required) return L"";
    std::vector<BYTE> buffer(required + sizeof(wchar_t), 0);
    if (!SetupDiGetDeviceRegistryPropertyW(set, &data, property, &type,
            buffer.data(), static_cast<DWORD>(buffer.size()), &required)) return L"";
    return reinterpret_cast<const wchar_t*>(buffer.data());
}

int wmain() {
    constexpr const wchar_t* target = L"VID_04A9&PID_262B";
    std::wcout << L"Canon LBP-1120 USB/PnP diagnostic (read-only)\n";
    std::wcout << L"Looking for " << target << L"\n\n";

    HDEVINFO set = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) {
        std::wcerr << L"SetupDiGetClassDevs failed: " << GetLastError() << L"\n";
        return 2;
    }

    unsigned count = 0, exact = 0, canon = 0;
    for (DWORD i = 0;; ++i) {
        SP_DEVINFO_DATA data{};
        data.cbSize = sizeof(data);
        if (!SetupDiEnumDeviceInfo(set, i, &data)) {
            if (GetLastError() == ERROR_NO_MORE_ITEMS) break;
            continue;
        }
        std::wstring ids = ReadProperty(set, data, SPDRP_HARDWAREID);
        std::wstring desc = ReadProperty(set, data, SPDRP_DEVICEDESC);
        std::wstring friendly = ReadProperty(set, data, SPDRP_FRIENDLYNAME);
        std::wstring label = friendly.empty() ? desc : friendly;
        std::wstring upperIds = ids;
        std::transform(upperIds.begin(), upperIds.end(), upperIds.begin(), towupper);
        bool isTarget = upperIds.find(target) != std::wstring::npos;
        bool isCanon = upperIds.find(L"VID_04A9") != std::wstring::npos;
        if (isTarget || isCanon) {
            ++count;
            if (isTarget) ++exact;
            if (isCanon) ++canon;
            std::wcout << L"[" << count << L"] " << (isTarget ? L"TARGET " : L"Canon ")
                       << label << L"\n";
            std::wcout << L"    Hardware IDs: " << (ids.empty() ? L"(not exposed)" : ids) << L"\n";
        }
    }
    SetupDiDestroyDeviceInfoList(set);
    std::wcout << L"\nSummary: exact VID/PID matches=" << exact
               << L", Canon VID matches=" << canon << L"\n";
    if (!exact) {
        std::wcout << L"No exact match found. Check Device Manager and USB cable/power.\n";
        return 1;
    }
    std::wcout << L"Device is present in Windows PnP. This is not a print test.\n";
    return 0;
}
