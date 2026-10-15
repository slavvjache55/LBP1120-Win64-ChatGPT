# Canon LBP-1120 Windows x64 CAPT prototype

This repository now builds two native Windows x64 executables:

- `lbp1120-usb-probe.exe`: read-only Plug-and-Play diagnostic.
- `lbp1120-capt-print.exe`: **experimental direct-print proof of concept**. It uses upstream `libcapt` (CAPT v1 protocol and SCoA compression) and attempts to communicate with the USBPRINT device interface for `VID_04A9&PID_262B`.

**Important:** this is not yet a registered Windows printer driver/queue. It is the next transport/protocol test toward one. It has not yet been verified on physical LBP-1120 hardware. Keep the existing Windows USB driver installed. The print prototype opens the existing Microsoft USBPRINT interface and does not install or replace device drivers.

## Build and download (no local Visual Studio required)

GitHub Actions builds the code on a hosted Windows x64 runner.

1. Open the [Actions page](https://github.com/slavvjache55/LBP1120-Win64-ChatGPT/actions).
2. Open the latest successful **Build Canon LBP-1120 x64 prototype** run.
3. Download the `LBP1120-Win64-prototype` artifact ZIP and extract it.
4. Connect and power on the LBP-1120. Close other printer utilities so the USB device is not busy.

The ZIP includes the two EXEs, this README, a generated full-page test pattern (`lbp1120-test.pbm`) and the PBM generator script.

## Experimental direct-print test

Open Command Prompt in the extracted folder and run:

```bat
lbp1120-capt-print.exe lbp1120-test.pbm
```

The first version tries to open the device through the standard Windows USB printer interface, then performs the CAPT v1 handshake and sends a 600-dpi PBM page using libcapt/SCoA. It reports errors rather than silently claiming a successful print. Do not run two copies simultaneously. If it reports access denied or cannot open the interface, do not switch the device to WinUSB or uninstall any driver; send the complete error output.

This uses a raw test application, not a Windows print queue. Printing from Notepad/Word still requires a Windows print pipeline (renderer/print processor or language monitor and installation package). The direct-print path must be confirmed on the actual printer first.

## Why libcapt?

[darkvision77/libcapt](https://github.com/darkvision77/libcapt) is an open-source C++ implementation of the Canon CAPT v1 protocol and SCoA raster compression, reverse-engineered from the original driver. The upstream project lists LBP-1120 among supported CAPT v1 models and explicitly says it is a protocol/compression library, not a complete installable printer driver. Its code is portable and can be built natively for x64; this project supplies the missing Windows USB interface experiment.

The separate [captppd](https://github.com/darkvision77/captppd) project wraps libcapt as a Linux CUPS backend/driver using libusb. It cannot be installed directly on Windows.

## Current state

- Confirmed on the user's PC: Windows detects `USB\VID_04A9&PID_262B` as `USB Printing Support`, status `OK`.
- Implemented: x64 build of the libcapt-backed direct-print prototype and PBM test image generation.
- Still unverified: whether Windows `usbprint.sys` exposes the bidirectional CAPT stream needed by this particular printer and whether a page completes on hardware.
- Not yet done: add a normal Windows printer queue so applications can print through the standard Print dialog.

## Safety

Do not force-install the old x86 Canon INF on x64 Windows, and do not replace the device's USB driver. The old Win32 Canon DLLs cannot be loaded into a 64-bit spooler process.
