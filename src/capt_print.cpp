#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <devpkey.h>

#include "libcapt/BasicCaptPrinter.hpp"
#include "libcapt/Compression/ScoaStreambuf.hpp"
#include "libcapt/Protocol/Enums.hpp"
#include "libcapt/Protocol/ExtendedStatus.hpp"
#include "libcapt/Protocol/PageParams.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <streambuf>
#include <thread>
#include <vector>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")

namespace {
constexpr wchar_t kTargetVidPid[] = L"VID_04A9&PID_262B";
constexpr wchar_t kBuildId[] = L"1";

const GUID kUsbPrintInterfaceGuid =
    {0x28d78fad, 0x5a12, 0x11d1, {0xae, 0x5b, 0x00, 0x00, 0xf8, 0x03, 0xa8, 0xc2}};

std::wstring DeviceInstanceId(DEVINST node) {
    std::vector<wchar_t> buffer(512);
    for (;;) {
        CONFIGRET cr = CM_Get_Device_IDW(node, buffer.data(),
            static_cast<ULONG>(buffer.size()), 0);
        if (cr == CR_SUCCESS) return buffer.data();
        if (cr != CR_BUFFER_SMALL || buffer.size() >= 32768) return L"";
        buffer.resize(buffer.size() * 2);
    }
}

bool IsTargetOrAncestor(DEVINST node) {
    for (unsigned depth = 0; depth < 12; ++depth) {
        const std::wstring id = DeviceInstanceId(node);
        std::wstring upper = id;
        std::transform(upper.begin(), upper.end(), upper.begin(),
            [](wchar_t c) { return static_cast<wchar_t>(towupper(c)); });
        if (upper.find(kTargetVidPid) != std::wstring::npos) return true;

        DEVINST parent = 0;
        if (CM_Get_Parent(&parent, node, 0) != CR_SUCCESS) break;
        node = parent;
    }
    return false;
}

std::wstring FindPrinterInterfacePath() {
    HDEVINFO set = SetupDiGetClassDevsW(&kUsbPrintInterfaceGuid, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("SetupDiGetClassDevs failed: " +
            std::to_string(GetLastError()));
    }

    std::wstring result;
    for (DWORD index = 0;; ++index) {
        SP_DEVICE_INTERFACE_DATA iface{};
        iface.cbSize = sizeof(iface);
        if (!SetupDiEnumDeviceInterfaces(set, nullptr, &kUsbPrintInterfaceGuid,
                index, &iface)) {
            const DWORD err = GetLastError();
            if (err == ERROR_NO_MORE_ITEMS) break;
            continue;
        }

        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &iface, nullptr, 0, &needed, nullptr);
        if (!needed) continue;

        std::vector<BYTE> storage(needed, 0);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        SP_DEVINFO_DATA devInfo{};
        devInfo.cbSize = sizeof(devInfo);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &iface, detail, needed, nullptr, &devInfo)) {
            continue;
        }

        if (IsTargetOrAncestor(devInfo.DevInst)) {
            result = detail->DevicePath;
            break;
        }
    }
    SetupDiDestroyDeviceInfoList(set);

    if (result.empty()) {
        throw std::runtime_error(
            "Could not find a USBPRINT interface whose parent is " +
            std::string("VID_04A9&PID_262B. Check that the printer is connected."));
    }
    return result;
}

class UsbPrintStreambuf final : public std::streambuf {
public:
    explicit UsbPrintStreambuf(HANDLE handle) : handle_(handle) {
        setg(input_.data(), input_.data(), input_.data());
    }

protected:
    int_type underflow() override {
        if (gptr() < egptr()) return traits_type::to_int_type(*gptr());
        DWORD count = 0;
        if (!ReadFile(handle_, input_.data(), static_cast<DWORD>(input_.size()),
                &count, nullptr)) {
            throw std::runtime_error("ReadFile from USB printer failed: " +
                std::to_string(GetLastError()));
        }
        if (count == 0) return traits_type::eof();
        setg(input_.data(), input_.data(), input_.data() + count);
        return traits_type::to_int_type(*gptr());
    }

    std::streamsize xsputn(const char* data, std::streamsize count) override {
        std::streamsize writtenTotal = 0;
        while (writtenTotal < count) {
            DWORD written = 0;
            const DWORD requested = static_cast<DWORD>(
                (std::min)(count - writtenTotal,
                    static_cast<std::streamsize>(0x7ffff000)));
            if (!WriteFile(handle_, data + writtenTotal, requested, &written, nullptr)) {
                throw std::runtime_error("WriteFile to USB printer failed: " +
                    std::to_string(GetLastError()));
            }
            if (written == 0) break;
            writtenTotal += written;
        }
        return writtenTotal;
    }

    int_type overflow(int_type ch) override {
        if (traits_type::eq_int_type(ch, traits_type::eof())) return traits_type::not_eof(ch);
        const char value = traits_type::to_char_type(ch);
        return xsputn(&value, 1) == 1 ? ch : traits_type::eof();
    }

