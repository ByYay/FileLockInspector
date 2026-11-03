[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$binary = Join-Path $root 'build\Release'
$report = Join-Path $root 'build\smoke-validation.txt'
$sandbox = Join-Path $root 'build\test-sandbox'
New-Item -ItemType Directory -Path $sandbox -Force | Out-Null
$file = Join-Path $sandbox ('smoke-' + [Guid]::NewGuid().ToString('N') + '.txt')
[IO.File]::WriteAllText($file, 'Disposable scan test fixture.')
$holders = @()
$results = [Collections.Generic.List[string]]::new()
$results.Add("Date: $([DateTimeOffset]::Now.ToString('o'))")
$results.Add("OS: $([Environment]::OSVersion.VersionString)")
$results.Add("CPU: $((Get-CimInstance Win32_Processor | Select-Object -First 1).Name)")
$results.Add("RAM bytes: $((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory)")
function Invoke-Scan([string]$Mode) {
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = Join-Path $binary 'FileLockWorker.exe'
    $info.Arguments = '--scan ' + $Mode + ' "' + $file + '"'
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.StandardOutputEncoding = [Text.Encoding]::Unicode
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $info
    $started = $false
    try {
        if (-not $process.Start()) { throw 'Scanner failed to start.' }
        $started = $true
        $read = $process.StandardOutput.ReadToEndAsync()
        if (-not $process.WaitForExit(25000)) { throw 'Scanner supervisor exceeded its time bound.' }
        $text = $read.GetAwaiter().GetResult()
        $results.Add("SCAN $Mode`r`n$text")
        if ($process.ExitCode -ne 0) { throw "Scanner failed: $($process.ExitCode)" }
        return $text
    } finally {
        if ($started -and -not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
        $process.Dispose()
    }
}
function Assert-Holder([string]$Text, [int]$HolderId, [bool]$Expected) {
    $found = $Text -match ('(?m)^' + $HolderId + "`t")
    if ($found -ne $Expected) { throw "Unexpected detection result for holder $HolderId (expected=$Expected)." }
}
try {
    for ($i = 0; $i -lt 2; $i++) {
        $eventName = 'Local\FLI.Smoke.' + [Guid]::NewGuid().ToString('N')
        $ready = [Threading.EventWaitHandle]::new($false, [Threading.EventResetMode]::ManualReset, $eventName)
        try {
            $holder = Start-Process -FilePath (Join-Path $binary 'fli_holder.exe') -ArgumentList @(('"' + $file + '"'), $eventName) -WindowStyle Hidden -PassThru
            $holders += $holder
            if (-not $ready.WaitOne(10000)) { throw 'Holder readiness timeout.' }
        } finally { $ready.Dispose() }
    }
    foreach ($mode in @('normal','advanced')) {
        $text = Invoke-Scan $mode
        foreach ($holder in $holders) { Assert-Holder $text $holder.Id $true }
        $results.Add("PASS: $mode detects both test holders")
    }
    # Only this script's disposable fixture process is ended, never a detected arbitrary PID.
    $holders[0].Kill()
    if (-not $holders[0].WaitForExit(5000)) { throw 'Fixture did not exit.' }
    $text = Invoke-Scan 'normal'
    Assert-Holder $text $holders[0].Id $false
    Assert-Holder $text $holders[1].Id $true
    $results.Add('PASS: refresh removes exited holder and retains surviving holder')
    $holders[1].Kill()
    if (-not $holders[1].WaitForExit(5000)) { throw 'Fixture did not exit.' }
    $text = Invoke-Scan 'normal'
    if ($text -match '(?m)^\d+\t') { throw 'Unexpected process detected for unused fixture.' }
    $results.Add('PASS: unused fixture returns no detected process')
    $results.Add('Scope: CLI scanner smoke tests only; full CTest and GUI termination are not covered.')
} catch {
    $results.Add("FAIL: $($_.Exception.Message)")
    throw
} finally {
    foreach ($holder in $holders) {
        if (-not $holder.HasExited) { $holder.Kill(); $holder.WaitForExit() }
        $holder.Dispose()
    }
    Remove-Item -LiteralPath $file -Force
    $results | Set-Content -LiteralPath $report -Encoding UTF8
}
Get-Content -LiteralPath $report
