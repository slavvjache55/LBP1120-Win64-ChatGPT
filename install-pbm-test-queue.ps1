# Run from an elevated PowerShell in the extracted package.
# Experimental queue: accepts only RAW jobs whose payload is binary PBM (P4).
# It does not render Word/PDF/XPS/EMF documents yet.
$ErrorActionPreference = 'Stop'
$queueName = 'Canon LBP-1120 CAPT (PBM test)'
$envName = 'Windows x64'
$procName = 'CAPTPBM'
$procDir = Join-Path $env:windir 'System32\spool\PRTPROCS\x64'
$sourceDll = Join-Path $PSScriptRoot 'CAPTPBM.dll'
if (-not (Test-Path $sourceDll)) { throw "Missing $sourceDll" }

# The processor DLL must live in the spooler print-processor directory.
Copy-Item $sourceDll (Join-Path $procDir 'CAPTPBM.dll') -Force

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class PrintProcessorNative {
  [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
  public struct PRINTPROCESSOR_INFO_1 { public string pName; }
  [DllImport("winspool.drv", CharSet=CharSet.Unicode, SetLastError=true)]
  public static extern bool AddPrintProcessor(string pName, string pEnvironment, string pPath, string pPrintProcessorName);
  [DllImport("winspool.drv", CharSet=CharSet.Unicode, SetLastError=true)]
  public static extern IntPtr AddPrinter(string pName, uint level, ref PRINTER_INFO_2 pPrinter);
  [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
  public struct PRINTER_INFO_2 {
    public string pServerName; public string pPrinterName; public string pShareName;
    public string pPortName; public string pDriverName; public string pComment;
    public string pLocation; public IntPtr pDevMode; public string pSepFile;
    public string pPrintProcessor; public string pDatatype; public string pParameters;
    public IntPtr pSecurityDescriptor; public uint Attributes; public uint Priority;
    public uint DefaultPriority; public uint StartTime; public uint UntilTime;
    public uint Status; public uint cJobs; public uint AveragePPM;
  }
  [DllImport("winspool.drv", SetLastError=true)]
  public static extern bool ClosePrinter(IntPtr hPrinter);
}
'@

$registered = [PrintProcessorNative]::AddPrintProcessor($null, $envName, 'CAPTPBM.dll', $procName)
if (-not $registered) {
  $err = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
  # Error 3005 (already installed) is harmless; other errors must be surfaced.
  if ($err -ne 3005) { throw "AddPrintProcessor failed: Win32 error $err" }
}

$existing = Get-Printer -Name $queueName -ErrorAction SilentlyContinue
if ($existing) {
  Write-Host "Queue already exists: $queueName"
  Write-Host "This queue is PBM-RAW-only; it is not a general Windows document printer."
  exit 0
}

$printer = New-Object PrintProcessorNative+PRINTER_INFO_2
$printer.pPrinterName = $queueName
$printer.pPortName = 'NUL:'
$printer.pDriverName = 'Generic / Text Only'
$printer.pPrintProcessor = $procName
$printer.pDatatype = 'CAPTPBM'
$printer.Attributes = 0x40 # PRINTER_ATTRIBUTE_LOCAL
$h = [PrintProcessorNative]::AddPrinter($null, 2, [ref]$printer)
if ($h -eq [IntPtr]::Zero) {
  $err = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
  throw "AddPrinter failed: Win32 error $err. Queue not created; no existing driver was changed."
}
[void][PrintProcessorNative]::ClosePrinter($h)
Write-Host "Created '$queueName'. This experimental queue accepts only RAW binary PBM P4 jobs."
Write-Host "It does NOT yet render Word/PDF/XPS/EMF documents. Keep the existing USB device driver installed."
