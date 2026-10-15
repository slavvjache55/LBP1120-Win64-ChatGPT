#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winspool.h>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#pragma comment(lib, "winspool.lib")

int wmain(int argc, wchar_t** argv) {
    std::wcout << L"Canon LBP-1120 CAPTPBM queue submitter BUILD #1\n";
    if (argc != 3) {
        std::wcerr << L"Usage: lbp1120-submit-pbm.exe \"Queue name\" file.pbm\n";
        return 2;
    }
    std::ifstream in(argv[2], std::ios::binary);
    if (!in) { std::wcerr << L"Cannot open PBM file.\n"; return 2; }
    char magic[2]{};
    in.read(magic, 2);
    if (!in || magic[0] != 'P' || magic[1] != '4') {
        std::wcerr << L"Input must be binary PBM P4.\n"; return 2;
    }
    in.clear();
    in.seekg(0);
    HANDLE printer = nullptr;
    if (!OpenPrinterW(argv[1], &printer, nullptr)) {
        std::wcerr << L"OpenPrinter failed: " << GetLastError() << L"\n"; return 1;
    }
    DOC_INFO_1W doc{};
    doc.pDocName = const_cast<LPWSTR>(L"Canon LBP-1120 PBM test");
    doc.pOutputFile = nullptr;
    doc.pDatatype = const_cast<LPWSTR>(L"CAPTPBM");
    DWORD job = StartDocPrinterW(printer, 1, reinterpret_cast<LPBYTE>(&doc));
    if (!job) {
        std::wcerr << L"StartDocPrinter failed: " << GetLastError() << L"\n";
        ClosePrinter(printer); return 1;
    }
    bool ok = StartPagePrinter(printer) != FALSE;
    std::vector<char> buf(64 * 1024);
    while (ok && in) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const auto count = static_cast<DWORD>(in.gcount());
        if (!count) break;
        DWORD written = 0;
        if (!WritePrinter(printer, buf.data(), count, &written) || written != count) {
            std::wcerr << L"WritePrinter failed: " << GetLastError() << L"\n";
            ok = false;
        }
    }
    if (!EndPagePrinter(printer)) ok = false;
    if (!EndDocPrinter(printer)) ok = false;
    ClosePrinter(printer);
    if (!ok) { std::wcerr << L"Queue submission failed.\n"; return 1; }
    std::wcout << L"PBM job submitted to queue. Check the print queue and printer output.\n";
    return 0;
}
