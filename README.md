# Canon LBP-1120 Windows x64 prototype

This repository is an early diagnostic/build prototype, **not a working printer driver**.

GitHub Actions builds a native x64 Windows USB/PnP diagnostic executable using Microsoft's hosted Windows runner. It searches Windows Plug-and-Play for Canon USB hardware ID `VID_04A9&PID_262B`. It only reads device properties; it does not change drivers or send data to the printer.

## Get the EXE

1. Open the **Actions** tab.
2. Select **Build Canon LBP-1120 x64 prototype**.
3. Open the latest run and wait for it to finish.
4. Download artifact **LBP1120-Win64-prototype**.
5. Extract the ZIP and run `lbp1120-usb-probe.exe` with the printer connected over USB.

No local Visual Studio installation is needed.

## What this is not

The diagnostic utility does not print. The open-source `libcapt` project implements parts of Canon CAPT v1 and SCoA, but a Windows print pipeline and compatible USB transport still need to be implemented and tested. Do not uninstall or replace the current Canon USB driver at this stage.

## Upstream project

- https://github.com/darkvision77/libcapt
