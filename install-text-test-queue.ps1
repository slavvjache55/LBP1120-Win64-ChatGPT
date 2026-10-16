# Run this script from an elevated PowerShell in the extracted ZIP.
$ErrorActionPreference = 'Stop'
$queue = 'Canon LBP-1120 CAPT (Text test)'
$dir = Join-Path $env:windir 'System32\spool\PRTPROCS\x64'
foreach ($f in @('CAPTPBM.dll','lbp1120-capt-print.exe','lbp1120-text-print.exe')) {
  if (-not (Test-Path (Join-Path $PSScriptRoot $f))) { throw "Missing package file: $f" }
}
if (-not (Get-PrinterDriver -Name 'Generic / Text Only' -ErrorAction SilentlyContinue)) {
  Write-Host "Generic / Text Only is not registered. Asking Windows to register its inbox driver..."
  try {
    Add-PrinterDriver -Name 'Generic / Text Only' -ErrorAction Stop
  } catch {
    throw ('Windows could not register Generic / Text Only. No printer files or queues were changed. Details: ' + $_.Exception.Message)
  }
}
if (-not (Get-PrinterDriver -Name 'Generic / Text Only' -ErrorAction SilentlyContinue)) {
  throw 'Windows could not register Generic / Text Only. No printer files or queues were changed.'
}
Write-Host 'This creates a separate experimental text-only queue. It briefly restarts the Print Spooler.'
Write-Host 'It does not replace the USB device driver or change other printer queues.'
if ((Read-Host 'Confirm there are no active print jobs. Continue? (Y/N)') -notin @('Y','y')) { throw 'Cancelled; no changes made.' }
$running = (Get-Service Spooler).Status -eq 'Running'
try {
  if ($running) { Stop-Service Spooler -Force; (Get-Service Spooler).WaitForStatus('Stopped',[TimeSpan]::FromSeconds(30)) }
  foreach ($f in @('CAPTPBM.dll','lbp1120-capt-print.exe','lbp1120-text-print.exe')) {
    Copy-Item (Join-Path $PSScriptRoot $f) (Join-Path $dir $f) -Force
  }
} finally {
  if ($running) { Start-Service Spooler; (Get-Service Spooler).WaitForStatus('Running',[TimeSpan]::FromSeconds(30)) }
}
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class LbpPrintNative {
 [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
 public struct PRINTER_INFO_2 {
  public string pServerName,pPrinterName,pShareName,pPortName,pDriverName,pComment,pLocation;
  public IntPtr pDevMode; public string pSepFile,pPrintProcessor,pDatatype,pParameters;
  public IntPtr pSecurityDescriptor; public uint Attributes,Priority,DefaultPriority,StartTime,UntilTime,Status,cJobs,AveragePPM;
 }
 [DllImport("winspool.drv", CharSet=CharSet.Unicode, SetLastError=true)]
 public static extern bool AddPrintProcessor(string server,string environment,string path,string name);
 [DllImport("winspool.drv", CharSet=CharSet.Unicode, SetLastError=true)]
 public static extern IntPtr AddPrinter(string server,uint level,ref PRINTER_INFO_2 info);
 [DllImport("winspool.drv", SetLastError=true)]
 public static extern bool ClosePrinter(IntPtr h);
}
'@
if (-not [LbpPrintNative]::AddPrintProcessor($null,'Windows x64','CAPTPBM.dll','CAPTPBM')) {
  $err = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
  if ($err -ne 3005) { throw "AddPrintProcessor failed (Win32 $err). Files copied, queue not created." }
}
if (Get-Printer -Name $queue -ErrorAction SilentlyContinue) { Write-Host "Queue already exists: $queue"; exit 0 }
$p = New-Object LbpPrintNative+PRINTER_INFO_2
$p.pPrinterName = $queue
$p.pPortName = 'NUL:'
$p.pDriverName = 'Generic / Text Only'
$p.pPrintProcessor = 'CAPTPBM'
$p.pDatatype = 'RAW'
$p.Attributes = 0x40
$h = [LbpPrintNative]::AddPrinter($null,2,[ref]$p)
if ($h -eq [IntPtr]::Zero) { $err=[Runtime.InteropServices.Marshal]::GetLastWin32Error(); throw "AddPrinter failed (Win32 $err)." }
[LbpPrintNative]::ClosePrinter($h) | Out-Null
Write-Host "Installed '$queue'. Test with Notepad and one short line first."
Write-Host 'This prototype prints plain text only; Word formatting, images and PDF graphics are not supported yet.'
Write-Host 'To remove the queue: Settings > Printers & scanners > select this queue > Remove.'
