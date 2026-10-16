$ErrorActionPreference = 'Stop'

Add-Type @'
using System;
using System.Runtime.InteropServices;

public static class MinimalPrintProcessorApi {
    [DllImport("winspool.drv", EntryPoint="AddPrintProcessorW",
        CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern bool AddPrintProcessor(
        string server, string environment, string pathName, string processorName);

    [DllImport("winspool.drv", EntryPoint="EnumPrintProcessorDatatypesW",
        CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern bool EnumPrintProcessorDatatypes(
        string server, string processorName, uint level, IntPtr buffer,
        uint cbBuf, out uint needed, out uint returned);
}
'@

Write-Host '1. Registering diagnostic-only CAPTTEST (does not print)...'
$ok = [MinimalPrintProcessorApi]::AddPrintProcessor(
    $null, 'Windows x64', 'CAPTTEST.dll', 'CAPTTEST')
$err = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
"AddPrintProcessor success: $ok"
"AddPrintProcessor Win32 error: $err"
if (-not $ok) {
    (New-Object ComponentModel.Win32Exception($err)).Message
    Write-Host 'Registration failed. Do not change CAPTPBM; inspect CAPTTEST-enum.log and PrintService/Admin events.'
    exit 1
}

Write-Host '2. Asking the spooler to enumerate CAPTTEST datatypes...'
[uint32]$needed = 0
[uint32]$returned = 0
$ok = [MinimalPrintProcessorApi]::EnumPrintProcessorDatatypes(
    $null, 'CAPTTEST', 1, [IntPtr]::Zero, 0, [ref]$needed, [ref]$returned)
$err = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
"First enumeration success (expected false with insufficient buffer): $ok"
"First enumeration Win32 error (expected 122): $err"
"Required bytes: $needed"

if ($needed -gt 0 -and $needed -lt 65536) {
    $buffer = [Runtime.InteropServices.Marshal]::AllocHGlobal([int]$needed)
    try {
        $ok = [MinimalPrintProcessorApi]::EnumPrintProcessorDatatypes(
            $null, 'CAPTTEST', 1, $buffer, $needed, [ref]$needed, [ref]$returned)
        $err = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        "Second enumeration success: $ok"
        "Second enumeration Win32 error: $err"
        "Returned datatype count: $returned"
    } finally {
        [Runtime.InteropServices.Marshal]::FreeHGlobal($buffer)
    }
} else {
    Write-Host 'Unexpected required buffer size; do not continue with this result.'
    exit 2
}

Write-Host 'Check C:\Windows\Temp\CAPTTEST-enum.log for calls into the test DLL.'
Write-Host 'This test DLL is diagnostic only and intentionally cannot print jobs.'
