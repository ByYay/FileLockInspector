[CmdletBinding()]
param(
    [ValidateSet('Classic','Modern')][string]$Mode = 'Classic',
    [string]$Source = (Split-Path $PSScriptRoot -Parent),
    [string]$PackagePath
)
$ErrorActionPreference = 'Stop'
if (-not [Environment]::Is64BitProcess) { throw 'Use 64-bit PowerShell.' }
$destination = Join-Path $env:LOCALAPPDATA 'Programs\FileLockInspector'
$classId = '{2F0DA1C3-406A-493B-93AA-6F408546812A}'
$classKey = "Software\Classes\CLSID\$classId"
$verbs = @('Software\Classes\*\shell\FileLockInspector', 'Software\Classes\Directory\shell\FileLockInspector')
$files = @('FileLockInspector.exe','FileLockWorker.exe','FileLockShell.dll')
foreach ($file in $files) {
    if (-not (Test-Path -LiteralPath (Join-Path $Source $file) -PathType Leaf)) { throw "Missing build output: $file" }
}
if ($Mode -eq 'Modern') {
    if (-not $PackagePath -or -not (Test-Path -LiteralPath $PackagePath -PathType Leaf)) { throw 'Modern mode requires a signed sparse MSIX: -PackagePath.' }
    $PackagePath = (Resolve-Path -LiteralPath $PackagePath).Path
}
# Stage every file and check all existing binaries before replacing any of them.
New-Item -ItemType Directory -Path $destination -Force | Out-Null
$transaction = Join-Path $destination ('.install-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $transaction | Out-Null
$changed = @()
$existing = @{}
$allFiles = $files + @('uninstall.ps1')
try {
    foreach ($file in $allFiles) {
        $from = if ($file -eq 'uninstall.ps1') { Join-Path $PSScriptRoot $file } else { Join-Path $Source $file }
        Copy-Item -LiteralPath $from -Destination (Join-Path $transaction $file) -Force
        $to = Join-Path $destination $file
        $existing[$file] = Test-Path -LiteralPath $to
        if ($existing[$file]) {
            if ($file -ne 'uninstall.ps1') {
                $probe = [IO.File]::Open($to, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
                $probe.Dispose()
            }
            Copy-Item -LiteralPath $to -Destination (Join-Path $transaction ($file + '.backup'))
        }
    }
    foreach ($file in $allFiles) {
        $changed += $file
        Copy-Item -LiteralPath (Join-Path $transaction $file) -Destination (Join-Path $destination $file) -Force
    }
} catch {
    $failure = $_
    foreach ($file in $changed) {
        try {
            if ($existing[$file]) { Copy-Item -LiteralPath (Join-Path $transaction ($file + '.backup')) -Destination (Join-Path $destination $file) -Force }
            else {
                $created = Join-Path $destination $file
                if (Test-Path -LiteralPath $created) { Remove-Item -LiteralPath $created -Force }
            }
        } catch { Write-Warning "Rollback failed for $file. Backup retained in $transaction. Close Explorer/app and restore before use." }
    }
    throw $failure
}
if ($Mode -eq 'Modern') {
    Add-AppxPackage -Path $PackagePath -ExternalLocation $destination -ErrorAction Stop
    foreach ($verb in $verbs) { [Microsoft.Win32.Registry]::CurrentUser.DeleteSubKeyTree($verb, $false) }
    [Microsoft.Win32.Registry]::CurrentUser.DeleteSubKeyTree($classKey, $false)
} else {
    # Remove modern registration first; never deliberately enable duplicate menu entries.
    Get-AppxPackage -Name 'FileLockInspector.Desktop' | ForEach-Object { Remove-AppxPackage -Package $_.PackageFullName -ErrorAction Stop }
    $key = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey("$classKey\InprocServer32")
    try { $key.SetValue('', (Join-Path $destination 'FileLockShell.dll')); $key.SetValue('ThreadingModel','Apartment') } finally { $key.Dispose() }
    foreach ($verb in $verbs) {
        $key = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($verb)
        try {
            $key.SetValue('', 'Who is using this file?')
            $key.SetValue('ExplorerCommandHandler', $classId)
            $key.SetValue('MultiSelectModel', 'Single')
        } finally { $key.Dispose() }
    }
}
# Only remove exact files created by this transaction; no recursive delete.
foreach ($file in $allFiles) {
    foreach ($name in @($file, ($file + '.backup'))) {
        $temporary = Join-Path $transaction $name
        if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force }
    }
}
Remove-Item -LiteralPath $transaction
Write-Output "Installed ($Mode): $destination. Explorer may require sign-out/sign-in to refresh its cached menu. No background process was started."
