#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winspool.h>
#include <winsplp.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <new>

#pragma comment(lib, "winspool.lib")

// Experimental x64 print processor. It accepts only RAW jobs containing a
// binary PBM (P4) page, then hands that page to the tested CAPT user-mode app.
// It is NOT a general EMF/XPS renderer and therefore cannot yet print arbitrary
// applications' documents.
namespace {
struct Context {
    std::wstring printerName;
    std::wstring datatype;
};
constexpr wchar_t kBuild[] = L"1";
HMODULE g_module = nullptr;
std::wstring ModulePath() {
    std::wstring path(32768, L'\0');
    DWORD n = GetModuleFileNameW(g_module, path.data(), static_cast<DWORD>(path.size()));
    if (!n || n >= path.size()) return L"";
    path.resize(n);
    return path;
}
}

extern "C"
BOOL WINAPI CAPTPBM_EnumPrintProcessorDatatypes(LPWSTR, LPWSTR, DWORD level,
    LPBYTE buffer, DWORD cbBuf, LPDWORD needed, LPDWORD returned) {
    if (!needed || !returned || level != 1) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    // The caller owns the output buffer. Keep both DATATYPES_INFO_1W and
    // its pName string inside that buffer, as required by the Win32 API.
    constexpr wchar_t datatype[] = L"CAPTPBM";
    constexpr DWORD required =
        static_cast<DWORD>(sizeof(DATATYPES_INFO_1W) + sizeof(datatype));
    *needed = required;
    *returned = 0;

    if (!buffer || cbBuf < required) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }

    auto* info = reinterpret_cast<DATATYPES_INFO_1W*>(buffer);
    auto* name = reinterpret_cast<LPWSTR>(buffer + sizeof(DATATYPES_INFO_1W));
    memcpy(name, datatype, sizeof(datatype));
    info->pName = name;
    *returned = 1;
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

extern "C"
DWORD WINAPI CAPTPBM_GetPrintProcessorCapabilities(LPWSTR datatype, DWORD, LPBYTE, DWORD cbBuf, LPDWORD needed) {
    if (!needed || !datatype || lstrcmpiW(datatype, L"CAPTPBM") != 0) {
        if (needed) *needed = 0;
        return ERROR_INVALID_DATATYPE;
    }
    // The prototype advertises no optional capabilities, but exports the
    // documented entry point so the spooler can resolve it by name.
    *needed = 0;
    (void)cbBuf;
    return ERROR_NOT_SUPPORTED;
}

extern "C"
HANDLE WINAPI CAPTPBM_OpenPrintProcessor(LPWSTR printerName, PPRINTPROCESSOROPENDATA openData) {
    if (!printerName || !openData || !openData->pDatatype ||
        lstrcmpiW(openData->pDatatype, L"CAPTPBM") != 0) {
        SetLastError(ERROR_INVALID_DATATYPE);
        return nullptr;
    }
    auto* ctx = new (std::nothrow) Context;
    if (!ctx) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    ctx->printerName = openData->pPrinterName ? openData->pPrinterName : printerName;
    ctx->datatype = openData->pDatatype;
    return ctx;
}

extern "C"
BOOL WINAPI CAPTPBM_ClosePrintProcessor(HANDLE handle) {
    delete reinterpret_cast<Context*>(handle);
    return TRUE;
}

extern "C"
BOOL WINAPI CAPTPBM_ControlPrintProcessor(HANDLE, DWORD) {
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { g_module = instance; DisableThreadLibraryCalls(instance); }
    return TRUE;
}extern "C"
BOOL WINAPI CAPTPBM_PrintDocumentOnPrintProcessor(HANDLE handle, LPWSTR documentName) {
    auto* ctx = reinterpret_cast<Context*>(handle);
    if (!ctx || !documentName) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    // The spooler passes the spool-file path here. Only our explicit CAPTPBM
    // datatype is accepted; no arbitrary EMF/XPS rendering is claimed.
    std::ifstream in(documentName, std::ios::binary);
    char magic[2]{};
    in.read(magic, 2);
    if (!in || magic[0] != 'P' || magic[1] != '4') {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }

    std::wstring exePath = ModulePath();
    const auto slash = exePath.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        SetLastError(ERROR_PATH_NOT_FOUND);
        return FALSE;
    }
    exePath.resize(slash + 1);
    exePath += L"lbp1120-capt-print.exe";

    std::wstring command = L"\"" + exePath + L"\" \"" + documentName + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exePath.c_str(), command.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return FALSE;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (exitCode != 0) {
        SetLastError(ERROR_PRINT_CANCELLED);
        return FALSE;
    }
    return TRUE;
}

