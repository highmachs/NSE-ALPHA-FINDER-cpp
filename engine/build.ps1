$SCRIPT_DIR = $PSScriptRoot
Write-Host "[build] Configuring cmake..." -ForegroundColor Cyan
cmake -B (Join-Path $SCRIPT_DIR "build") -S $SCRIPT_DIR -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
Write-Host "[build] Compiling C++ engine..." -ForegroundColor Cyan
cmake --build (Join-Path $SCRIPT_DIR "build") --parallel 4
Write-Host "[build] Done. Module at: $SCRIPT_DIR\build_output\" -ForegroundColor Cyan
Get-ChildItem (Join-Path $SCRIPT_DIR "build_output")
