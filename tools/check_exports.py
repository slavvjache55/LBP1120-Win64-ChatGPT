import sys
from pathlib import Path
import pefile

dll = Path(sys.argv[1])
required = {
    "EnumPrintProcessorDatatypes",
    "OpenPrintProcessor",
    "PrintDocumentOnPrintProcessor",
    "ClosePrintProcessor",
    "ControlPrintProcessor",
    "GetPrintProcessorCapabilities",
}
pe = pefile.PE(str(dll), fast_load=True)
pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"]])
exports = {entry.name.decode("ascii", errors="replace")
           for entry in pe.DIRECTORY_ENTRY_EXPORT.symbols if entry.name}
print("Exports found:")
for name in sorted(exports):
    print(f"  {name}")
missing = sorted(required - exports)
if missing:
    print("Missing required exports: " + ", ".join(missing), file=sys.stderr)
    sys.exit(1)
print("All required print-processor exports are present.")
