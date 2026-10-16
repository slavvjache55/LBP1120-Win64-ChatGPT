#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winspool.h>
#include <winsplp.h>
#include <cstdio>
#include <cstring>

// Diagnostic-only print processor. It advertises RAW and deliberately does
// not print. Its purpose is to isolate spooler ABI/loading from CAPT logic.
namespace {
HMODULE g_module = nullptr;

void Log(const wchar_t* event, DWORD level, DWORD cbBuf, DWORD needed,
         DWORD returned, DWORD error) {
    HANDLE file = CreateFileW(L"C:\\Windows\\Temp\\CAPTTEST-enum.log",
        FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    wchar_t line[256]{};
    int chars = swprintf_s(line,
        L"pid=%lu event=%ls level=%lu cbBuf=%lu needed=%lu returned=%lu error=%lu\r\n",
        GetCurrentProcessId(), event, level, cbBuf, needed, returned, error);
    if (chars > 0) {
        DWORD written = 0;
        WriteFile(file, line, static_cast<DWORD>(chars) * sizeof(wchar_t), &written, nullptr);
    }
    CloseHandle(file);
}
}

extern "C" BOOL WINAPI CAPTTEST_EnumPrintProcessorDatatypes(
    LPWSTR, LPWSTR, DWORD level, LPBYTE buffer, DWORD cbBuf,
    LPDWORD needed, LPDWORD returned) {
    if (!needed || !returned || level != 1) {
        if (needed) *needed = 0;
        if (returned) *returned = 0;
        SetLastError(ERROR_INVALID_PARAMETER);
        Log(L"invalid", level, cbBuf, 0, 0, ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    constexpr wchar_t name[] = L"RAW";
    constexpr DWORD required = sizeof(DATATYPES_INFO_1W) + sizeof(name);
    *needed = required;
    *returned = 0;
    if (!buffer || cbBuf < required) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        Log(L"small-buffer", level, cbBuf, *needed, 0, ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }

    auto* info = reinterpret_cast<DATATYPES_INFO_1W*>(buffer);
    auto* text = reinterpret_cast<LPWSTR>(buffer + sizeof(DATATYPES_INFO_1W));
    memcpy(text, name, sizeof(name));
    info->pName = text;
    *returned = 1;
    SetLastError(ERROR_SUCCESS);
    Log(L"success", level, cbBuf, *needed, *returned, ERROR_SUCCESS);
    return TRUE;
}

extern "C" HANDLE WINAPI CAPTTEST_OpenPrintProcessor(
    LPWSTR, PPRINTPROCESSOROPENDATA) {
    SetLastError(ERROR_NOT_SUPPORTED);
    return nullptr;
}

extern "C" BOOL WINAPI CAPTTEST_PrintDocumentOnPrintProcessor(HANDLE, LPWSTR) {
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

extern "C" BOOL WINAPI CAPTTEST_ClosePrintProcessor(HANDLE) {
    return TRUE;
}

extern "C" BOOL WINAPI CAPTTEST_ControlPrintProcessor(HANDLE, DWORD) {
    return TRUE;
}

extern "C" DWORD WINAPI CAPTTEST_GetPrintProcessorCapabilities(
    LPWSTR datatype, DWORD level, LPBYTE, DWORD, LPDWORD needed) {
    if (!needed || !datatype || level != 1 || lstrcmpiW(datatype, L"RAW") != 0) {
        if (needed) *needed = 0;
        return ERROR_INVALID_DATATYPE;
    }
    *needed = 0;
    return ERROR_SUCCESS;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
