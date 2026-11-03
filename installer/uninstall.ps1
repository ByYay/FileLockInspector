[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
if (-not [Environment]::Is64BitProcess) { throw 'Use 64-bit PowerShell.' }
Get-AppxPackage -Name 'FileLockInspector.Desktop' | ForEach-Object { Remove-AppxPackage -Package $_.PackageFullName -ErrorAction Stop }
foreach ($key in @('Software\Classes\*\shell\FileLockInspector','Software\Classes\Directory\shell\FileLockInspector','Software\Classes\CLSID\{2F0DA1C3-406A-493B-93AA-6F408546812A}')) {
    [Microsoft.Win32.Registry]::CurrentUser.DeleteSubKeyTree($key, $false)
}
$destination = [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'Programs\FileLockInspector'))
# Delete only known application files. Never recursively delete a user-supplied directory.
$left = @()
foreach ($file in @('FileLockInspector.exe','FileLockWorker.exe','FileLockShell.dll','uninstall.ps1')) {
    $target = Join-Path $destination $file
    if (Test-Path -LiteralPath $target) {
        try { Remove-Item -LiteralPath $target -Force -ErrorAction Stop }
        catch { $left += $target }
    }
}
if ($left.Count) { throw "Registration removed. Close the application and sign out to unload the Shell DLL, then rerun uninstall. Remaining files: $($left -join ', ')" }
if ((Test-Path -LiteralPath $destination) -and -not (Get-ChildItem -LiteralPath $destination -Force)) { Remove-Item -LiteralPath $destination }
Write-Output 'File Lock Inspector removed. No user files were deleted.'
