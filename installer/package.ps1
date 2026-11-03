[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Publisher,
    [string]$Output = (Join-Path (Split-Path $PSScriptRoot -Parent) 'build\package'),
    [string]$MakeAppx = 'makeappx.exe'
)
$ErrorActionPreference = 'Stop'
$Output = [IO.Path]::GetFullPath($Output)
$staging = Join-Path $Output 'staging'
New-Item -ItemType Directory -Path (Join-Path $staging 'Assets') -Force | Out-Null
[xml]$manifest = Get-Content -LiteralPath (Join-Path (Split-Path $PSScriptRoot -Parent) 'shell\Package.appxmanifest') -Raw
$manifest.Package.Identity.Publisher = $Publisher
$manifest.Save((Join-Path $staging 'AppxManifest.xml'))
Add-Type -AssemblyName System.Drawing
foreach ($entry in @(@('StoreLogo',50),@('Square44x44Logo',44),@('Square150x150Logo',150))) {
    $size = [int]$entry[1]
    $bitmap = New-Object Drawing.Bitmap($size,$size)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $pen = New-Object Drawing.Pen([Drawing.Color]::White, ([single]($size / 14)))
    try {
        $graphics.Clear([Drawing.Color]::FromArgb(32,90,145))
        $graphics.DrawArc($pen, [single]($size*.30), [single]($size*.17), [single]($size*.40), [single]($size*.45), 180, 180)
        $graphics.DrawRectangle($pen, [single]($size*.22), [single]($size*.42), [single]($size*.56), [single]($size*.40))
        $bitmap.Save((Join-Path $staging "Assets\$($entry[0]).png"), [Drawing.Imaging.ImageFormat]::Png)
    } finally { $pen.Dispose(); $graphics.Dispose(); $bitmap.Dispose() }
}
$package = Join-Path $Output 'FileLockInspector.msix'
& $MakeAppx pack /d $staging /p $package /nv /o
if ($LASTEXITCODE -ne 0) { throw "MakeAppx failed: $LASTEXITCODE" }
Write-Output "Unsigned sparse package: $package. Sign with a certificate matching '$Publisher', then install with -Mode Modern -PackagePath. This script does not install or trust certificates."
