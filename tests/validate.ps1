[CmdletBinding()]
param([string]$Build = (Join-Path (Split-Path $PSScriptRoot -Parent) 'build'))
$ErrorActionPreference = 'Stop'
$Build = [IO.Path]::GetFullPath($Build)
$report = Join-Path $Build 'validation.txt'
New-Item -ItemType Directory -Path $Build -Force | Out-Null
@(
    "Date: $([DateTimeOffset]::Now.ToString('o'))"
    "OS: $([Environment]::OSVersion.VersionString)"
    "CPU: $((Get-CimInstance Win32_Processor | Select-Object -First 1).Name)"
    "Logical processors: $([Environment]::ProcessorCount)"
    "RAM bytes: $((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory)"
) | Set-Content -LiteralPath $report
& cmake --build $Build --config Release
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
& ctest --test-dir $Build -C Release --output-on-failure -V 2>&1 | Tee-Object -FilePath $report -Append
if ($LASTEXITCODE -ne 0) { throw "Tests failed; see $report" }
$exe = Join-Path $Build 'Release\FileLockInspector.exe'
$target = Join-Path $Build 'startup-fixture.tmp'
[IO.File]::WriteAllBytes($target, [byte[]]@())
$watch = [Diagnostics.Stopwatch]::StartNew()
$process = Start-Process -FilePath $exe -ArgumentList ('"' + $target + '"') -PassThru
try {
    $idle = $process.WaitForInputIdle(10000)
    $watch.Stop()
    "UI input idle: $idle; launch-to-input-idle ms: $($watch.ElapsedMilliseconds)" | Add-Content -LiteralPath $report
    $process.Refresh()
    "UI peak working set bytes: $($process.PeakWorkingSet64); CPU ms: $($process.TotalProcessorTime.TotalMilliseconds)" | Add-Content -LiteralPath $report
    if (-not $process.CloseMainWindow()) { throw 'Could not request UI closure.' }
    if (-not $process.WaitForExit(10000)) { throw 'UI failed to exit.' }
    Start-Sleep -Milliseconds 500
    $worker = Get-CimInstance Win32_Process -Filter "Name='FileLockWorker.exe'" | Where-Object { $_.ParentProcessId -eq $process.Id }
    if ($worker) { throw 'Worker survived UI closure.' }
    'No worker survives UI closure: PASS' | Add-Content -LiteralPath $report
} finally {
    if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
    $process.Dispose()
    Remove-Item -LiteralPath $target -Force
}
Write-Output "Validation report: $report"
