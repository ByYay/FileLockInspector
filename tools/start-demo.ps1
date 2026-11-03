[CmdletBinding()]
param([ValidateRange(1,2)][int]$Holders = 2)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$binary = Join-Path $root 'build\Release'
foreach ($name in @('fli_holder.exe','FileLockInspector.exe','FileLockWorker.exe')) {
    if (-not (Test-Path -LiteralPath (Join-Path $binary $name))) { throw 'Run tools\build-test.ps1 first.' }
}
$sandbox = Join-Path $root 'build\test-sandbox'
New-Item -ItemType Directory -Path $sandbox -Force | Out-Null
$file = Join-Path $sandbox 'Sample locked file.txt'
if (-not (Test-Path -LiteralPath $file)) { [IO.File]::WriteAllText($file, 'Disposable File Lock Inspector test fixture.') }
$children = @()
try {
    for ($i = 0; $i -lt $Holders; $i++) {
        $eventName = 'Local\FLI.Demo.' + [Guid]::NewGuid().ToString('N')
        $ready = New-Object Threading.EventWaitHandle($false, [Threading.EventResetMode]::ManualReset, $eventName)
        try {
            # Visible consoles are intentional: Enter closes each interactive holder.
            $child = Start-Process -FilePath (Join-Path $binary 'fli_holder.exe') -ArgumentList @(('"' + $file + '"'), $eventName) -PassThru
            $children += $child
            if (-not $ready.WaitOne(10000)) { throw 'Test holder did not become ready.' }
        } finally { $ready.Dispose() }
    }
    Start-Process -FilePath (Join-Path $binary 'FileLockInspector.exe') -ArgumentList ('"' + $file + '"')
    Write-Output "Test file: $file"
    Write-Output "Holder PIDs: $(($children | ForEach-Object { $_.Id }) -join ', '). Press Enter in each holder console to release it, or test End Process / Force Terminate in the inspector."
} catch {
    foreach ($child in $children) { if (-not $child.HasExited) { $child.Kill(); $child.WaitForExit() } }
    throw
} finally { foreach ($child in $children) { $child.Dispose() } }
