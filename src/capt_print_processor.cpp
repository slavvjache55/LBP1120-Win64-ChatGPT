#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winspool.h>
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
    *returned = 0;
    *needed = sizeof(DATATYPES_INFO_1W);
    if (!buffer || cbBuf < *needed) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    auto* info = reinterpret_cast<DATATYPES_INFO_1W*>(buffer);
    info->pName = const_cast<LPWSTR>(L"CAPTPBM");
    *returned = 1;
    return TRUE;
}

struct CAPT_OPEN_DATA { LPWSTR pPrinterName; LPWSTR pDatatype; LPWSTR pParameters; PDEVMODE pDevMode; DWORD JobId; };

extern "C"
HANDLE WINAPI CAPTPBM_OpenPrintProcessor(LPWSTR printerName, CAPT_OPEN_DATA* openData) {
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
    ctx->printerName = printerName;
    ctx->datatype = openData->pDatatype;
    return ctx;
}

extern "C"
BOOL WINAPI CAPTPBM_PrintDocumentOnPrintProcessor(HANDLE handle, LPWSTR, DWORD, DWORD, LPBYTE) {
    auto* ctx = reinterpret_cast<Context*>(handle);
    if (!ctx) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    HANDLE printer = nullptr;
    if (!OpenPrinterW(ctx->printerName.data(), &printer, nullptr)) return FALSE;

    std::wstring tempPath(32768, L'\0');
    DWORD n = GetTempPathW(static_cast<DWORD>(tempPath.size()), tempPath.data());
    if (!n || n >= tempPath.size()) {
        ClosePrinter(printer);
        SetLastError(ERROR_PATH_NOT_FOUND);
        return FALSE;
    }
    tempPath.resize(n);
    wchar_t tempFile[MAX_PATH]{};
    if (!GetTempFileNameW(tempPath.c_str(), L"CAP", 0, tempFile)) {
        ClosePrinter(printer);
        return FALSE;
    }

    bool ok = false;
    {
        std::ofstream out(tempFile, std::ios::binary | std::ios::trunc);
        if (!out) {
            DeleteFileW(tempFile);
            ClosePrinter(printer);
            SetLastError(ERROR_ACCESS_DENIED);
            return FALSE;
        }
        BYTE chunk[64 * 1024];
        for (;;) {
            DWORD read = 0;
            if (!ReadPrinter(printer, chunk, sizeof(chunk), &read)) {
                out.close();
                DeleteFileW(tempFile);
                ClosePrinter(printer);
                return FALSE;
            }
            if (!read) break;
            out.write(reinterpret_cast<const char*>(chunk), read);
            if (!out) break;
        }
        out.flush();
        ok = static_cast<bool>(out);
    }
    ClosePrinter(printer);

    if (!ok) {
        DeleteFileW(tempFile);
        SetLastError(ERROR_WRITE_FAULT);
        return FALSE;
    }

    // Verify the payload is binary PBM P4 before invoking the CAPT transport.
    {
        std::ifstream in(tempFile, std::ios::binary);
        char magic[2]{};
        in.read(magic, 2);
        if (!in || magic[0] != 'P' || magic[1] != '4') {
            DeleteFileW(tempFile);
            SetLastError(ERROR_INVALID_DATA);
            return FALSE;
        }
    }

    std::wstring exePath = ModulePath();
    const auto slash = exePath.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        DeleteFileW(tempFile);
        SetLastError(ERROR_PATH_NOT_FOUND);
        return FALSE;
    }
    exePath.resize(slash + 1);
    exePath += L"lbp1120-capt-print.exe";

    std::wstring command = L"\"" + exePath + L"\" \"" + tempFile + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exePath.c_str(), command.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        DeleteFileW(tempFile);
        return FALSE;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    DeleteFileW(tempFile);
    if (exitCode != 0) {
        SetLastError(ERROR_PRINT_CANCELLED);
        return FALSE;
    }
    return TRUE;
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

