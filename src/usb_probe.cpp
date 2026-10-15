#include <windows.h>
#include <setupapi.h>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cwctype>
#include <cwchar>

static constexpr const wchar_t* BUILD_ID = L"4";

static std::wstring ReadStringProperty(HDEVINFO set, SP_DEVINFO_DATA& data, DWORD property) {
    DWORD type = 0, required = 0;
    SetupDiGetDeviceRegistryPropertyW(set, &data, property, &type, nullptr, 0, &required);
    if (!required) return L"";
    std::vector<BYTE> buffer(required + sizeof(wchar_t), 0);
    if (!SetupDiGetDeviceRegistryPropertyW(set, &data, property, &type,
            buffer.data(), static_cast<DWORD>(buffer.size()), &required)) return L"";
    return std::wstring(reinterpret_cast<const wchar_t*>(buffer.data()));
}

static std::wstring ReadHardwareIds(HDEVINFO set, SP_DEVINFO_DATA& data) {
    DWORD type = 0, required = 0;
    SetupDiGetDeviceRegistryPropertyW(set, &data, SPDRP_HARDWAREID, &type, nullptr, 0, &required);
    if (!required) return L"";
    std::vector<wchar_t> buffer(required / sizeof(wchar_t) + 2, L'\\0');
    if (!SetupDiGetDeviceRegistryPropertyW(set, &data, SPDRP_HARDWAREID, &type,
            reinterpret_cast<PBYTE>(buffer.data()), static_cast<DWORD>(buffer.size() * sizeof(wchar_t)), &required)) return L"";
    std::wstring joined;
    for (const wchar_t* p = buffer.data(); *p; p += wcslen(p) + 1) {
        if (!joined.empty()) joined += L" | ";
        joined += p;
    }
    return joined;
}

int wmain() {
    constexpr const wchar_t* target = L"VID_04A9&PID_262B";
    std::wcout << L"Canon LBP-1120 USB/PnP diagnostic (read-only)\\n";
    std::wcout << L"BUILD #" << BUILD_ID << L"\\n";
    std::wcout << L"Looking for " << target << L"\\n\\n" << std::flush;

    HDEVINFO set = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) {
        std::wcerr << L"SetupDiGetClassDevs failed: " << GetLastError() << L"\\n";
        return 2;
    }

    unsigned count = 0, exact = 0, canon = 0;
    for (DWORD i = 0;; ++i) {
        SP_DEVINFO_DATA data{};
        data.cbSize = sizeof(data);
        if (!SetupDiEnumDeviceInfo(set, i, &data)) {
            DWORD err = GetLastError();
            if (err == ERROR_NO_MORE_ITEMS) break;
            std::wcerr << L"SetupDiEnumDeviceInfo index " << i << L" failed: " << err << L"\\n";
            continue;
        }

        std::wstring ids = ReadHardwareIds(set, data);
        std::wstring upperIds = ids;
        std::transform(upperIds.begin(), upperIds.end(), upperIds.begin(),
            [](wchar_t c) { return static_cast<wchar_t>(towupper(static_cast<wint_t>(c))); });
        bool isTarget = upperIds.find(target) != std::wstring::npos;
        bool isCanon = upperIds.find(L"VID_04A9") != std::wstring::npos;
        if (!isTarget && !isCanon) continue;

        ++count;
        if (isTarget) ++exact;
        if (isCanon) ++canon;

        // Print and flush the build marker BEFORE reading optional name properties.
        std::wcout << L"[" << count << L"] " << (isTarget ? L"TARGET" : L"Canon")
                   << L" (BUILD #" << BUILD_ID << L")\\n" << std::flush;

        std::wstring desc = ReadStringProperty(set, data, SPDRP_DEVICEDESC);
        std::wstring friendly = ReadStringProperty(set, data, SPDRP_FRIENDLYNAME);
        std::wstring label = friendly.empty() ? desc : friendly;
        std::wcout << L"    Name: " << (label.empty() ? L"(device name unavailable)" : label) << L"\\n";
        std::wcout << L"    Hardware IDs: " << (ids.empty() ? L"(not exposed)" : ids) << L"\\n"
                   << std::flush;
    }
    SetupDiDestroyDeviceInfoList(set);
    std::wcout << L"\\nSummary: exact VID/PID matches=" << exact
               << L", Canon VID matches=" << canon << L"\\n";
    if (!exact) {
        std::wcout << L"No exact match found. Check Device Manager and USB cable/power.\\n";
        return 1;
    }
    std::wcout << L"Device is present in Windows PnP. This is not a print test.\\n";
    return 0;
}
