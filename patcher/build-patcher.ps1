param(
    [Parameter(Mandatory = $true)]
    [string]$Delta,

    [string]$Output = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Output)) {
    $Output = Join-Path $PSScriptRoot "dist\PrismModFoldersPatcher-11.0.3.exe"
}

$compiler = "C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
$source = Join-Path $PSScriptRoot "PrismModFoldersPatcher.cs"

if (-not (Test-Path -LiteralPath $compiler)) {
    throw "Не найден компилятор .NET Framework: $compiler"
}
if (-not (Test-Path -LiteralPath $Delta)) {
    throw "Не найден delta-патч: $Delta"
}

$outputDirectory = Split-Path -Parent $Output
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

& $compiler `
    /nologo `
    /target:winexe `
    /optimize+ `
    /platform:x64 `
    /reference:System.dll `
    /reference:System.Core.dll `
    /reference:System.Drawing.dll `
    /reference:System.Windows.Forms.dll `
    "/resource:$Delta,PrismModFolders.Delta.11.0.3" `
    "/out:$Output" `
    $source

if ($LASTEXITCODE -ne 0) {
    throw "Сборка патчера завершилась с кодом $LASTEXITCODE."
}

Get-FileHash -Algorithm SHA256 -LiteralPath $Output
