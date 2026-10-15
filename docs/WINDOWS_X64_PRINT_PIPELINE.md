# Windows x64 print pipeline plan

## Goal
Print from Word, PDF viewers, browsers, and other Windows applications through the standard Print dialog on Windows 10 x64, using the hardware-tested CAPT v1/SCoA transport. This document is a plan, not a claim that a complete driver already exists.

## Verified baseline
- The native x64 `lbp1120-capt-print.exe` has printed a PBM P4 test page on the user's physical Canon LBP-1120.
- Keep the CAPT transport as a separate, reusable user-mode component.
- The experimental `CAPTPBM` print processor accepts RAW PBM P4 only. It does not render Word/PDF/XPS/EMF.

## Proposed pipeline
1. Install a separate x64 Windows print queue/package; do not replace the USB device driver or affect other queues.
2. Use a supported Windows document-rendering path, evaluating a V4/XPS filter pipeline if the required Windows 10/WDK toolchain and package model are available.
3. Render each page to a 1-bit monochrome raster at the supported resolution, respecting page size, orientation, printable margins, scaling, and clipping.
4. Pass validated raster/page data to the existing CAPT v1/SCoA transport.
5. Serialize access to the physical printer and handle cancellation, timeouts, status, and errors.

## V4/XPS caveat
A V4 package and XPS filter pipeline are candidates, not an assumption. They require compatible package/filter configuration, INF/catalog/signing considerations, and a suitable build environment. The current CMake GitHub Actions workflow does not produce such a package. Confirm WDK availability and a supported deployment path before writing an installer. If this cannot be built and validated in the hosted environment, evaluate another supported user-mode rendering/print route.

## Milestones
### 1. Renderer proof of concept
- Define page geometry and a deterministic raster format.
- Verify dimensions, row stride, bit order, orientation, and margins.
- Preserve the direct-print executable as the hardware baseline.

### 2. Windows pipeline proof
- Build a minimal x64 queue/package in an isolated branch.
- First route a Windows test page to a diagnostic sink, not the physical printer.
- Confirm that a simple text document reaches the renderer in a supported format.

### 3. End-to-end hardware
- Connect the queue to CAPT transport.
- Test Windows test page, Notepad, Word, and PDF.
- Test cancellation, repeat jobs, disconnect/recovery, and status.

### 4. Packaging
- Provide install/uninstall and rollback.
- Preserve the existing Canon USB driver and unrelated queues.
- Document signing requirements. Do not call the result production-ready before end-to-end tests pass.

## Non-goals
- Replacing the USB device driver.
- Loading legacy x86 Canon DLLs in the x64 spooler.
- Claiming general document support from the PBM-only print processor.
- Changing the user's system as part of a build workflow.