    int sync() override { return 0; }

private:
    HANDLE handle_;
    std::array<char, 8192> input_{};
};

struct NeverStop {
    bool stop_requested() const noexcept { return false; }
};

std::string ReadPbmToken(std::istream& input) {
    for (;;) {
        int c = input.peek();
        while (c != EOF && std::isspace(static_cast<unsigned char>(c))) {
            input.get();
            c = input.peek();
        }
        if (c == '#') {
            std::string ignored;
            std::getline(input, ignored);
            continue;
        }
        break;
    }

    std::string token;
    for (;;) {
        const int c = input.peek();
        if (c == EOF || std::isspace(static_cast<unsigned char>(c)) || c == '#') break;
        token.push_back(static_cast<char>(input.get()));
    }
    if (token.empty()) throw std::runtime_error("PBM header ended unexpectedly");

    // Consume exactly the header delimiter. P4 data starts immediately afterwards.
    const int delimiter = input.get();
    if (delimiter == EOF || !std::isspace(static_cast<unsigned char>(delimiter))) {
        throw std::runtime_error("PBM header is missing a whitespace delimiter");
    }
    if (delimiter == '\r' && input.peek() == '\n') input.get();
    return token;
}

void PrintPage(const std::wstring& pbmPath) {
    std::ifstream pbm(pbmPath, std::ios::binary);
    if (!pbm) throw std::runtime_error("Cannot open PBM file.");

    if (ReadPbmToken(pbm) != "P4") {
        throw std::runtime_error("Input must be a binary PBM (P4) file.");
    }
    const unsigned long width = std::stoul(ReadPbmToken(pbm));
    const unsigned long height = std::stoul(ReadPbmToken(pbm));
    if (width == 0 || height == 0 || width % 8 != 0 ||
            width / 8 > 65535 || height > 65535) {
        throw std::runtime_error("PBM dimensions are invalid for CAPT (width must be divisible by 8).");
    }

    const std::wstring devicePath = FindPrinterInterfacePath();
    HANDLE handle = CreateFileW(devicePath.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Cannot open USBPRINT device (Win32 error " +
            std::to_string(GetLastError()) +
            "). Close printer status utilities and retry; do not change the USB driver.");
    }

    try {
        UsbPrintStreambuf transport(handle);
        std::iostream printerStream(&transport);
        Capt::BasicCaptPrinter<NeverStop> printer(printerStream);

        std::cout << "Reserving printer unit...\n";
        printer.ReserveUnit();
        Capt::ExtendedStatus status = printer.GetStatus();
        if (!status.Ready()) {
            throw std::runtime_error(
                "Printer is not ready. Check paper, cover, toner, and printer power.");
        }
        if (!status.Online() || status.Start != 0) {
            if (!printer.GoOnline(0)) {
                throw std::runtime_error("Printer refused the online command.");
            }
        }

        Capt::PageParams params{
            .PaperSize = 0x09,
            .TonerDensity = 0x3f,
            .Mode = 0,
            .Resolution = Capt::ResolutionIdx::RES_600,
            .SmoothEnable = true,
            .TonerSaving = false,
            .MarginLeft = 1,
            .MarginTop = 1,
            .ImageLineSize = static_cast<std::uint16_t>(width / 8),
            .ImageLines = static_cast<std::uint16_t>(height),
            .PaperWidth = 4960,
            .PaperHeight = 7014,
        };

        Capt::Compression::ScoaStreambuf compressed(*pbm.rdbuf(),
            static_cast<unsigned>(width / 8), static_cast<unsigned>(height));
        std::cout << "Sending " << width << "x" << height
                  << " pixels using CAPT v1/SCoA (BUILD #" << std::string("1") << ")...\n";
        if (!printer.WriteVideoData(params, compressed)) {
            throw std::runtime_error("Printer rejected the video data; no successful page transfer was confirmed.");
        }

        std::cout << "Page data sent. Waiting for printer status...\n";
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
        while (std::chrono::steady_clock::now() < deadline) {
            status = printer.GetStatus();
            if (status.VideoDataError()) {
                throw std::runtime_error("Printer reported a video-data error.");
            }
            if (!status.IsPrinting()) break;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        printer.GoOffline();
        printer.ReleaseUnit();
        std::cout << "CAPT job finished. Inspect the printer output.\n";
    } catch (...) {
        CloseHandle(handle);
        throw;
    }
    CloseHandle(handle);
}
}

int wmain(int argc, wchar_t** argv) {
    std::cout << "Canon LBP-1120 CAPT x64 direct-print prototype\n";
    std::cout << "BUILD #1\n";
    std::cout << "Target USB ID: VID_04A9&PID_262B\n";
    if (argc != 2) {
        std::wcerr << L"Usage: lbp1120-capt-print.exe <file.pbm>\n"
                   << L"Input must be binary PBM (P4), 600-dpi raster.\n";
        return 2;
    }

    try {
        PrintPage(argv[1]);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        std::cerr << "This is an experimental user-mode transport, not yet an installed Windows printer driver.\n";
        return 1;
    }
}
