param(
    [string]$Command = "help",
    [Parameter(ValueFromRemainingArguments=$true)]
    [string[]]$ArgsRemaining
)

$SCRIPT_DIR = $PSScriptRoot
$BUILD_OUT = Join-Path $SCRIPT_DIR "build_output"
$PYTHON = Join-Path $SCRIPT_DIR "python"

function Write-Ok { param([string]$Message) Write-Host "[OK]  $Message" -ForegroundColor Green }
function Write-Info { param([string]$Message) Write-Host "[..]  $Message" -ForegroundColor Cyan }
function Write-Fail { param([string]$Message) Write-Host "[!!]  $Message" -ForegroundColor Red }
function Write-Sep { Write-Host "----------------------------------------------------" -ForegroundColor DarkGray }

function Show-Usage {
    Write-Host @"
 ███╗   ██╗███████╗███████╗     █████╗ ██╗      ██████╗ ██╗  ██╗ █████╗
 ████╗  ██║██╔════╝██╔════╝    ██╔══██╗██║     ██╔══██╗██║  ██║██╔══██╗
 ██╔██╗ ██║███████╗█████╗      ███████║██║     ██████╔╝███████║███████║
 ██║╚██╗██║╚════██║██╔══╝      ██╔══██║██║     ██╔═══╝ ██╔══██║██╔══██║
 ██║ ╚████║███████║███████╗    ██║  ██║███████╗██║     ██║  ██║██║  ██║
 ╚═╝  ╚═══╝╚══════╝╚══════╝    ╚═╝  ╚═╝╚══════╝╚═╝     ╚═╝  ╚═╝╚═╝  ╚═╝
"@ -ForegroundColor Cyan
    Write-Host "`nCommands:" -ForegroundColor White
    Write-Host "  build                   Rebuild all C++ targets (module, CLI, tests)"
    Write-Host "  test                    Run C++ unit tests"
    Write-Host "  serve [PORT]            Start FastAPI server (default: 8000)"
    Write-Host "  cli <csv> [options]     Run C++ standalone CLI on a CSV file"
    Write-Host "  benchmark [ROWS]        C++ benchmark (default: 1 000 000 rows)"
    Write-Host "  bench-table [MAX_ROWS]  Full benchmark table across all row counts"
    Write-Host "  compare [ROWS]          C++ vs pure-Python speed comparison"
    Write-Host "  all-strategies <csv>    Run all 3 strategies + comparison table"
    Write-Host "  validate <csv>          Validate OHLCV data"
    Write-Host "  fetch <SYM> <START> ... Download NSE OHLCV data"
    Write-Host "  help                    Show this message"
}

function Invoke-BuildIfNeeded {
    if (-not (Test-Path (Join-Path $BUILD_OUT "nse_engine.exe"))) {
        Write-Info "Binary not found - building first..."
        Invoke-Build
    }
}

function Invoke-Build {
    Write-Sep
    Write-Info "Configuring CMake (Release, -O3)..."
    cmake -B (Join-Path $SCRIPT_DIR "build") -S $SCRIPT_DIR -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -Wno-dev
    Write-Info "Compiling targets..."
    cmake --build (Join-Path $SCRIPT_DIR "build") --parallel 4
    Write-Sep
    Write-Ok "Build complete."
}

switch ($Command) {
    "build" {
        Invoke-Build
    }
    "test" {
        Invoke-BuildIfNeeded
        Write-Sep
        Write-Info "Running C++ unit tests..."
        & (Join-Path $BUILD_OUT "nse_tests.exe")
    }
    "serve" {
        $PORT = if ($ArgsRemaining.Count -gt 0) { $ArgsRemaining[0] } else { "8000" }
        Write-Sep
        Write-Info "Starting FastAPI server on port $PORT..."
        $env:PORT = $PORT
        & "C:/Python313/python.exe" (Join-Path $PYTHON "server.py")
    }
    "cli" {
        Invoke-BuildIfNeeded
        if ($ArgsRemaining.Count -lt 1) {
            Write-Host "Usage: .\run.ps1 cli <csv_file> [options]"
            exit 1
        }
        & (Join-Path $BUILD_OUT "nse_engine.exe") $ArgsRemaining
    }
    "benchmark" {
        Invoke-BuildIfNeeded
        $ROWS = if ($ArgsRemaining.Count -gt 0) { $ArgsRemaining[0] } else { "1000000" }
        Write-Sep
        Write-Info "C++ standalone benchmark - $ROWS rows..."
        & (Join-Path $BUILD_OUT "nse_engine.exe") "--benchmark" "--rows" $ROWS
    }
    "bench-table" {
        $MAX = if ($ArgsRemaining.Count -gt 0) { $ArgsRemaining[0] } else { "1000000" }
        Write-Sep
        Write-Info "Full benchmark table..."
        & "C:/Python313/python.exe" (Join-Path $PYTHON "benchmark_table.py") "--max-rows" $MAX
    }
    "compare" {
        $ROWS = if ($ArgsRemaining.Count -gt 0) { $ArgsRemaining[0] } else { "100000" }
        Write-Sep
        Write-Info "C++ vs pure-Python comparison..."
        & "C:/Python313/python.exe" (Join-Path $PYTHON "compare.py") $ROWS
    }
    "all-strategies" {
        Invoke-BuildIfNeeded
        if ($ArgsRemaining.Count -lt 1) {
            Write-Host "Usage: .\run.ps1 all-strategies <csv_file>"
            exit 1
        }
        & (Join-Path $BUILD_OUT "nse_engine.exe") $ArgsRemaining[0] "--strategy" "all" "--benchmark"
    }
    "validate" {
        if ($ArgsRemaining.Count -lt 1) {
            Write-Host "Usage: .\run.ps1 validate <csv_file>"
            exit 1
        }
        $csv = $ArgsRemaining[0]
        # In a generic windows environment calling python inline script
        $script = @"
import sys
sys.path.insert(0, r'$BUILD_OUT'.replace('\\', '/'))
import nse_engine_cpp as e

data = e.DataIngestionEngine.load_from_csv(r'$csv', e.MissingValuePolicy.DROP)
e.DataUtils.normalise_timestamps(data)
errs = e.DataUtils.validate(data)

print(f"  Rows loaded : {data.size()}")
print(f"  Date range  : {data.timestamp[0]}  ->  {data.timestamp[-1]}")
print(f"  Price range : {min(data.close):.2f}  -  {max(data.close):.2f}")

if not errs:
    print("  Validation  : PASS (0 errors)")
else:
    print(f"  Validation  : {len(errs)} warning(s)")
    for err in errs[:10]:
        print(f"    row {err.row}: [{err.field}] {err.reason}")
"@
        & "C:/Python313/python.exe" "-c" $script
    }
    "fetch" {
        if ($ArgsRemaining.Count -lt 2) {
            Write-Host "Usage: .\run.ps1 fetch <SYMBOL> <START_DATE> [END_DATE] [SOURCE] [OUTPUT_DIR]"
            exit 1
        }
        & "C:/Python313/python.exe" (Join-Path $PYTHON "data_fetcher.py") $ArgsRemaining
    }
    default {
        if ($Command -ne "help" -and $Command -ne "-h" -and $Command -ne "--help") {
            Write-Fail "Unknown command: $Command"
        }
        Show-Usage
    }
}
