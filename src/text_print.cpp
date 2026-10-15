#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
constexpr int kWidth = 4960;
constexpr int kHeight = 7014;
constexpr int kStride = ((kWidth + 31) / 32) * 4;

std::wstring ModuleSibling(const wchar_t* name) {
    std::wstring path(32768, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!n || n >= path.size()) return {};
    path.resize(n);
    const auto slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};
    path.resize(slash + 1);
    path += name;
    return path;
}

bool RenderTextToPbm(const std::wstring& inputPath, const std::wstring& outputPath) {
    std::ifstream in(inputPath, std::ios::binary);
    if (!in) { std::wcerr << L"Cannot open spool text file.\n"; return false; }
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.empty()) { std::wcerr << L"Print job is empty.\n"; return false; }

    // Generic / Text Only emits text bytes. Interpret them using the active Windows ANSI code page.
    int chars = MultiByteToWideChar(CP_ACP, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (chars <= 0) { std::wcerr << L"Cannot decode text using the Windows ANSI code page.\n"; return false; }
    std::wstring text(chars, L'\0');
    MultiByteToWideChar(CP_ACP, 0, bytes.data(), static_cast<int>(bytes.size()), text.data(), chars);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = kWidth;
    bmi.bmiHeader.biHeight = -kHeight; // top-down, so rows map directly to PBM
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 1;
    bmi.bmiHeader.biCompression = BI_RGB;
    bmi.bmiColors[0] = RGBQUAD{255,255,255,0}; // palette index 0 = white
    bmi.bmiColors[1] = RGBQUAD{0,0,0,0};       // palette index 1 = black

    HDC screen = GetDC(nullptr);
    if (!screen) return false;
    HDC dc = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    if (!dc) return false;
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!bitmap || !pixels) { if (bitmap) DeleteObject(bitmap); DeleteDC(dc); return false; }
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    PatBlt(dc, 0, 0, kWidth, kHeight, WHITENESS);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0,0,0));
    SetBkColor(dc, RGB(255,255,255));
    HFONT font = CreateFontW(-92, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        FIXED_PITCH | FF_MODERN, L"Consolas");
    if (!font) font = (HFONT)GetStockObject(SYSTEM_FIXED_FONT);
    HGDIOBJ oldFont = SelectObject(dc, font);

    const int left = 300, top = 280, lineHeight = 120, charsPerLine = 72;
    int x = left, y = top;
    std::wstring line;
    auto drawLine = [&]() {
        if (y + lineHeight >= kHeight - 250) { y = top; x += 1500; }
        if (x + 1300 >= kWidth - 200) { x = left; y = kHeight; }
        if (y < kHeight - 250) TextOutW(dc, x, y, line.c_str(), static_cast<int>(line.size()));
        y += lineHeight;
        line.clear();
    };
    for (wchar_t ch : text) {
        if (ch == L'\r') continue;
        if (ch == L'\f') { drawLine(); y = kHeight; continue; }
        if (ch == L'\n') { drawLine(); continue; }
        if (ch == L'\t') {
            const size_t spaces = 4 - (line.size() % 4);
            line.append(spaces, L' ');
        } else if (ch >= L' ') {
            line.push_back(ch);
        }
        if (line.size() >= charsPerLine) drawLine();
    }
    if (!line.empty()) drawLine();

    SelectObject(dc, oldFont);
    if (font && font != GetStockObject(SYSTEM_FIXED_FONT)) DeleteObject(font);
    SelectObject(dc, oldBitmap);

    std::ofstream out(outputPath, std::ios::binary);
    if (!out) { DeleteObject(bitmap); DeleteDC(dc); return false; }
    out << "P4\n" << kWidth << " " << kHeight << "\n";
    const auto* data = static_cast<const unsigned char*>(pixels);
    for (int row = 0; row < kHeight; ++row) {
        out.write(reinterpret_cast<const char*>(data + static_cast<size_t>(row) * kStride), kWidth / 8);
    }
    out.close();
    DeleteObject(bitmap);
    DeleteDC(dc);
    return static_cast<bool>(out);
}

bool RunDirectPrinter(const std::wstring& pbmPath) {
    const std::wstring exe = ModuleSibling(L"lbp1120-capt-print.exe");
    if (exe.empty()) return false;
    std::wstring cmd = L"\"" + exe + L"\" \"" + pbmPath + L"\"";
    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        std::wcerr << L"Cannot start CAPT transport, Win32 error " << GetLastError() << L"\n";
        return false;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return code == 0;
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) { std::wcerr << L"Usage: lbp1120-text-print.exe spool-file\n"; return 2; }
    wchar_t tempDir[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, tempDir)) return 2;
    wchar_t tempFile[MAX_PATH]{};
    if (!GetTempFileNameW(tempDir, L"LBP", 0, tempFile)) return 2;
    std::wstring pbm(tempFile);
    pbm += L".pbm";
    DeleteFileW(tempFile);
    const bool rendered = RenderTextToPbm(argv[1], pbm);
    const bool printed = rendered && RunDirectPrinter(pbm);
    DeleteFileW(pbm.c_str());
    return printed ? 0 : 1;
}
